#include "missions.hpp"
#include "core/bridge.hpp"
#include "rpc/server.hpp"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <stdexcept>
#include <iostream>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Predicate> void until(Predicate ready){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!ready()){
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Mission lifecycle timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}
int main(){try{
    using namespace albert::core;
    Scene scene;Bridge bridge;
    std::jthread owner([&](std::stop_token stop){
        while(!stop.stop_requested()){
            bridge.drain([&](Command& command){
                std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<T,Create>)scene.create(std::move(value.object),value.replace);
                    else if constexpr(std::is_same_v<T,Remove>)scene.remove(value.id);
                    else if constexpr(std::is_same_v<T,Append>)scene.samples(value.id,std::move(value.values));
                },command);return Reply{Code::Ok,{},scene.revision()};
            });
            auto snapshot=scene.snapshot(1792861200.,{});snapshot.simulation.utcSeconds=1792861200.;bridge.publish(std::move(snapshot));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    albert::rpc::Server server(bridge,0,nullptr,{[](double,double,double)->albert::rpc::LiveAir{throw std::runtime_error("offline");},[](double){return albert::rpc::UpperAir{};},{},{}});
    albert::missions::Runner mission;
    for(int run=0;run<2;++run){
        require(mission.startStarship(server.port()),"Mission did not start");
        require(!mission.startStarship(server.port()),"A duplicate mission was allowed");
        until([&]{return !server.flights().empty()||!mission.status().active;});
        require(mission.status().active,"Mission failed during startup");
        if(run==0){
            until([&]{return !server.flightGauges().empty();});
            mission.advanceTo(albert::missions::Phase::Separated);require(mission.playback().target==2,"Advance target was not retained");
            mission.speed(0);require(mission.playback().target<0,"Pausing did not cancel the advance");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));const auto before=server.flightGauges().front().utc;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));require(server.flightGauges().front().utc==before,"Paused mission continued stepping physics");
        }
        mission.stop();
        until([&]{return !mission.status().active;});
        require(mission.status().state==albert::missions::State::Idle,"Mission cleanup failed");
        require(server.flights().empty(),"Mission left flights behind");
    }
    // A client-owned flight must survive a rejected built-in mission.
    auto channel=grpc::CreateChannel("127.0.0.1:"+std::to_string(server.port()),grpc::InsecureChannelCredentials());
    auto flight=albert::v1::FlightService::NewStub(channel);albert::v1::CreateFlightRequest create;
    create.set_vehicle(albert::v1::GENERIC_LAUNCHER);create.set_weather(albert::v1::CreateFlightRequest::STANDARD_ATMOSPHERE);
    create.mutable_site()->set_latitude_degrees(28.6);create.mutable_site()->set_longitude_degrees(-80.6);
    albert::v1::FlightSample sample;grpc::ClientContext context;
    require(flight->CreateFlight(&context,create,&sample).ok(),"External flight fixture failed");
    mission.startStarship(server.port());until([&]{return !mission.status().active;});
    require(mission.status().state==albert::missions::State::Failed&&!server.flights().empty(),"Existing flight was replaced or removed");
    bridge.close();std::cout<<"Bundled mission starts, cancels, restarts and preserves external flights\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
