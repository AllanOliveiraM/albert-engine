// The Starship-class mission (examples/starship_flight.hpp) against an in-process server, as the external flight computer
// flies it: the ship to orbit, the booster back to a catch on the tower's arms and at rest on them 30 s later; its timeline
// beside the public profile, loosely (approximate vehicles, not a model of any flight).
#include "core/scene.hpp"
#include "core/simulation.hpp"
#include "rpc/server.hpp"
#include "starship_flight.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
using namespace albert::core;
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
// Caught: on its pins in the arms' grooves 30 s on, a sway of a few centimetres about them allowed (how a catch hangs).
constexpr double sway=.5;
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
    if(argc>1&&std::string(argv[1]).rfind("--fall",0)==0){ // The ship's way down from its belly flop (the bench): one ("--fall", failures after "=") or a sweep of
        // where it starts ("--fall-sweep": offsets, headings), each caught, safe (down clear of the works) or unsafe.
        namespace sx=albert::example::starship;const std::string a=argv[1];
        if(a.rfind("--fall-sweep",0)!=0){sx::ShipGate g;if(const char* e=std::getenv("ALBERT_GATE"))std::sscanf(e,"%lf,%lf,%lf,%lf",&g.east,&g.north,&g.ve,&g.vn);if(const char* l=std::getenv("ALBERT_LOAD"))g.propellant=std::stod(l)*1e3; // Its propellant (t).
            sx::falling(*flight,g,a.size()>7?sx::faults(a.substr(7)):std::vector<sx::Fault>{});return 0;}
        struct Case {std::string name;sx::ShipGate g;};std::vector<Case> cases{{"nominal",{}}};
        for(int k=0;k<8;++k){const double b=k*45*sx::deg,v=std::hypot(198.2,59.8);sx::ShipGate g;g.ve=v*std::sin(b);g.vn=v*std::cos(b);g.east=-2000*std::sin(b);g.north=-2000*std::cos(b); // From 2 km short, heading in from each side.
            cases.push_back({"from "+std::to_string(k*45)+"°, 2 km short",g});g.east=-4500*std::sin(b);g.north=-4500*std::cos(b);cases.push_back({"from "+std::to_string(k*45)+"°, 4.5 km short",g});}
        int caught=0,safe=0,lost=0;
        for(const auto& k:cases){bool ok=false,clear=false;std::string what;
            try{const auto h=sx::falling(*flight,k.g);ok=h.caught>0&&h.onRails();clear=h.down&&h.clear();what=ok?"caught "+std::to_string(h.along).substr(0,5)+" m along, "+std::to_string(static_cast<int>(h.caught-h.flip))+" s from ignition":(clear?"safe: down ":"UNSAFE: down ")+std::to_string(static_cast<int>(h.away))+" m away ("+h.gaveUp+")";}
            catch(const std::exception& e){what=std::string("UNSAFE: ")+e.what();}
            caught+=ok;safe+=ok||clear;lost+=!(ok||clear);std::printf("FALL %-26s %s\n",k.name.c_str(),what.c_str());}
        std::printf("FALL %d caught, %d safe, %d unsafe of %zu\n",caught,safe,lost,cases.size());return lost?1:0;}
    if(argc>1&&std::string(argv[1])=="--home-sweep"){ // The ship's return from orbit end to end, from several places in orbit and loads: where its belly
        // flop begins (from where the landing burn is to hold it) and how it ends; caught, safe (down clear of the works) or unsafe.
        namespace sx=albert::example::starship;int caught=0,safe=0,lost=0,n=0;
        for(const double ahead:{540.,550.,560.,570.,580.})for(const double load:{150e3,189e3}){bool ok=false,clear=false;std::string what;double handoff=-1,miss=-1;++n;
            try{const auto h=sx::homecoming(*flight,true,1000,ahead,{},load);handoff=h.handoff;miss=h.handoffMiss;ok=h.caught>0&&h.onRails()&&h.speed<sway;clear=h.down&&h.clear();
                what=ok?"caught "+std::to_string(h.along).substr(0,5)+" m along, "+std::to_string(static_cast<int>(h.caught-h.flip))+" s from ignition":(clear?"safe: down ":"UNSAFE: down ")+std::to_string(static_cast<int>(h.away))+" m away ("+h.gaveUp+")";}
            catch(const std::exception& e){what=std::string("UNSAFE: ")+e.what();}
            caught+=ok;safe+=ok||clear;lost+=!(ok||clear);std::printf("HOME %3.0f° %3.0f t  belly flop from %5.0f m, unsteered miss %5.0f m  %s\n",ahead,load/1e3,handoff,miss,what.c_str());}
        std::printf("HOME %d caught, %d safe, %d unsafe of %d\n",caught,safe,lost,n);return lost?1:0;}
    if(argc>1&&std::string(argv[1]).rfind("--home",0)==0){const std::string a=argv[1]; // The ship's return alone (development); "=t[,deg]": its propellant (t), how far round its orbit it starts (560°).
        const std::size_t comma=a.find(',');albert::example::starship::homecoming(*flight,true,1000,comma!=std::string::npos?std::stod(a.substr(comma+1)):560,{},a.size()>7?std::stod(a.substr(7))*1e3:189e3);return 0;}
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
            const bool pad=!m.scrubbed.empty(),caught=!pad&&!m.boosterLost&&!m.boosterDown&&m.caught>0&&m.onRails()&&m.speed<sway,safe=caught||(m.boosterDown||m.boosterLost)&&m.clear();
            bool found=!*k.found||m.scrubbed.find(k.found)!=std::string::npos;for(const auto& w:m.fdir)found=found||w.find(k.found)!=std::string::npos;
            const bool ok=(k.expect=='p'?pad&&m.padSafe:k.expect=='c'?caught:safe)&&found&&m.towerClearance>0;failed+=!ok;
            std::printf("SCENARIO %-60s %s (%s%s)\n",*k.spec?k.spec:"nominal",ok?"ok":"FAILED",pad?"scrubbed: ":caught?"caught":m.boosterDown?"down ":m.boosterLost?"lost ":"?",
                pad?m.scrubbed.c_str():caught?"":(std::to_string(static_cast<int>(m.boosterAway))+" m away").c_str());}
        require(failed==0,"A scenario not ridden out");std::puts("Every scenario ridden out");return 0;}
    if(argc>1&&std::string(argv[1]).rfind("--landing",0)==0){ // The booster's landing alone (the bench); failures after "=".
        // ALBERT_WIND=n,e (m/s): a wind added from the start.
        const std::string a=argv[1];albert::example::starship::Gate g;if(const char* w=std::getenv("ALBERT_WIND"))std::sscanf(w,"%lf,%lf",&g.wind.x,&g.wind.y);
        const auto m=albert::example::starship::landing(*flight,g,a.size()>10?albert::example::starship::faults(a.substr(10)):std::vector<albert::example::starship::Fault>{},false);
        std::printf("%s\n",m.caught>0?("caught, at rest "+std::to_string(m.along)+" m along, "+std::to_string(m.across)+" m across, "+std::to_string(m.speed)+" m/s").c_str():
            ((m.boosterDown?"down ":m.boosterLost?"LOST ":"? ")+std::to_string(static_cast<int>(m.boosterAway))+" m away ("+m.gaveUp+")").c_str());return 0;}
    if(argc>1&&std::string(argv[1]).rfind("--sweep",0)==0){ // What the landing rides out: winds, failures and where it starts, each from the bench.
        struct Case {std::string name;albert::example::Vec wind;const char* faults;double east,north;};
        std::vector<Case> cases{{"nominal",{},"",0,0},{"engine 1 out",{},"booster@landing:1",0,0},{"engine 1 silent",{},"booster@landing:silent:1",0,0},
            {"gyro drift",{},"booster@landing:imu2.gyro=drift:1e-3,0,0",0,0},{"accel bias",{},"booster@coast:imu1.accel=bias:0.5,0,0",0,0},
            {"200 m east",{},"",200,0},{"200 m west",{},"",-200,0},{"100 m north",{},"",0,100},{"100 m south",{},"",0,-100}};
        for(const double speed:{4.,8.})for(int k=0;k<8;++k){const double a=k*45*albert::example::starship::deg; // The air going toward north, north-east...
            cases.push_back({"wind "+std::to_string(static_cast<int>(speed))+" to "+std::to_string(k*45)+"°",{speed*std::cos(a),speed*std::sin(a),0},"",0,0});}
        if(std::string(argv[1])=="--sweep=full")for(int k=0;k<8;++k){const double a=k*45*albert::example::starship::deg;
            cases.push_back({"wind 12 to "+std::to_string(k*45)+"°",{12*std::cos(a),12*std::sin(a),0},"",0,0});
            cases.push_back({"wind 6 to "+std::to_string(k*45)+"°, engine 1 out",{6*std::cos(a),6*std::sin(a),0},"booster@landing:1",0,0});}
        int caught=0,safe=0,lost=0;
        for(const auto& k:cases){albert::example::starship::Gate g;g.wind=k.wind;g.east+=k.east;g.north+=k.north;
            const auto m=albert::example::starship::landing(*flight,g,albert::example::starship::faults(k.faults));
            const bool ok=m.caught>0&&m.onRails()&&m.speed<sway,held=m.caught>0&&m.onRails(),clear=(m.boosterDown||m.boosterLost)&&m.clear(); // held: on the arms, still swaying.
            caught+=ok;safe+=held||clear;lost+=!(held||clear);
            std::printf("SWEEP %-30s %s\n",k.name.c_str(),held?((ok?"caught at ":"caught, swaying "+std::to_string(m.speed).substr(0,4)+" m/s, at ")+std::to_string(static_cast<int>(m.caught))+" s, "+std::to_string(m.along).substr(0,5)+" m along").c_str():
                ((clear?"safe: ":"UNSAFE: ")+std::string(m.boosterDown?"down ":m.boosterLost?"lost ":"? ")+std::to_string(static_cast<int>(m.boosterAway))+" m away ("+m.gaveUp+")").c_str());}
        std::printf("SWEEP %d caught, %d safe, %d unsafe of %zu\n",caught,safe,lost,cases.size());return lost?1:0;}
    if(argc>1&&std::string(argv[1]).rfind("--booster",0)==0){ // The ascent and the booster's way back alone (development); failures after "=".
        const std::string a=argv[1];const auto m=albert::example::starship::mission(*flight,true,1000,true,{},a.size()>10?albert::example::starship::faults(a.substr(10)):std::vector<albert::example::starship::Fault>{});
        std::printf("MECO %.1f s at %.1f km; booster %s, %.2f m off the axis, %.1f m from the tower\n",m.meco,m.mecoHeight/1e3,m.boosterDown?"down":m.boosterLost?"lost":"caught",m.offAxis,
            m.boosterDown?m.boosterAway:m.towerClearance);return 0;}
    if(argc>1&&std::string(argv[1]).rfind("--fail=",0)==0){ // A mission with engine failures (development).
        const auto f=albert::example::starship::mission(*flight,true,1000,false,{},albert::example::starship::faults(std::string(argv[1]).substr(7)));
        std::printf("booster %.2f m off the axis, ship %.2f m; clearance %.1f and %.1f m\n",f.offAxis,f.ship.offAxis,f.towerClearance,f.ship.towerClearance);return 0;}
    int bad=0;auto check=[&](bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';++bad;}}; // Every check reported, not only the first.
    const auto m=albert::example::starship::mission(*flight,true);
    check(m.meco>128&&m.meco<152&&m.mecoHeight>48e3&&m.mecoHeight<66e3,"Staging off Flights 12-14's (~140 s after liftoff, ~55 km)");
    check(m.perigee>170e3&&m.apoapsis<260e3,"The ship short of its 180 km orbit");
    check(m.caught>m.meco+200&&m.onRails()&&m.speed<sway&&m.propellant>5e3,"The booster not caught and at rest on the arms");
    check(m.ship.deorbit>6e4&&m.ship.deorbit<1.7e5,"The ship's deorbit burn not on the pass over the tower a day on");
    check(m.ship.caught>m.ship.entry&&m.ship.onRails()&&m.ship.speed<sway&&m.ship.propellant>5e3,"The ship not caught and at rest on the arms");
    check(m.ship.maxTile<1700&&m.ship.overheats==0,"The ship's heat shield past its limit");
    check(m.towerClearance>0&&m.ship.towerClearance>0&&!m.boosterLost,"A vehicle through the tower, or broken up");
    // Engines failing as the guidance must ride out: three of the 33 on the way up, a centre one as the booster lights for
    // landing and another as it hovers, one of the ship's sea-level Raptors as it deorbits (it lands on the other two).
    const auto f=albert::example::starship::mission(*flight,true,1000,false,{},albert::example::starship::faults("stack@60:20,stack@90:30,stack@100:5,booster@landing:1,booster@hover:2,ship@deorbit:2"));
    check(f.perigee>170e3&&f.caught>f.meco+200&&f.onRails()&&f.speed<sway&&f.ship.caught>f.ship.entry&&f.ship.onRails()&&f.ship.speed<sway&&f.towerClearance>0&&f.ship.towerClearance>0&&!f.boosterLost,
        "Engine failures not ridden out");
    require(bad==0,"Starship-class mission off its marks");
    std::puts("Starship-class mission: orbit, booster caught and at rest, the ship home a day later, caught and at rest; and again with six engines failing");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
