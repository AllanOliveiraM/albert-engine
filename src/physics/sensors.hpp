#pragma once
#include "physics/vector.hpp"
#include <array>
#include <cstdint>
namespace albert::physics {
// Gaussian noise identical on every platform: xoshiro256** and Box-Muller.
class Noise {
public:
    explicit Noise(std::uint64_t seed);
    std::uint64_t next();
    double uniform(){return static_cast<double>(next()>>11)*0x1.0p-53;} // [0, 1)
    double gaussian();
private:
    std::uint64_t s_[4];bool spare_=false;double cached_=0;
};
// Per-axis inertial error terms (IEEE 952), SI units. white: noise density (VRW m/s/√s,
// ARW rad/√s); instability: bias instability as first-order Gauss-Markov (σ ≈ 1.07 × the
// Allan floor) with correlation time; walk: rate random walk (unit/s/√s); bias: turn-on bias
// standard deviation, drawn once; scale: scale-factor error standard deviation.
struct Channel {double white=0,instability=0,correlation=100,walk=0,bias=0,scale=0;};
struct ImuModel {Channel accelerometer,gyroscope;double pressureNoise=0,pressureBias=0;}; // Pa.
enum class ImuGrade {ideal,tactical,industrial};
ImuModel imuModel(ImuGrade grade);
// Measures specific force, rate, their increments over dt and static pressure.
class Imu {
public:
    struct Reading {V3 specificForce,rate,deltaVelocity,deltaAngle;double pressure=0;};
    Imu(const ImuModel& model,std::uint64_t seed);
    Reading measure(double dt,V3 specificForce,V3 rate,V3 deltaVelocity,V3 deltaAngle,double pressure);
    const ImuModel& model()const{return model_;}
private:
    struct Axis {double bias=0,scale=0,markov=0,walk=0;};
    double error(const Channel& c,Axis& a,double dt,double& white);
    ImuModel model_;Noise noise_;std::array<Axis,6> axes_{};double pressureBias_=0;
};
}
