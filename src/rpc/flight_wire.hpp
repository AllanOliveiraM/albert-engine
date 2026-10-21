#pragma once
// Albert flight data plane: fixed-size little-endian frames over local TCP (127.0.0.1, the
// port in FlightSample.data_port). Each request applies its commands atomically, advances
// `ticks` steps and answers with one response, as FlightService.Step does.
#include <bit>
#include <cstdint>
namespace albert::wire {
static_assert(std::endian::native==std::endian::little,"Frames are little-endian");
enum Command:std::uint32_t {ignite=1,cutoff=2,separate=4,release=8,throttle=16,gimbal=32,torque=64,fail=128}; // fail: the selected engine fails (needs one).
enum Extra:std::uint32_t {thruster=256,surface=512,spawn=1024,brake=2048,steer=4096,swing=8192}; // gRPC only (thruster_duty, surface_radians, spawn, brake, steer_radians,
// structure_radians); kinds in the replay log.
enum Status:std::uint32_t {ok=0,invalid=1,precondition=2,noFlight=3};
enum Event:std::uint32_t {liftoff=1,depleted=2,touchdown=4,overheat=8,breakup=16}; // overheat: a thermal node above its limit; breakup: the vehicle broke up.
enum Flag:std::uint32_t {burning=1,clamped=2,landed=4,contact=8}; // contact: a leg or wheel on the ground, the hull in the water or a hard point on a structure.
struct Request {
    char magic[4]{'A','L','B','Q'};std::uint32_t ticks=1,commands=0,engine=0; // engine: 0 every engine of the stage, n its n-th (ignite, cutoff, throttle, gimbal, fail).
    double throttleValue=1,gimbalPitch=0,gimbalYaw=0,torqueValue[3]{};
};
struct Response {
    char magic[4]{'A','L','B','R'};std::uint32_t status=0;std::uint64_t tick=0;double elapsed=0,utc=0;
    double specificForce[3],rate[3],deltaVelocity[3],deltaAngle[3],pressure;  // Measured, body axes.
    double position[3],velocity[3],attitude[4],geodetic[3],nedVelocity[3];   // GCRF; quaternion x,y,z,w; degrees, m.
    double attitudeNed[4];                                                    // Body to NED, x,y,z,w.
    double mach,dynamicPressure,alpha,beta,mass,propellant,thrust;
    std::uint32_t stage,flags,events,reserved;std::uint64_t hash;
};
static_assert(sizeof(Request)==64&&sizeof(Response)==376,"Frame layout");
}
