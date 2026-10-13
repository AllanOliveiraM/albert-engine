#pragma once
#include "download.hpp"
#include <functional>
#include <filesystem>
#include <mutex>
#include <map>
#include <string>
#include <vector>
namespace albert::providers {
std::timed_mutex& jplRequestGate();
struct SmallBody {std::string designation,name;int spkId=0;double radiusMeters=0;};
class SmallBodies {
public:
    using Transport=std::function<std::string(const std::string&,DownloadProgress*)>;
    explicit SmallBodies(Transport transport={},std::filesystem::path cacheDirectory={});
    std::vector<SmallBody> search(const std::string&,DownloadProgress* progress=nullptr);
    SmallBody resolve(const std::string&,DownloadProgress* progress=nullptr);
private:
    struct Cached {double fetched;std::vector<SmallBody> values;};
    Transport transport_;std::filesystem::path directory_;std::map<std::string,Cached> cache_;
};
std::vector<SmallBody> parseSmallBodies(const std::string& response);
std::string smallBodyCatalogId(const std::string& designation);
std::string smallBodyDesignation(const std::string& catalogId);
}
