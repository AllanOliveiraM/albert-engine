#pragma once
// What the example flight computers share: the flight API's status as an exception, its vectors and samples in the
// guidance's types.
#include "albert/v1/engine.grpc.pb.h"
#include "gravity_turn.hpp"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <stdexcept>
#include <thread>
namespace albert::example {
inline void check(const grpc::Status& s){if(!s.ok())throw std::runtime_error(s.error_message());}
// An RPC until the server takes it: a busy server (UNAVAILABLE, RESOURCE_EXHAUSTED) refuses before applying anything, so it
// is sent again after 50 ms to 0.8 s, for up to ~15 s; anything else throws.
template<class Rpc> void call(Rpc&& rpc){
    for(int i=0;;++i){grpc::ClientContext c;const grpc::Status s=rpc(c);if(s.ok())return;
        const bool busy=s.error_code()==grpc::StatusCode::UNAVAILABLE||s.error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED;
        if(!busy||i>=24)throw std::runtime_error(s.error_message());std::this_thread::sleep_for(std::chrono::milliseconds(50<<std::min(i,4)));}
}
inline Vec vec(const ::albert::v1::Vector3& v){return{v.x(),v.y(),v.z()};}
inline Reading reading(const ::albert::v1::FlightSample& s){
    Reading r;r.elapsed=s.elapsed_seconds();r.height=s.geodetic().height_meters();r.nedVelocity=vec(s.velocity_ned_meters_per_second());
    const auto& q=s.attitude_ned();r.qw=q.w();r.qx=q.x();r.qy=q.y();r.qz=q.z();
    const auto& a=s.attitude();r.iw=a.w();r.ix=a.x();r.iy=a.y();r.iz=a.z();r.mass=s.mass_kilograms();
    r.fixedPosition=vec(s.position_ecef_meters());r.fixedVelocity=vec(s.velocity_ecef_meters_per_second());r.contact=s.leg_contact();
    r.rate=vec(s.angular_rate_radians_per_second());r.specificForce=vec(s.specific_force_meters_per_second2());
    r.position=vec(s.position_meters());r.velocity=vec(s.velocity_meters_per_second());
    r.stage=static_cast<int>(s.stage());r.stages=static_cast<int>(s.stages());r.burning=s.burning();r.clamped=s.clamped();r.propellant=s.propellant_kilograms();return r;
}
}
