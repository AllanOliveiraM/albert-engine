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
enum class ImuGrade {ideal,tactical,industrial,navigation}; // navigation: a launch vehicle's (ring laser or fibre gyros).
ImuModel imuModel(ImuGrade grade);
// Measures specific force, rate, their increments over dt and static pressure.
// GNSS receiver: position and velocity with white noise (σ per axis in the local horizontal, vertical, and on velocity) and a
// first-order Gauss-Markov position drift (σ, correlation time: ionosphere, orbits, clocks), one fix a period while below
// the height and speed limits (0: none; the export rules' 18 km and 515 m/s make a civil receiver; the defaults, a space
// receiver to 3000 km). Earth only.
// RTK: within rtkRange (m; 0: none) of a base station (ECEF), the corrections it sends cancel the drift and most of the noise:
// σ rtkSigma + rtkPpm × range on each axis, rtkVelocity on velocity; the fix lost with probability rtkDropout + (range /
// rtkRange)² (the link fading out to its edge), the plain fix then. blackout: the heat flux (W/m²) above which the plasma
// sheath stops every fix (0: never).
struct GnssModel {double horizontal=1.5,vertical=3,velocity=.05,drift=1,correlation=300,period=.1,maxHeight=3e6,maxSpeed=0,
    rtkRange=0,rtkSigma=.02,rtkPpm=1e-6,rtkVelocity=.02,rtkDropout=.02,blackout=0;V3 base;};
class Gnss {
public:
    struct Reading {bool valid=false,rtk=false;double time=0;V3 position,velocity;}; // ECEF (m, m/s), at `time` (s since the start).
    Gnss(const GnssModel& model,std::uint64_t seed);
    // ned: rows north, east and down at the point (nedFromFixed); a fix when available, else invalid.
    Reading measure(double time,double dt,V3 position,V3 velocity,const M3& ned,double height,double speed,bool available,double heatFlux=0);
    const GnssModel& model()const{return model_;}
private:
    GnssModel model_;Noise noise_;V3 drift_;
};
// Radar altimeter: slant range along a body boresight to flat ground at the site's height, within the beam's half-angle of
// the vertical and the range limit; noise σ = max(floor, fraction × range).
struct RadarModel {V3 boresight{0,0,1};double halfAngle=.35,maxRange=2500,floor=.3,fraction=.01,period=.05;};
class RadarAltimeter {
public:
    struct Reading {bool valid=false;double time=0,range=0;};
    RadarAltimeter(const RadarModel& model,std::uint64_t seed);
    Reading measure(double time,double height,V3 boresightNed); // Height over the ground; the boresight in NED (unit).
    const RadarModel& model()const{return model_;}
private:
    RadarModel model_;Noise noise_;
};
// Star tracker: the body's attitude (body to inertial) with white noise about the boresight's cross axes and about it (roll);
// none while the boresight is within the exclusion angle of the Sun, within the central body's disc plus a margin, or the
// body turns faster than the limit.
struct StarTrackerModel {V3 boresight{0,0,-1};double cross=2.4e-5,roll=2e-4,sunExclusion=.52,bodyMargin=.17,maxRate=.035,period=.1;}; // 5″, 40″, 30°, 10°, 2°/s.
class StarTracker {
public:
    struct Reading {bool valid=false;double time=0;Q attitude;};
    StarTracker(const StarTrackerModel& model,std::uint64_t seed);
    // sun, body: inertial unit directions from the vehicle; bodyRadius: the body's angular radius (rad); rate: body rad/s.
    Reading measure(double time,Q attitude,V3 sun,V3 body,double bodyRadius,V3 rate);
    const StarTrackerModel& model()const{return model_;}
private:
    StarTrackerModel model_;Noise noise_;
};
// Propellant gauges (the tanks' level sensors): what is left with white noise σ = fraction × it + floor (kg) and a bias of
// biasFraction × it drawn once, a reading a period.
struct GaugeModel {double fraction=.005,floor=300,biasFraction=.003,period=.1;};
class PropellantGauge {
public:
    struct Reading {bool valid=false;double time=0,propellant=0;};
    PropellantGauge(const GaugeModel& model,std::uint64_t seed);
    Reading measure(double time,double propellant);
    const GaugeModel& model()const{return model_;}
private:
    GaugeModel model_;Noise noise_;double bias_=0;
};
// Anemometer (a cup or sonic one on the tower's crown): the air's horizontal velocity there with white noise σ a horizontal
// axis (m/s), a reading a period; sent to the vehicles over the site's link.
struct AnemometerModel {double sigma=.3,period=1;};
class Anemometer {
public:
    struct Reading {bool valid=false;double time=0;V3 wind;}; // NED (m/s), down nought.
    Anemometer(const AnemometerModel& model,std::uint64_t seed);
    Reading measure(double time,V3 windNed);
    const AnemometerModel& model()const{return model_;}
private:
    AnemometerModel model_;Noise noise_;
};
// Radio beacon on the tower (a ranging transponder with an interferometer): the vehicle's range from it (σ range + rangePpm ×
// it) and its azimuth (from north, toward east) and elevation as the beacon sees them (σ angle), a reading a period within
// maxRange; the reply lost with probability dropout. Independent of the satellites: the last metres held when GNSS is not.
struct BeaconModel {double range=.05,rangePpm=1e-5,angle=5e-4,maxRange=2e4,dropout=.01,period=.1;};
class Beacon {
public:
    struct Reading {bool valid=false;double time=0,range=0,azimuth=0,elevation=0;}; // m, rad.
    Beacon(const BeaconModel& model,std::uint64_t seed);
    Reading measure(double time,V3 fromBeaconNed); // The vehicle from the beacon, in the beacon's NED (m).
    const BeaconModel& model()const{return model_;}
private:
    BeaconModel model_;Noise noise_;
};
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
