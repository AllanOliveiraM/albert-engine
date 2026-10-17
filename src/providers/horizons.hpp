#pragma once
#include "core/scene.hpp"
#include "download.hpp"
#include <functional>
#include <filesystem>
#include <memory>
namespace albert::providers {
struct Point {double jdTDB;core::Vector positionMeters,velocityMetersPerSecond;};
struct Query {int target=0;std::string center="500@399";double startUtc=0,endUtc=0;std::string step="5 min";bool bootstrapCache=false,forceRefresh=false;};
struct Table {std::vector<Point> points;double fetchedUtc=0;bool fromCache=false;std::string source="NASA/JPL Horizons";};
// Table interval for a simulated instant moving at `rate` simulated seconds per
// second: `playbackSeconds` of real time (at least `minimumSeconds`) ahead in the
// direction of travel, centred while paused, plus one step behind. The step is
// the finest of a fixed ladder (5 min to 5 d), not finer than `finestStepSeconds`,
// that keeps at most `maxSamples` rows; when even `coarsestStepSeconds` would
// exceed them, the span shrinks instead. Horizons time grows with the rows.
struct Playback {double minimumSeconds=86400.,playbackSeconds=600.;unsigned maxSamples=2000;double finestStepSeconds=300.,coarsestStepSeconds=86400.;};
struct Window {double startUtc=0,endUtc=0;std::string step;};
Window playbackWindow(double utc,double rate,const Playback& playback={});
// The instant left the window, or less than half of the planned coverage remains
// ahead of it. A paused instant inside the window never expires.
bool expiring(const Window& window,double utc,double rate,const Playback& playback={});
std::vector<Point> parseVectors(const std::string& response);
std::string horizonsURL(const Query& query);
class Horizons {
public:
    using Transport=std::function<std::string(const std::string&,DownloadProgress*)>;
    using Now=std::function<double()>;
    explicit Horizons(Transport transport={},Now utcNow={},Now monotonicNow={},std::filesystem::path cacheDirectory={});
    ~Horizons();
    Table query(Query query,DownloadProgress* progress=nullptr);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
Horizons& horizons();
}
