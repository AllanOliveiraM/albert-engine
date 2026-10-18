// External flight computer: drives Albert's generic launcher from the pad to orbit in
// lockstep, reading only the simulated sensors and truth the flight API reports.
// Usage: albert_flight_client [address] [--fast] [--wire] [--utc=UNIX_SECONDS]
// Defaults: 127.0.0.1:50051, real time, the instant shown by Albert. --wire flies over the
// binary data plane at 1 kHz (one exchange per tick) and then coasts to 10 minutes.
#include "albert/v1/engine.grpc.pb.h"
#include "gravity_turn.hpp"
#include "wire.hpp"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
namespace api=albert::v1;namespace ex=albert::example;
void check(const grpc::Status& s){if(!s.ok())throw std::runtime_error(s.error_message());}
ex::Vec vec(const api::Vector3& v){return{v.x(),v.y(),v.z()};}
ex::Reading reading(const api::FlightSample& s){
    ex::Reading r;r.elapsed=s.elapsed_seconds();r.height=s.geodetic().height_meters();r.nedVelocity=vec(s.velocity_ned_meters_per_second());
    const auto& q=s.attitude_ned();r.qw=q.w();r.qx=q.x();r.qy=q.y();r.qz=q.z();
    r.rate=vec(s.angular_rate_radians_per_second());r.specificForce=vec(s.specific_force_meters_per_second2());
    r.position=vec(s.position_meters());r.velocity=vec(s.velocity_meters_per_second());
    r.stage=static_cast<int>(s.stage());r.burning=s.burning();r.clamped=s.clamped();r.propellant=s.propellant_kilograms();return r;
}
int main(int argc,char** argv){try{
    std::setvbuf(stdout,nullptr,_IOLBF,0);std::string address="127.0.0.1:50051";bool fast=false,binary=false;double utc=0;bool chosen=false;
    for(int i=1;i<argc;++i){const std::string a=argv[i];if(a=="--fast")fast=true;else if(a=="--wire")binary=true;else if(a.rfind("--utc=",0)==0){utc=std::stod(a.substr(6));chosen=true;}else address=a;}
    auto channel=grpc::CreateChannel(address,grpc::InsecureChannelCredentials());
    if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(30)))throw std::runtime_error("Abra AlbertEngine --rpc antes de executar o exemplo");
    auto flight=api::FlightService::NewStub(channel);
    api::CreateFlightRequest create;create.set_vehicle(api::GENERIC_LAUNCHER);create.set_heading_degrees(90);create.set_focus_camera(true);if(chosen)create.set_utc_unix_seconds(utc);
    auto* site=create.mutable_site();site->set_latitude_degrees(28.6082);site->set_longitude_degrees(-80.6041);create.set_terrain_height(true); // Kennedy LC-39A, on the measured ground.
    api::FlightSample s;{grpc::ClientContext c;check(flight->CreateFlight(&c,create,&s));}
    std::printf("%s, passo %.0f ms, %s\n",s.vehicle().c_str(),s.step_seconds()*1e3,fast?"o mais rápido possível":"tempo real");
    constexpr double pi=3.14159265358979323846;constexpr std::uint32_t ticks=20; // Guidance at 50 Hz.
    if(binary){ // 1 kHz lockstep: one tick per frame, guidance every 20 frames, then a coast to 600 s.
        albert::wire::Connection link(s.data_port());ex::GravityTurn g(pi/2);albert::wire::Request q;q.ticks=1;albert::wire::Response r{};
        ex::Reading rd=reading(s);const auto t0=std::chrono::steady_clock::now();std::uint64_t frames=0,lost=0,last=s.tick();bool orbit=false;
        while(last<600000){
            q.commands=0;
            if(frames%20==0&&!orbit){const ex::Commands c=g.step(rd);
                q.commands=albert::wire::gimbal|albert::wire::torque|(c.ignite?albert::wire::ignite:0u)|(c.separate?albert::wire::separate:0u)|(c.cutoff?albert::wire::cutoff:0u);
                q.gimbalPitch=c.gimbalPitch;q.gimbalYaw=c.gimbalYaw;q.torqueValue[0]=c.torque.x;q.torqueValue[1]=c.torque.y;q.torqueValue[2]=c.torque.z;
                if(g.phase()==ex::GravityTurn::Phase::orbit){orbit=true;q.torqueValue[0]=q.torqueValue[1]=q.torqueValue[2]=0;}}
            r=link.exchange(q);++frames;if(r.status!=albert::wire::ok||r.tick!=last+1)++lost;last=r.tick;
            rd.elapsed=r.elapsed;rd.height=r.geodetic[2];rd.nedVelocity={r.nedVelocity[0],r.nedVelocity[1],r.nedVelocity[2]};rd.qx=r.attitudeNed[0];rd.qy=r.attitudeNed[1];rd.qz=r.attitudeNed[2];rd.qw=r.attitudeNed[3];
            rd.rate={r.rate[0],r.rate[1],r.rate[2]};rd.specificForce={r.specificForce[0],r.specificForce[1],r.specificForce[2]};rd.position={r.position[0],r.position[1],r.position[2]};
            rd.velocity={r.velocity[0],r.velocity[1],r.velocity[2]};rd.stage=static_cast<int>(r.stage);rd.burning=r.flags&albert::wire::burning;rd.clamped=r.flags&albert::wire::clamped;rd.propellant=r.propellant;
            if(r.events)std::printf("%7.2f s  eventos 0x%x\n",r.elapsed,r.events);
        }
        const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
        std::printf("%llu quadros a 1 kHz em %.1f s (%.1f us cada, %.0fx tempo real), %llu perdidos; perigeu %.1f km\n",static_cast<unsigned long long>(frames),wall,wall/frames*1e6,600/wall,
            static_cast<unsigned long long>(lost),ex::GravityTurn::perigee(rd.position,rd.velocity)/1e3);
        return lost==0&&orbit?0:1;
    }
    ex::GravityTurn guidance(pi/2);auto phase=guidance.phase();const auto wall=std::chrono::steady_clock::now();double report=0;
    const char* names[]{"pad","subida vertical","pitch kick","gravity turn","MECO","separação","segundo estágio","órbita"};
    for(;;){
        const ex::Commands c=guidance.step(reading(s));
        api::StepRequest step;step.set_ticks(ticks);
        if(c.ignite)step.add_commands()->set_ignite(true);
        if(c.separate)step.add_commands()->set_separate(true);
        if(c.cutoff)step.add_commands()->set_cutoff(true);
        auto* g=step.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw);
        auto* t=step.add_commands()->mutable_torque_newton_meters();t->set_x(c.torque.x);t->set_y(c.torque.y);t->set_z(c.torque.z);
        {grpc::ClientContext ctx;check(flight->Step(&ctx,step,&s));}
        for(const auto& e:s.events())std::printf("%7.2f s  evento %s (estágio %u)\n",e.elapsed_seconds(),api::FlightEvent::Kind_Name(e.kind()).c_str(),e.stage()+1);
        const double h=s.geodetic().height_meters(),v=std::hypot(s.velocity_ned_meters_per_second().x(),s.velocity_ned_meters_per_second().y(),s.velocity_ned_meters_per_second().z());
        if(guidance.phase()!=phase||s.elapsed_seconds()>=report){
            phase=guidance.phase();report=std::floor(s.elapsed_seconds()/10)*10+10;
            std::printf("%7.2f s  %-17s h %7.1f km  v solo %6.0f m/s  q %6.0f Pa  Mach %5.2f  %6.0f kg  perigeu %7.1f km\n",s.elapsed_seconds(),names[static_cast<int>(phase)],h/1e3,v,
                s.dynamic_pressure_pascals(),s.mach(),s.mass_kilograms(),ex::GravityTurn::perigee(vec(s.position_meters()),vec(s.velocity_meters_per_second()))/1e3);
        }
        if(guidance.phase()==ex::GravityTurn::Phase::orbit){
            std::printf("Órbita: perigeu %.1f km, %.0f kg de propelante restante\n",ex::GravityTurn::perigee(vec(s.position_meters()),vec(s.velocity_meters_per_second()))/1e3,s.propellant_kilograms());
            // One revolution in nine calls: ballistic above the atmosphere, Albert takes 1 s steps.
            const auto t0=std::chrono::steady_clock::now();
            for(int i=0;i<9;++i){api::StepRequest coast;coast.set_ticks(600000);auto* q=coast.add_commands()->mutable_torque_newton_meters();q->set_x(0);q->set_y(0);q->set_z(0);
                grpc::ClientContext ctx;check(flight->Step(&ctx,coast,&s));}
            std::printf("Uma volta (90 min) calculada em %.2f s: altura %.1f km, perigeu %.1f km\n",std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count(),
                s.geodetic().height_meters()/1e3,ex::GravityTurn::perigee(vec(s.position_meters()),vec(s.velocity_meters_per_second()))/1e3);
            return 0;
        }
        if(s.landed())throw std::runtime_error("O veículo voltou ao solo");
        if(s.elapsed_seconds()>900)throw std::runtime_error("Órbita não alcançada em 15 minutos");
        if(!fast)std::this_thread::sleep_until(wall+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(s.elapsed_seconds())));
    }
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
