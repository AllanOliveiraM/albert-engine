#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
namespace albert {
class Cache {
public:
    explicit Cache(std::filesystem::path directory);
    void refresh();
    void setLimit(int megabytes);
    void clear();
    int limitMB = 2048;
    uintmax_t bytes = 0;
    std::string error;
private:
    std::filesystem::path directory_;
};
}
