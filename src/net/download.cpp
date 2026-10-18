#include "download.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // curl.h pulls windows.h; keep std::numeric_limits<...>::max usable.
#endif
#endif
#include <curl/curl.h>

#include <array>
#include <cstdio>
#include <memory>
#include <limits>
#include <stdexcept>
#include <utility>

namespace albert {

void DownloadProgress::stage(std::string name, int completed) {
    checkCancelled();
    std::lock_guard lock(mutex_);
    snapshot_ = {std::move(name), completed, 0, 0};
}

void DownloadProgress::transfer(std::int64_t received, std::int64_t total) {
    std::lock_guard lock(mutex_);
    snapshot_.received = received;
    snapshot_.total = total;
}

DownloadSnapshot DownloadProgress::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

void DownloadProgress::checkCancelled() const {
    if (cancelled.load()) throw std::runtime_error("Download cancelado");
}

namespace {
struct Response {std::string bytes;std::size_t limit;bool overflow=false;};
std::size_t writeString(char* data, std::size_t size, std::size_t count, void* user) noexcept {
    try {
        auto& response=*static_cast<Response*>(user);
        if((size&&count>std::numeric_limits<std::size_t>::max()/size)||size*count>response.limit-response.bytes.size()){response.overflow=true;return 0;}
        const auto bytes = size * count;
        response.bytes.append(data, bytes);
        return bytes;
    } catch (...) {
        return 0; // Report allocation failures through CURL, never across C ABI.
    }
}

int reportProgress(void* user, curl_off_t total, curl_off_t received,
                   curl_off_t, curl_off_t) noexcept {
    auto& progress = *static_cast<DownloadProgress*>(user);
    if (progress.cancelled.load()) return 1;
    try {
        progress.transfer(received, total);
        return 0;
    } catch (...) {
        return 1;
    }
}
} // namespace

std::string httpGet(const std::string& url, DownloadProgress* progress,HttpOptions options) {
    if (progress) progress->checkCancelled();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("CURL indisponivel");

    if(options.maximumBytes==0||options.timeoutSeconds<1||options.timeoutSeconds>90)throw std::invalid_argument("Invalid HTTP limits");
    Response response{{},options.maximumBytes};
    std::array<char, CURL_ERROR_SIZE> error{};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeString);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error.data());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, options.timeoutSeconds);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 100L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "AlbertEngine/0.2 (+https://github.com/AllanOliveiraM/albert-engine)");
    if(!options.range.empty())curl_easy_setopt(curl.get(),CURLOPT_RANGE,options.range.c_str());
    if (progress) {
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, reportProgress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, progress);
    }

    const auto code = curl_easy_perform(curl.get());
    if(!options.range.empty()&&code==CURLE_OK){long status=0;curl_easy_getinfo(curl.get(),CURLINFO_RESPONSE_CODE,&status);if(status!=206)throw std::runtime_error("HTTP: server ignored the byte range");}
    if (progress) progress->checkCancelled();
    if(response.overflow)throw std::runtime_error("HTTP response exceeds configured size limit");
    if (code != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP: ") +
            (error[0] ? error.data() : curl_easy_strerror(code)));
    }
    return std::move(response.bytes);
}

namespace {
struct FileSink {
    std::FILE* file=nullptr;std::uint64_t offset=0,expected=0,written=0;CURL* curl=nullptr;DownloadProgress* progress=nullptr;bool checked=false,restart=false,overflow=false,failed=false;
};
std::FILE* openFile(const std::filesystem::path& path,bool append){
#ifdef _WIN32
    return _wfopen(path.c_str(),append?L"ab":L"wb");
#else
    return std::fopen(path.c_str(),append?"ab":"wb");
#endif
}
std::size_t writeFile(char* data,std::size_t size,std::size_t count,void* user) noexcept {
    auto& sink=*static_cast<FileSink*>(user);const auto bytes=size*count;
    if(sink.progress&&sink.progress->cancelled.load())return 0; // Stop at the next block, not the next progress tick.
    if(!sink.checked){
        // Only now is the final response known (after redirects). HTTP 200 means
        // the server ignored the range: discard the partial file and start over.
        long status=0;curl_easy_getinfo(sink.curl,CURLINFO_RESPONSE_CODE,&status);sink.checked=true;
        if(status==200&&sink.offset>0){std::fclose(sink.file);sink.file=nullptr;sink.restart=true;sink.offset=0;}
        else if(status!=200&&status!=206){sink.failed=true;return 0;}
    }
    if(!sink.file)return 0;
    if(bytes>sink.expected-sink.offset-sink.written){sink.overflow=true;return 0;}
    if(std::fwrite(data,1,bytes,sink.file)!=bytes){sink.failed=true;return 0;}
    sink.written+=bytes;return bytes;
}
struct ResumeProgress {DownloadProgress* progress;std::uint64_t offset,expected;};
int reportResume(void* user,curl_off_t,curl_off_t received,curl_off_t,curl_off_t) noexcept {
    auto& state=*static_cast<ResumeProgress*>(user);
    if(state.progress->cancelled.load())return 1;
    try{state.progress->transfer(static_cast<std::int64_t>(state.offset)+received,static_cast<std::int64_t>(state.expected));return 0;}catch(...){return 1;}
}
}

