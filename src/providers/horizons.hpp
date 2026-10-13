#pragma once
#include "core/scene.hpp"
#include "download.hpp"
#include <functional>
#include <memory>
namespace albert::providers {
struct Point {double jdTDB;core::Vector positionMeters,velocityMetersPerSecond;};
struct Query {int target=0;std::string center="500@399";double startUtc=0,endUtc=0;std::string step="5 min";};
struct Table {std::vector<Point> points;double fetchedUtc=0;bool fromCache=false;std::string source="NASA/JPL Horizons";};
std::vector<Point> parseVectors(const std::string& response);
std::string horizonsURL(const Query& query);
class Horizons {
public:
    using Transport=std::function<std::string(const std::string&,DownloadProgress*)>;
    using Now=std::function<double()>;
    explicit Horizons(Transport transport={},Now utcNow={},Now monotonicNow={});
    ~Horizons();
    Table query(Query query,DownloadProgress* progress=nullptr);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
Horizons& horizons();
}
