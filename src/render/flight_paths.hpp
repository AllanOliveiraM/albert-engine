#pragma once
#include "core/scene.hpp"

namespace albert {
struct TrajectoryPoint {double utc;core::Vector position,velocity;};
// Earth-centred ICRF, metres. Prediction is gravity-only from the measured
// current state; it deliberately makes no assumption about future engine burns.
std::vector<TrajectoryPoint> predictTrajectory(TrajectoryPoint initial,double seconds=6000,double step=10);
std::vector<TrajectoryPoint> starshipTargetOrbit(TrajectoryPoint launch);
class FlightHistory {
public:
    void update(const core::Object& flight,double now);
    void clear(){instance_=0;points_.clear();}
    const std::vector<TrajectoryPoint>& points()const{return points_;}
private:
    std::uint64_t instance_=0;std::vector<TrajectoryPoint> points_;
};
}
