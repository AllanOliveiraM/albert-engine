#pragma once
#include <filesystem>
#include <optional>
#include <string>
namespace albert::providers {
struct StoredResponse {std::string payload;double fetchedUtc;};
std::optional<StoredResponse> loadResponse(const std::filesystem::path&,const std::string& identity,std::size_t maximumBytes);
void saveResponse(const std::filesystem::path&,const std::string& identity,const StoredResponse&,std::size_t maximumBytes);
}
