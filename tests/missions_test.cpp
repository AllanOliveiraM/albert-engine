#include "missions.hpp"
#include "mission_time.hpp"
#include "starship_flight.hpp"
#include <imgui_internal.h>
#include "core/bridge.hpp"
#include "rpc/server.hpp"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <stdexcept>
#include <iostream>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void coastPlayback(){
    namespace ex=albert::example;
    bool advancing=true;double multiplier=60;
    ex::flightSpeed=[&]{return multiplier;};ex::flightAdvance=[&]{return advancing;};
    ex::starship::Pacer pace;
    pace(3600.,1000.,true);
    require(pace.last==3600.&&pace.wall==0.,"Milestone coast retained an artificial playback delay");
    advancing=false;multiplier=1.;pace(3600.02,1.,false);
    require(std::abs(pace.wall-.02)<1e-9,"Entry inherited time debt from the skipped coast");
    ex::starship::Pacer normal;normal(1.,1000.,true);
    require(std::abs(normal.wall-.001)<1e-12,"Normal orbital acceleration changed without an advance");
    advancing=true;multiplier=60.;ex::starship::Pacer burn;burn(1.,1.,false);
    require(std::abs(burn.wall-1./60.)<1e-12,"Unpaced coasting leaked into an active flight phase");
    std::stop_source source;ex::flightStop=source.get_token();multiplier=0;
    std::jthread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(50));source.request_stop();});
    bool stopped=false;try{ex::starship::Pacer paused;paused(3600.,1000.,true);}catch(const std::runtime_error&){stopped=source.stop_requested();}
    require(stopped,"Unpaced coast ignored pause/stop");
    ex::flightStop={};ex::flightSpeed={};ex::flightAdvance={};
}
void timePanel(albert::missions::Runner& mission){
    for(float width:{288.f,420.f,560.f})for(float scale:{.8f,1.f,1.6f}){
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={width,800};io.DeltaTime=1.f/60.f;io.FontGlobalScale=scale;
        ImFontConfig config;config.SizePixels=16;io.Fonts->AddFontDefault(&config);
        unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
        ImGui::GetStyle().FramePadding={10,8};
        for(int frame=0;frame<2;++frame){
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,800});
            ImGui::Begin("Mission time",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
            albert::missions::drawTimeControls(mission,ImGui::GetContentRegionAvail().x);
            const auto* window=ImGui::GetCurrentWindow();
            require(window->DC.CursorMaxPos.x<=window->WorkRect.Max.x+1,"Mission time controls overflow the panel");
            ImGui::End();ImGui::Render();
        }
        ImGui::DestroyContext();
    }
}
template<class Predicate> void until(Predicate ready){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!ready()){
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Mission lifecycle timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}
int main(){try{
    coastPlayback();
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
            timePanel(mission);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));const auto before=server.flightGauges().front().utc;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));require(server.flightGauges().front().utc==before,"Paused mission continued stepping physics");
            mission.speed(5);mission.speed(0);
            require(mission.playback().speed==0&&mission.playback().resumeSpeed==5,"Pause lost the selected mission speed");
            mission.resume();require(mission.playback().speed==5,"Resume did not restore the selected speed");
            mission.speed(0);
            mission.advanceTo(albert::missions::Phase::Ascent);
            until([&]{const auto p=mission.playback();return (p.phase>=albert::missions::Phase::Ascent&&p.target<0)||!mission.status().active;});
            require(mission.status().state==albert::missions::State::Running,"Mission failed while advancing to liftoff");
            const auto reached=mission.playback();
            require(reached.phase==albert::missions::Phase::Ascent&&reached.target<0&&reached.speed==1.&&reached.resumeSpeed==1.,"Milestone did not cancel acceleration and return to 1x");
            mission.advanceTo(albert::missions::Phase::Countdown);
            require(mission.playback().target<0,"Mission accepted a backward milestone");
            mission.speed(0);
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
    bridge.close();std::cout<<"Bundled mission starts, pauses, advances to liftoff at 1x, cancels, restarts and preserves external flights\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
