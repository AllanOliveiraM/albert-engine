// The Starship-class mission (examples/starship_flight.hpp) against an in-process server, as the external flight computer
// flies it: the ship to orbit, the booster back to a catch on the tower's arms and at rest on them 30 s later; its timeline
// beside the public profile, loosely (approximate vehicles, not a model of any flight).
#include "core/scene.hpp"
#include "core/simulation.hpp"
#include "rpc/server.hpp"
#include "starship_flight.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
using namespace albert::core;
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv){try{
    Scene registry;Bridge bridge;double monotonic=0;albert::time::Simulation simulation([]{return 1792238400.;},[&]{return monotonic;});
    std::jthread owner([&](std::stop_token stop){CameraState camera;
        while(!stop.stop_requested()){
            bridge.drain([&](Command& command){std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,Create>)registry.create(std::move(value.object),value.replace);
                else if constexpr(std::is_same_v<T,Remove>)registry.remove(value.id);
                else if constexpr(std::is_same_v<T,Append>)registry.samples(value.id,std::move(value.values));
                else if constexpr(std::is_same_v<T,Visibility>)registry.visible(value.id,value.enabled);
                else if constexpr(std::is_same_v<T,Focus>){registry.get(value.id);camera.target=value.id;registry.cameraChanged();}
            },command);return Reply{Code::Ok,{},registry.revision()};});
            const auto simulated=simulation.read();auto snapshot=registry.snapshot(simulated.utcSeconds,camera);snapshot.simulation=simulated;
            bridge.publish(std::move(snapshot));std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    albert::rpc::Server server(bridge,0,nullptr,{[](double,double,double)->albert::rpc::LiveAir{throw std::runtime_error("offline");},[](double){return albert::rpc::UpperAir{};},{},{}});
    auto channel=grpc::CreateChannel("127.0.0.1:"+std::to_string(server.port()),grpc::InsecureChannelCredentials());
    require(channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(3)),"Server did not start");
    auto flight=albert::v1::FlightService::NewStub(channel);
    if(argc>1&&std::string(argv[1])=="--home"){albert::example::starship::homecoming(*flight,true);return 0;} // The ship's return alone (development).
    if(argc>1&&std::string(argv[1]).rfind("--fail=",0)==0){ // A mission with engine failures (development).
        const auto f=albert::example::starship::mission(*flight,true,1000,false,{},albert::example::starship::faults(std::string(argv[1]).substr(7)));
        std::printf("booster %.2f m off the axis, ship %.2f m; clearance %.1f and %.1f m\n",f.offAxis,f.ship.offAxis,f.towerClearance,f.ship.towerClearance);return 0;}
    const auto m=albert::example::starship::mission(*flight,true);
    require(m.meco>135&&m.meco<165&&m.mecoHeight>60e3&&m.mecoHeight<90e3,"Staging off the public profile (~141-160 s, ~64-75 km)");
    require(m.perigee>170e3&&m.apoapsis<260e3,"The ship short of its 180 km orbit");
    require(m.caught>m.meco+200&&m.offAxis<1&&m.speed<.1&&m.propellant>5e3,"The booster not caught and at rest on the arms");
    require(m.ship.deorbit>6e4&&m.ship.deorbit<1.7e5,"The ship's deorbit burn not on the pass over the tower a day on");
    require(m.ship.caught>m.ship.entry&&m.ship.offAxis<1.5&&m.ship.speed<.1&&m.ship.propellant>5e3,"The ship not caught and at rest on the arms");
    require(m.ship.maxTile<1700&&m.ship.overheats==0,"The ship's heat shield past its limit");
    require(m.towerClearance>0&&m.ship.towerClearance>0,"A vehicle through the tower");
    // Engines failing as the guidance must ride out: three of the 33 on the way up, a centre one as the booster lights for
    // landing and another as it hovers, one of the ship's sea-level Raptors as it deorbits (it lands on the other two).
    const auto f=albert::example::starship::mission(*flight,true,1000,false,{},albert::example::starship::faults("stack@60:20,stack@90:30,stack@100:5,booster@landing:1,booster@hover:2,ship@deorbit:2"));
    require(f.perigee>170e3&&f.caught>f.meco+200&&f.offAxis<1&&f.speed<.1&&f.ship.caught>f.ship.entry&&f.ship.offAxis<1.5&&f.ship.speed<.1&&f.towerClearance>0&&f.ship.towerClearance>0,
        "Engine failures not ridden out");
    std::puts("Starship-class mission: orbit, booster caught and at rest, the ship home a day later, caught and at rest; and again with six engines failing");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
