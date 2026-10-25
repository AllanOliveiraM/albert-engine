#pragma once
// What the example flight computers share: the flight API's status as an exception, its vectors and samples in the
// guidance's types.
#include "albert/v1/engine.grpc.pb.h"
#include "gravity_turn.hpp"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
namespace albert::example {
inline double paceSim=0,paceSlept=0; // ALBERT_PACE_LOG: the pacer's last simulated time (s) and its sleep since the last RPC (ms).
inline void check(const grpc::Status& s){if(!s.ok())throw std::runtime_error(s.error_message());}
// An RPC until the server takes it: a busy server (UNAVAILABLE, RESOURCE_EXHAUSTED) refuses before applying anything, so it
// is sent again after 50 ms to 0.8 s, for up to ~15 s; anything else throws.
template<class Rpc> void call(Rpc&& rpc){
    static const bool log=std::getenv("ALBERT_PACE_LOG")!=nullptr;static auto back=std::chrono::steady_clock::now();const auto sent=std::chrono::steady_clock::now();
    struct Done{std::chrono::steady_clock::time_point sent;std::chrono::steady_clock::time_point& back;~Done(){if(!log)return;const auto now=std::chrono::steady_clock::now();
        const double rpcMs=std::chrono::duration<double,std::milli>(now-sent).count(),clientMs=std::chrono::duration<double,std::milli>(sent-back).count();
        if(rpcMs>100||clientMs-paceSlept>100)std::fprintf(stderr,"PACE rpc %.0f ms, client %.0f ms before it (%.0f asleep) at sim %.2f s\n",rpcMs,clientMs,paceSlept,paceSim);paceSlept=0;back=now;}} done{sent,back};
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
