#include "store.hpp"
#include "net/sha256.hpp"
#include "platform.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif
namespace fs=std::filesystem;
namespace albert::providers {
std::optional<StoredResponse> loadResponse(const fs::path& directory,const std::string& identity,std::size_t maximum){try{
    if(directory.empty())return {};const auto path=directory/(sha256(identity)+".json");std::error_code ec;
    if(fs::is_symlink(path,ec)||ec)return {};const auto size=fs::file_size(path,ec);if(ec||size>maximum*2+8192)return {};
    std::ifstream file(path,std::ios::binary);std::string data(static_cast<std::size_t>(size), '\0');
    if(!file.read(data.data(),static_cast<std::streamsize>(data.size()))||file.peek()!=std::char_traits<char>::eof())return {};
    const auto record=nlohmann::json::parse(data);StoredResponse result{record.at("payload"),record.at("fetched_utc")};
    if(record.at("schema")!=1||record.at("identity")!=identity||result.payload.size()>maximum||!std::isfinite(result.fetchedUtc)||record.at("sha256")!=sha256(result.payload))return {};
    return result;
}catch(const std::exception&){return {};}}
void saveResponse(const fs::path& directory,const std::string& identity,const StoredResponse& value,std::size_t maximum){try{
    if(directory.empty()||value.payload.size()>maximum||!std::isfinite(value.fetchedUtc))return;fs::create_directories(directory);
    const auto target=directory/(sha256(identity)+".json");
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    const auto temporary=directory/(sha256(identity)+"."+std::to_string(pid)+".tmp");
    {std::ofstream file(temporary,std::ios::binary);file<<nlohmann::json{{"schema",1},{"identity",identity},{"fetched_utc",value.fetchedUtc},{"sha256",sha256(value.payload)},{"payload",value.payload}}.dump();file.close();if(!file){std::error_code ec;fs::remove(temporary,ec);return;}}
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code ec;fs::remove(temporary,ec);return;}
#else
    std::error_code renameError;fs::rename(temporary,target,renameError);if(renameError){fs::remove(temporary,renameError);return;}
#endif
    // Independent of the UI cache setting: a provider cannot grow without bound.
    struct Entry {fs::path path;fs::file_time_type used;uintmax_t bytes;};std::vector<Entry> files;uintmax_t bytes=0;std::error_code ec;
    for(fs::directory_iterator it(directory,ec),end;it!=end&&!ec;it.increment(ec))if(it->path().extension()==".json"&&!it->is_symlink(ec)&&it->is_regular_file(ec)){const auto size=it->file_size(ec);const auto time=it->last_write_time(ec);if(!ec){files.push_back({it->path(),time,size});bytes+=size;}}
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.used<b.used;});std::size_t count=files.size();
    for(const auto& file:files){if(count<=128&&bytes<=64*1024*1024)break;if(fs::remove(file.path,ec)){--count;bytes-=file.bytes;}}
}catch(const std::exception&){/* Cache availability must never invalidate real data. */}}
}