std::uint64_t httpDownloadFile(const std::string& url,const std::filesystem::path& partial,std::uint64_t expectedBytes,DownloadProgress* progress,long stallSeconds){
    if(progress)progress->checkCancelled();
    if(expectedBytes==0||stallSeconds<5||stallSeconds>600)throw std::invalid_argument("Invalid download limits");
    std::error_code error;auto existing=std::filesystem::exists(partial,error)?std::filesystem::file_size(partial,error):0;
    if(error||existing>expectedBytes){std::filesystem::remove(partial,error);existing=0;}
    if(existing==expectedBytes)return existing;
    std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> curl(curl_easy_init(),curl_easy_cleanup);
    if(!curl)throw std::runtime_error("CURL indisponivel");
    FileSink sink;sink.offset=existing;sink.expected=expectedBytes;sink.curl=curl.get();sink.progress=progress;
    sink.file=openFile(partial,existing>0);if(!sink.file)throw std::runtime_error("Nao foi possivel gravar "+partial.filename().string());
    std::array<char,CURL_ERROR_SIZE> message{};const auto range=std::to_string(existing)+"-";
    curl_easy_setopt(curl.get(),CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl.get(),CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl.get(),CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(curl.get(),CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(curl.get(),CURLOPT_WRITEFUNCTION,writeFile);
    curl_easy_setopt(curl.get(),CURLOPT_WRITEDATA,&sink);
    curl_easy_setopt(curl.get(),CURLOPT_ERRORBUFFER,message.data());
    curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT,15L);
    curl_easy_setopt(curl.get(),CURLOPT_LOW_SPEED_LIMIT,1024L);
    curl_easy_setopt(curl.get(),CURLOPT_LOW_SPEED_TIME,stallSeconds);
    curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl.get(),CURLOPT_USERAGENT,"AlbertEngine/0.2 (+https://github.com/AllanOliveiraM/albert-engine)");
    if(existing>0)curl_easy_setopt(curl.get(),CURLOPT_RANGE,range.c_str());
    ResumeProgress state{progress,existing,expectedBytes};
    if(progress){curl_easy_setopt(curl.get(),CURLOPT_NOPROGRESS,0L);curl_easy_setopt(curl.get(),CURLOPT_XFERINFOFUNCTION,reportResume);curl_easy_setopt(curl.get(),CURLOPT_XFERINFODATA,&state);}
    auto code=curl_easy_perform(curl.get());
    if(sink.restart&&code==CURLE_WRITE_ERROR){
        // Range ignored: restart the same request from zero into a fresh file.
        sink=FileSink{};sink.expected=expectedBytes;sink.curl=curl.get();sink.progress=progress;sink.checked=true;
        sink.file=openFile(partial,false);if(!sink.file)throw std::runtime_error("Nao foi possivel gravar "+partial.filename().string());
        curl_easy_setopt(curl.get(),CURLOPT_RANGE,nullptr);state.offset=0;code=curl_easy_perform(curl.get());
    }
    if(sink.file&&std::fclose(sink.file)!=0&&code==CURLE_OK)throw std::runtime_error("Falha ao gravar "+partial.filename().string());
    if(progress)progress->checkCancelled();
    if(sink.overflow){std::filesystem::remove(partial,error);throw std::runtime_error("Download maior que o esperado: "+partial.filename().string());}
    if(sink.failed)throw std::runtime_error("Resposta HTTP inesperada ou falha de escrita");
    if(code==CURLE_HTTP_RETURNED_ERROR){long status=0;curl_easy_getinfo(curl.get(),CURLINFO_RESPONSE_CODE,&status);if(status==416)std::filesystem::remove(partial,error);}
    if(code!=CURLE_OK)throw std::runtime_error(std::string("HTTP: ")+(message[0]?message.data():curl_easy_strerror(code)));
    return std::filesystem::file_size(partial);
}

} // namespace albert
