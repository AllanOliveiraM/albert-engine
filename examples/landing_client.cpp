// External flight computer on the Moon: the generic lander lifts off its legs at the Apollo 11
// site, climbs to 150 m, hovers and lands softly, reading only the flight API's sensors.
// Usage: albert_landing_client [address] [--fast]   (default 127.0.0.1:50051, real time)
#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
namespace api=albert::v1;
void check(const grpc::Status& s){if(!s.ok())throw std::runtime_error(s.error_message());}
int main(int argc,char** argv){try{
    std::setvbuf(stdout,nullptr,_IOLBF,0);std::string address="127.0.0.1:50051";bool fast=false;
    for(int i=1;i<argc;++i){const std::string a=argv[i];if(a=="--fast")fast=true;else address=a;}
    auto channel=grpc::CreateChannel(address,grpc::InsecureChannelCredentials());
    if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(30)))throw std::runtime_error("Abra AlbertEngine --rpc antes de executar o exemplo");
    auto flight=api::FlightService::NewStub(channel);
    api::CreateFlightRequest create;create.set_vehicle(api::GENERIC_LANDER);create.set_body(api::CreateFlightRequest::MOON);create.set_focus_camera(true);create.set_terrain_height(true);
    create.mutable_site()->set_latitude_degrees(.67416);create.mutable_site()->set_longitude_degrees(23.47314); // Tranquility Base, on the LOLA ground.
    api::FlightSample s;{grpc::ClientContext c;check(flight->CreateFlight(&c,create,&s));}
    const double ground=s.geodetic().height_meters(),g=s.specific_force_meters_per_second2().x(),thrust=45040; // The accelerometer reads g on the pad.
    std::printf("%s na Lua, CG a %.0f m (LOLA), g %.4f m/s²\n",s.vehicle().c_str(),ground,g);
    enum class Phase {climb,hover,descent,landed} phase=Phase::climb;const char* names[]{"subida","pairando","descida","pousado"};
    double hoverStart=0,report=0;bool ignited=false;
    while(phase!=Phase::landed){
        const double altitude=s.geodetic().height_meters()-ground,up=-s.velocity_ned_meters_per_second().z(),t=s.elapsed_seconds();
        if(phase==Phase::climb&&altitude>=149){phase=Phase::hover;hoverStart=t;}
        if(phase==Phase::hover&&t-hoverStart>=10)phase=Phase::descent;
        // Vertical speed (m/s): the climb brakes at ~1 m/s² (10% throttle cannot do better here), the descent eases to 1 m/s.
        const double target=phase==Phase::climb?std::min(15.,std::sqrt(2*std::max(0.,150-altitude))):phase==Phase::hover?(150-altitude)*.5:-std::clamp(altitude*.1,1.,15.);
        const double throttle=std::clamp(s.mass_kilograms()*(g+(target-up))/thrust,.1,1.);
        api::StepRequest step;step.set_ticks(20); // Guidance at 50 Hz.
        if(!ignited){step.add_commands()->set_release(true);step.add_commands()->set_ignite(true);ignited=true;}
        step.add_commands()->set_throttle(throttle);
        const auto& w=s.angular_rate_radians_per_second();auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(-2e4*w.x());q->set_y(-2e4*w.y());q->set_z(-2e4*w.z()); // Rate damping.
        const auto wall=std::chrono::steady_clock::now();
        {grpc::ClientContext c;check(flight->Step(&c,step,&s));}
        for(const auto& e:s.events())
            if(e.kind()==api::FlightEvent::TOUCHDOWN&&phase==Phase::descent){
                std::printf("%7.2f s  pouso a %.2f m/s, %.0f kg de propelante\n",e.elapsed_seconds(),std::abs(s.velocity_ned_meters_per_second().z()),s.propellant_kilograms());
                api::StepRequest stop;stop.set_ticks(2000);stop.add_commands()->set_cutoff(true);grpc::ClientContext c;check(flight->Step(&c,stop,&s));phase=Phase::landed;
            }
        if(t>=report||phase==Phase::landed){report=std::floor(t/5)*5+5;
            std::printf("%7.2f s  %-9s altura %6.1f m  vertical %6.2f m/s  acelerador %3.0f%%  pernas %s\n",s.elapsed_seconds(),names[static_cast<int>(phase)],s.geodetic().height_meters()-ground,
                -s.velocity_ned_meters_per_second().z(),throttle*100,s.leg_contact()?"no solo":"no ar");}
        if(!fast)std::this_thread::sleep_until(wall+std::chrono::milliseconds(20));
        if(s.propellant_kilograms()<=0)throw std::runtime_error("Propelente esgotado antes do pouso");
    }
    return s.leg_contact()?0:1;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
