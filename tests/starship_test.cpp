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
    if(argc>1&&std::string(argv[1]).rfind("--scenarios",0)==0){ // Each system failing or lying, and winds: the count must refuse a launch with it, the booster's way back ride it out or end safe.
        // expect: 'c' caught and at rest on the arms; 's' that, or down clear of the ground's works by its length (never on the
        // tower, the pad or the tanks); 'p' the
        // count stopped on the pad, the stack whole, clamped, its engines out; found: what its FDIR or the count must report
        // (empty: nothing asked).
        struct Case {const char* spec;char expect;const char* found;};
        const Case cases[]{
            {"",'c',""},
            {"stack@-55:imu2.accel=bias:0.02,0,0",'p',"IMU 2"},{"stack@-55:imu3.gyro=stuck",'p',"IMU 3"},{"stack@-50:imu1.gyro=drift:2e-7,0,0",'p',"IMU 1"},
            {"stack@-55:gnss=dead",'p',"GNSS"},{"stack@-58:gauge=bias:100000",'p',"medidor"},{"stack@-45:gimbal:7",'p',"gimbal do motor 7"},
            {"stack@-35:5",'p',"motor 5"},{"stack@-2:9",'p',"motor 9"},{"stack@-1.5:silent:20",'p',"motor 20"},
            {"booster@coast:imu1.accel=bias:0.5,0,0",'c',"IMU 1"},{"booster@landing:imu2.gyro=drift:1e-3,0,0",'c',"IMU 2"},
            {"booster@coast:imu3.accel=dead",'c',"IMU 3"},{"booster@coast:imu1.gyro=stuck",'c',"IMU 1"},
            {"booster@coast:gauge=stuck",'c',"medidor"},{"booster@coast:gauge=bias:-30000",'c',"medidor"},{"booster@landing:silent:1",'c',"motor 1"},
            {"booster@coast:gnss=bias:80,0,0",'s',"GNSS"},{"booster@coast:gnss=dead",'s',""},{"booster@landing:gnss=stuck",'s',"GNSS"},
            {"booster@landing:gust:0,-6,0",'c',""},{"booster@landing:gust:8,0,0",'c',""},{"booster@landing:gust:-6,6,0",'c',""},{"booster@landing:gust:15,0,0",'s',""},
            {"booster@landing:1,booster@landing:4,booster@landing:5,booster@landing:6,booster@landing:7,booster@landing:8,booster@landing:9",'s',""},
            {"booster@hover:1,booster@hover:2",'s',""},{"booster@final:push:0,-600000,0",'s',""}}; // Never onto the tower or the ground's works.
        int failed=0;
        const std::string only=std::string(argv[1]).size()>12?std::string(argv[1]).substr(12):""; // --scenarios=text: those whose failures name it (development).
        for(const auto& k:cases){if(!only.empty()&&std::string(k.spec).find(only)==std::string::npos)continue;const auto m=albert::example::starship::mission(*flight,true,1000,true,{},albert::example::starship::faults(k.spec));
            const bool pad=!m.scrubbed.empty(),caught=!pad&&!m.boosterLost&&!m.boosterDown&&m.caught>0&&m.onRails()&&m.speed<.1,safe=caught||(m.boosterDown||m.boosterLost)&&m.clear();
            bool found=!*k.found||m.scrubbed.find(k.found)!=std::string::npos;for(const auto& w:m.fdir)found=found||w.find(k.found)!=std::string::npos;
            const bool ok=(k.expect=='p'?pad&&m.padSafe:k.expect=='c'?caught:safe)&&found&&m.towerClearance>0;failed+=!ok;
            std::printf("SCENARIO %-60s %s (%s%s)\n",*k.spec?k.spec:"nominal",ok?"ok":"FAILED",pad?"scrubbed: ":caught?"caught":m.boosterDown?"down ":m.boosterLost?"lost ":"?",
                pad?m.scrubbed.c_str():caught?"":(std::to_string(static_cast<int>(m.boosterAway))+" m away").c_str());}
        require(failed==0,"A scenario not ridden out");std::puts("Every scenario ridden out");return 0;}
    if(argc>1&&std::string(argv[1]).rfind("--booster",0)==0){ // The ascent and the booster's way back alone (development); failures after "=".
        const std::string a=argv[1];const auto m=albert::example::starship::mission(*flight,true,1000,true,{},a.size()>10?albert::example::starship::faults(a.substr(10)):std::vector<albert::example::starship::Fault>{});
        std::printf("MECO %.1f s at %.1f km; booster %s, %.2f m off the axis, %.1f m from the tower\n",m.meco,m.mecoHeight/1e3,m.boosterDown?"down":m.boosterLost?"lost":"caught",m.offAxis,
            m.boosterDown?m.boosterAway:m.towerClearance);return 0;}
    if(argc>1&&std::string(argv[1]).rfind("--fail=",0)==0){ // A mission with engine failures (development).
        const auto f=albert::example::starship::mission(*flight,true,1000,false,{},albert::example::starship::faults(std::string(argv[1]).substr(7)));
        std::printf("booster %.2f m off the axis, ship %.2f m; clearance %.1f and %.1f m\n",f.offAxis,f.ship.offAxis,f.towerClearance,f.ship.towerClearance);return 0;}
    int bad=0;auto check=[&](bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';++bad;}}; // Every check reported, not only the first.
    const auto m=albert::example::starship::mission(*flight,true);
    check(m.meco>135&&m.meco<165&&m.mecoHeight>60e3&&m.mecoHeight<90e3,"Staging off the public profile (~141-160 s, ~64-75 km)");
    check(m.perigee>170e3&&m.apoapsis<260e3,"The ship short of its 180 km orbit");
    check(m.caught>m.meco+200&&m.onRails()&&m.speed<.1&&m.propellant>5e3,"The booster not caught and at rest on the arms");
    check(m.ship.deorbit>6e4&&m.ship.deorbit<1.7e5,"The ship's deorbit burn not on the pass over the tower a day on");
    check(m.ship.caught>m.ship.entry&&m.ship.onRails()&&m.ship.speed<.1&&m.ship.propellant>5e3,"The ship not caught and at rest on the arms");
    check(m.ship.maxTile<1700&&m.ship.overheats==0,"The ship's heat shield past its limit");
    check(m.towerClearance>0&&m.ship.towerClearance>0&&!m.boosterLost,"A vehicle through the tower, or broken up");
    // Engines failing as the guidance must ride out: three of the 33 on the way up, a centre one as the booster lights for
    // landing and another as it hovers, one of the ship's sea-level Raptors as it deorbits (it lands on the other two).
    const auto f=albert::example::starship::mission(*flight,true,1000,false,{},albert::example::starship::faults("stack@60:20,stack@90:30,stack@100:5,booster@landing:1,booster@hover:2,ship@deorbit:2"));
    check(f.perigee>170e3&&f.caught>f.meco+200&&f.onRails()&&f.speed<.1&&f.ship.caught>f.ship.entry&&f.ship.onRails()&&f.ship.speed<.1&&f.towerClearance>0&&f.ship.towerClearance>0&&!f.boosterLost,
        "Engine failures not ridden out");
    require(bad==0,"Starship-class mission off its marks");
    std::puts("Starship-class mission: orbit, booster caught and at rest, the ship home a day later, caught and at rest; and again with six engines failing");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
