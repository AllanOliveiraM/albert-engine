#include "cache.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <vector>

namespace fs=std::filesystem;
namespace {
struct Entry { fs::path path; fs::file_time_type used; uintmax_t bytes; };
std::vector<Entry> entries(const fs::path& directory) {
    std::vector<Entry> result;
    for(const char* name:{"assets","clouds"}) {
        const auto root=directory/name;
        std::error_code ec;
        if(!fs::is_directory(root,ec) || fs::is_symlink(root,ec)) continue;
        for(fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)) {
            if(ec) { ec.clear();continue; }
            if(!it->is_regular_file(ec) || it->is_symlink(ec)) continue;
            const auto size=it->file_size(ec);
            const auto time=it->last_write_time(ec);
            if(!ec) result.push_back({it->path(),time,size});
        }
    }
    return result;
}
}
albert::Cache::Cache(fs::path directory):directory_(std::move(directory)) {
    try {
        std::ifstream file(directory_/"cache-settings.json");
        if(file) limitMB=std::clamp(nlohmann::json::parse(file).value("limit_mb",2048),128,32768);
    } catch(const std::exception&) { /* A damaged preference never prevents startup. */ }
    refresh();
}
void albert::Cache::refresh() {
    error.clear();
    auto files=entries(directory_);
    bytes=0;for(const auto& file:files) bytes+=file.bytes;
    std::sort(files.begin(),files.end(),[](const Entry& a,const Entry& b){return a.used<b.used;});
    const auto limit=static_cast<uintmax_t>(limitMB)*1024*1024;
    for(const auto& file:files) {
        if(bytes<=limit) break;
        if(file.path.extension()==".tmp") continue; // In-flight transfers are never evicted.
        std::error_code ec;
        if(fs::remove(file.path,ec)) bytes-=file.bytes;
        if(ec) error="Alguns arquivos em uso serao removidos depois.";
        if(file.path.extension()==".png") {
            // A partial observation set is no longer a committed cloud cache.
            fs::remove(file.path.parent_path()/"date.txt",ec);
        }
    }
}
void albert::Cache::setLimit(int megabytes) {
    limitMB=std::clamp(megabytes,128,32768);
    try {
        fs::create_directories(directory_);
        std::ofstream file(directory_/"cache-settings.json");
        file<<nlohmann::json{{"limit_mb",limitMB}}.dump(2)<<'\n';
        if(!file) throw std::runtime_error("Nao foi possivel salvar o limite do cache");
        refresh();
    } catch(const std::exception& e) {error=e.what();}
}
void albert::Cache::clear() {
    error.clear();
    for(const char* name:{"assets","clouds"}) {
        std::error_code ec;
        const auto root=directory_/name;
        if(!fs::is_symlink(root,ec)) fs::remove_all(root,ec);
        if(ec) error="Alguns arquivos do cache nao puderam ser apagados.";
    }
    const auto removalError=error;
    refresh();
    if(!removalError.empty()) error=removalError;
}
