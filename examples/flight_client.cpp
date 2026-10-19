// External flight computer: drives a vehicle from the pad to orbit in lockstep, reading only
// the simulated sensors and truth the flight API reports. Earth: the generic launcher from
// Kennedy LC-39A; Moon: the lunar ascent stage from Tranquility Base; Mars: the Mars ascent
// vehicle from Jezero crater (two burns, circularizing at a 250 km apoapsis).
// Usage: albert_flight_client [address] [--fast] [--wire] [--utc=UNIX_SECONDS] [--body=moon|mars] [--apollo11]
//        [--export-vehicle=launcher|saturn-v|lander|lunar-ascent|mars-ascent] [--definition=FILE.json] [--starship [--follow-booster]]
// Defaults: 127.0.0.1:50051, real time, the instant shown by Albert, the Earth. --wire (Earth)
// flies over the binary data plane at 1 kHz (one exchange per tick) and then coasts to 10 minutes.
// --apollo11: the Saturn V of Apollo 11 (AS-506) from LC-39A on 1969-07-16 by its flight program (saturn_v.hpp).
// --export-vehicle prints a preset as a vehicle definition in JSON (to edit); --definition flies such a file from the Earth's
// pad with the launcher's guidance (two stages, as the generic launcher).
// --starship: a Starship-class flight from Starbase, the ship to orbit and the booster back to a catch on the tower's arms
// (starship_flight.hpp); --follow-booster keeps the camera on the booster from staging.
#include "albert/v1/engine.grpc.pb.h"
#include "gravity_turn.hpp"
#include "saturn_v.hpp"
#include "flight_api.hpp"
#include "starship_flight.hpp"
#include "wire.hpp"
#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/json_util.h>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
namespace api=albert::v1;namespace ex=albert::example;
using ex::check;using ex::vec;using ex::reading;
// Apollo 11's Saturn V from Launch Complex 39A at 1969-07-16 13:32:00 UTC to the parking orbit, by its flight program.
int apollo11(api::FlightService::Stub& flight,bool fast){
    api::CreateFlightRequest create;create.set_vehicle(api::SATURN_V);create.set_focus_camera(true);create.set_heading_degrees(90);
    create.set_utc_unix_seconds(-14552880.+ex::SaturnV::release);create.mutable_site()->set_latitude_degrees(28.6084);create.mutable_site()->set_longitude_degrees(-80.6041);
    api::FlightSample s;{grpc::ClientContext c;check(flight.CreateFlight(&c,create,&s));}
    std::printf("%s, ar: %s, %s\n",s.vehicle().c_str(),s.atmosphere().c_str(),fast?"o mais rápido possível":"tempo real");
    ex::SaturnV g;const auto wall=std::chrono::steady_clock::now();double report=0;
    auto speed=[&]{const auto& v=s.velocity_meters_per_second();return std::hypot(v.x(),v.y(),v.z());};
    while(g.phase()!=ex::SaturnV::Phase::orbit){
        const ex::Commands c=g.step(reading(s));api::StepRequest step;step.set_ticks(20); // Guidance at 50 Hz.
        if(c.separate)step.add_commands()->set_separate(true);
        if(c.ignite)step.add_commands()->set_ignite(true);
        if(c.cutoff)step.add_commands()->set_cutoff(true);
        if(c.throttle>=0)step.add_commands()->set_throttle(c.throttle);
        auto* t=step.add_commands()->mutable_torque_newton_meters();t->set_x(c.torque.x);t->set_y(c.torque.y);t->set_z(c.torque.z);
        {grpc::ClientContext ctx;check(flight.Step(&ctx,step,&s));}
        const double range=s.elapsed_seconds()+ex::SaturnV::release;
        for(const auto& e:s.events())std::printf("%7.2f s  evento %s (estágio %u)\n",range,api::FlightEvent::Kind_Name(e.kind()).c_str(),e.stage()+1);
        if(range>=report){report=std::floor(range/20)*20+20;
            std::printf("%7.2f s  estágio %u  h %6.1f km  v inercial %6.0f m/s  q %6.0f Pa  %8.0f kg\n",range,s.stage()+1,s.geodetic().height_meters()/1e3,speed(),s.dynamic_pressure_pascals(),s.mass_kilograms());}
        if(s.landed())throw std::runtime_error("O veículo voltou ao solo");
        if(range>800)throw std::runtime_error("Órbita não alcançada em 800 s");
        if(!fast)std::this_thread::sleep_until(wall+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(s.elapsed_seconds())));
    }
    const ex::Vec rp=vec(s.position_meters()),vp=vec(s.velocity_meters_per_second());const double a=ex::SaturnV::semiMajor(rp,vp),apo=ex::GravityTurn::apoapsis(rp,vp)+6378137.,peri=ex::GravityTurn::perigee(rp,vp)+6378137.;
    std::printf("Corte do S-IVB a %.2f s (Apollo 11: 699,34 s): órbita %.1f x %.1f km sobre a esfera de 6378,165 km (Apollo 11: 186,0 x 183,2), a %.1f km, %.0f kg\n",
        s.elapsed_seconds()+ex::SaturnV::release,(apo-6378165)/1e3,(peri-6378165)/1e3,a/1e3,s.mass_kilograms());
    return 0;
}
int main(int argc,char** argv){try{
    std::setvbuf(stdout,nullptr,_IOLBF,0);std::string address="127.0.0.1:50051",world="earth",exported,definition;bool fast=false,binary=false,apollo=false,starship=false,follow=false;double utc=0;bool chosen=false;
    for(int i=1;i<argc;++i){const std::string a=argv[i];if(a=="--fast")fast=true;else if(a=="--wire")binary=true;else if(a=="--apollo11")apollo=true;else if(a=="--starship")starship=true;else if(a=="--follow-booster")follow=true;else if(a.rfind("--utc=",0)==0){utc=std::stod(a.substr(6));chosen=true;}
        else if(a.rfind("--body=",0)==0)world=a.substr(7);else if(a.rfind("--export-vehicle=",0)==0)exported=a.substr(17);else if(a.rfind("--definition=",0)==0)definition=a.substr(13);else address=a;}
    if(world!="earth"&&world!="moon"&&world!="mars")throw std::invalid_argument("--body: earth, moon or mars");
    if(binary&&world!="earth")throw std::invalid_argument("--wire flies the Earth case");
    auto channel=grpc::CreateChannel(address,grpc::InsecureChannelCredentials());
    if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(30)))throw std::runtime_error("Abra AlbertEngine --rpc antes de executar o exemplo");
    auto flight=api::FlightService::NewStub(channel);
    if(apollo)return apollo11(*flight,fast);
    if(starship){auto camera=api::CameraService::NewStub(channel); // --follow-booster: the camera on the booster from staging.
        ex::starship::mission(*flight,fast,follow?std::function<void()>([&]{grpc::ClientContext c;api::FocusRequest f;f.set_id("flight.booster");api::Applied a;check(camera->Focus(&c,f,&a));}):std::function<void()>());return 0;}
    if(!exported.empty()){ // A preset as JSON, to edit and fly with --definition.
        const std::map<std::string,api::Vehicle> presets{{"launcher",api::GENERIC_LAUNCHER},{"saturn-v",api::SATURN_V},{"lander",api::GENERIC_LANDER},{"lunar-ascent",api::GENERIC_LUNAR_ASCENT},{"mars-ascent",api::GENERIC_MARS_ASCENT}};
        const auto kind=presets.find(exported);if(kind==presets.end())throw std::invalid_argument("--export-vehicle: launcher, saturn-v, lander, lunar-ascent or mars-ascent");
        api::VehicleRequest q;q.set_vehicle(kind->second);api::VehicleDefinition d;{grpc::ClientContext c;check(flight->GetVehicle(&c,q,&d));}
        std::string json;google::protobuf::util::JsonPrintOptions o;o.add_whitespace=true;o.preserve_proto_field_names=true;
        if(!google::protobuf::util::MessageToJsonString(d,&json,o).ok())throw std::runtime_error("JSON conversion failed");
        std::printf("%s\n",json.c_str());return 0;
    }
    constexpr double pi=3.14159265358979323846,deg=pi/180;
    api::CreateFlightRequest create;create.set_focus_camera(true);if(chosen)create.set_utc_unix_seconds(utc);create.set_terrain_height(true); // On the measured ground.
    auto* site=create.mutable_site();ex::Body body=ex::earth;
    ex::GravityTurn guidance(pi/2); // Kennedy LC-39A eastward.
    if(world=="earth"){create.set_vehicle(api::GENERIC_LAUNCHER);create.set_heading_degrees(90);site->set_latitude_degrees(28.6082);site->set_longitude_degrees(-80.6041);
        if(!definition.empty()){std::ifstream in(definition);if(!in)throw std::runtime_error("Cannot read "+definition);const std::string json{std::istreambuf_iterator<char>(in),{}};
            if(const auto st=google::protobuf::util::JsonStringToMessage(json,create.mutable_definition());!st.ok())throw std::invalid_argument(definition+": "+std::string(st.message()));
            create.set_vehicle(api::CUSTOM);}}
    else if(!definition.empty())throw std::invalid_argument("--definition flies from the Earth");
    else if(world=="moon"){ // Tranquility Base, westward as Apollo 11: a 50 m rise, then steered to 18 km and a 15 x 85 km orbit.
        create.set_vehicle(api::GENERIC_LUNAR_ASCENT);create.set_body(api::CreateFlightRequest::MOON);create.set_heading_degrees(270);site->set_latitude_degrees(.67416);site->set_longitude_degrees(23.47314);
        body=ex::moon;guidance=ex::GravityTurn(270*deg,0,18.3e3,15e3,ex::moon,true,85e3,60,60);
    }else{ // Jezero crater, eastward: steered to a 250 km apoapsis, coast there, circularize to a 200 km perigee.
        create.set_vehicle(api::GENERIC_MARS_ASCENT);create.set_body(api::CreateFlightRequest::MARS);create.set_heading_degrees(90);site->set_latitude_degrees(18.4447);site->set_longitude_degrees(77.4508);
        body=ex::mars;guidance=ex::GravityTurn(90*deg,0,250e3,200e3,ex::mars,true,250e3,200,1000,true);
    }
    api::FlightSample s;{grpc::ClientContext c;check(flight->CreateFlight(&c,create,&s));}
    std::printf("%s, passo %.0f ms, %s\n",s.vehicle().c_str(),s.step_seconds()*1e3,fast?"o mais rápido possível":"tempo real");
    constexpr std::uint32_t ticks=20; // Guidance at 50 Hz.
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
    auto phase=guidance.phase();const auto wall=std::chrono::steady_clock::now();double report=0;
    const char* names[]{"pad","subida vertical","pitch kick","gravity turn","MECO","separação","guiado","órbita","rumo ao apoapsis","circularização"};
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
                s.dynamic_pressure_pascals(),s.mach(),s.mass_kilograms(),ex::GravityTurn::perigee(vec(s.position_meters()),vec(s.velocity_meters_per_second()),body)/1e3);
        }
        if(guidance.phase()==ex::GravityTurn::Phase::orbit){
            const ex::Vec rp=vec(s.position_meters()),vp=vec(s.velocity_meters_per_second());
            std::printf("Órbita: %.1f x %.1f km, %.0f kg de propelante restante\n",ex::GravityTurn::perigee(rp,vp,body)/1e3,ex::GravityTurn::apoapsis(rp,vp,body)/1e3,s.propellant_kilograms());
            if(world!="earth")return 0;
            // One revolution in nine calls: ballistic above the atmosphere, Albert takes 1 s steps.
            const auto t0=std::chrono::steady_clock::now();
            for(int i=0;i<9;++i){api::StepRequest coast;coast.set_ticks(600000);auto* q=coast.add_commands()->mutable_torque_newton_meters();q->set_x(0);q->set_y(0);q->set_z(0);
                grpc::ClientContext ctx;check(flight->Step(&ctx,coast,&s));}
            std::printf("Uma volta (90 min) calculada em %.2f s: altura %.1f km, perigeu %.1f km\n",std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count(),
                s.geodetic().height_meters()/1e3,ex::GravityTurn::perigee(vec(s.position_meters()),vec(s.velocity_meters_per_second()))/1e3);
            return 0;
        }
        if(s.landed())throw std::runtime_error("O veículo voltou ao solo");
        if(s.elapsed_seconds()>1200)throw std::runtime_error("Órbita não alcançada em 20 minutos");
        if(!fast)std::this_thread::sleep_until(wall+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(s.elapsed_seconds())));
    }
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
