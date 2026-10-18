#pragma once
#include "net/download.hpp"
#include "physics/earth.hpp"
#include "physics/gravity.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
namespace albert::providers {
// PDS SHADR tables (header: radius km, GM km³/s², …; rows: n, m, C̄, S̄, σC, σS), truncated.
physics::GravityField parseShadr(const std::string& text,int degree,std::string name);
// A field and the table excerpt it came from (flight logs carry it, so replay needs no download).
struct GravityModel {std::shared_ptr<const physics::Gravity> gravity;std::string table;};
// GRAIL GRGM1200A (Moon, DE440 principal axes) or MRO GMM-3 (Mars) from a table excerpt.
GravityModel gravityModel(physics::Center body,std::string table,int degree);
int gravityMaximum(physics::Center body); // Highest degree offered: 600 (Moon), 120 (Mars).
std::size_t gravityTableBytes(int degree); // Longest excerpt accepted (fixed rows, CRLF allowed).
// The same fields from the PDS Geosciences Node, read only up to the requested degree and cached.
class GravityModels {
public:
    using Transport=std::function<std::string(const std::string& url,const std::string& range,DownloadProgress*)>;
    explicit GravityModels(std::filesystem::path cache,Transport transport={});
    GravityModel load(physics::Center body,int degree,DownloadProgress* progress=nullptr);
    std::shared_ptr<const physics::Gravity> moon(int degree,DownloadProgress* p=nullptr){return load(physics::Center::moon,degree,p).gravity;}
    std::shared_ptr<const physics::Gravity> mars(int degree,DownloadProgress* p=nullptr){return load(physics::Center::mars,degree,p).gravity;}
private:
    std::filesystem::path cache_;Transport transport_;
};
GravityModels& gravityModels();
}
