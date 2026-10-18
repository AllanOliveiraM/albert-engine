#pragma once
#include "net/download.hpp"
#include "physics/gravity.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
namespace albert::providers {
// PDS SHADR tables (header: radius km, GM km³/s², …; rows: n, m, C̄, S̄, σC, σS), truncated.
physics::GravityField parseShadr(const std::string& text,int degree,std::string name);
// GRAIL GRGM1200A (Moon, DE430 principal axes) and MRO GMM-3 (Mars, Konopliv 2006 frame)
// from the PDS Geosciences Node, read only up to the requested degree and cached.
class GravityModels {
public:
    using Transport=std::function<std::string(const std::string& url,const std::string& range,DownloadProgress*)>;
    explicit GravityModels(std::filesystem::path cache,Transport transport={});
    std::shared_ptr<const physics::Gravity> moon(int degree,DownloadProgress* progress=nullptr);
    std::shared_ptr<const physics::Gravity> mars(int degree,DownloadProgress* progress=nullptr);
private:
    std::shared_ptr<const physics::Gravity> load(const std::string& url,const std::string& name,int degree,int maximum,DownloadProgress* progress);
    std::filesystem::path cache_;Transport transport_;
};
GravityModels& gravityModels();
}
