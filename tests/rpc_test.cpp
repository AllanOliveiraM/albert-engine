#include "rpc/server.hpp"
#include "model_fixture.hpp"
#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
namespace api=albert::v1;using namespace albert::core;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{
    Scene registry;Bridge bridge;double monotonic=0;albert::time::Simulation simulation([]{return 100.5;},[&]{return monotonic;});
    std::jthread owner([&](std::stop_token stop){CameraState camera;
        while(!stop.stop_requested()){
            bridge.drain([&](Command& command){std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,Create>)registry.create(std::move(value.object));
                else if constexpr(std::is_same_v<T,Remove>){registry.remove(value.id);if(camera.target==value.id)camera.target="earth";}
                else if constexpr(std::is_same_v<T,CreateLayer>)registry.createLayer(std::move(value.layer));
                else if constexpr(std::is_same_v<T,RemoveLayer>)registry.removeLayer(value.id);
                else if constexpr(std::is_same_v<T,LayerVisibility>)registry.layerVisible(value.id,value.enabled);
                else if constexpr(std::is_same_v<T,InstallModel>)registry.installModel(std::move(value.model));
                else if constexpr(std::is_same_v<T,RemoveModel>)registry.removeModel(value.id);
                else if constexpr(std::is_same_v<T,Visibility>)registry.visible(value.id,value.enabled);
                else if constexpr(std::is_same_v<T,Append>)registry.samples(value.id,std::move(value.values));
                else if constexpr(std::is_same_v<T,Focus>){registry.get(value.id);camera.target=value.id;registry.cameraChanged();}
                else if constexpr(std::is_same_v<T,SetTime>){if(value.live)simulation.live();else if(value.hold&&value.utcSeconds)simulation.stall(*value.utcSeconds);else simulation.choose(value.utcSeconds,value.rate);}
            },command);return Reply{Code::Ok,{},registry.revision()};});
            const auto simulated=simulation.read();
            auto snapshot=registry.snapshot(simulated.utcSeconds,camera);snapshot.simulation=simulated;snapshot.clock.utcSeconds=100.5;snapshot.clock.quality=albert::time::Quality::Holdover;
            snapshot.clock.source="fixture-one, fixture-two";snapshot.clock.uncertaintySeconds=.03;snapshot.clock.generation=2;
            bridge.publish(std::move(snapshot));std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    struct Breeze final:albert::physics::AirModel { // Live-weather stand-in: standard air, 10 m/s toward the east.
        albert::physics::Air at(std::int64_t,double,double,double h)const override{auto a=albert::physics::standardAtmosphere(h);a.wind={0,10,0};return a;}
        std::string source()const override{return "test breeze";}
    };
    albert::rpc::Server server(bridge,0,nullptr,{[](double lat,double,double)->std::shared_ptr<const albert::physics::AirModel>{if(lat<0)throw std::runtime_error("offline");return std::make_shared<Breeze>();},
        [](double)->std::shared_ptr<const albert::physics::AirModel>{return nullptr;}}); // No network in tests.
    auto channel=grpc::CreateChannel("127.0.0.1:"+std::to_string(server.port()),grpc::InsecureChannelCredentials());
    require(channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(3)),"Server did not start");
    auto scene=api::SceneService::NewStub(channel);auto camera=api::CameraService::NewStub(channel);api::Applied applied;
    {auto time=api::TimeService::NewStub(channel);grpc::ClientContext c;api::ClockStatus timing;
        require(time->GetTime(&c,{},&timing).ok(),"Clock RPC failed");
        require(timing.quality()==api::ClockStatus::HOLDOVER&&timing.utc_unix_seconds()==100.5&&!timing.authenticated()&&timing.generation()==2,"Clock quality/provenance lost in RPC");
        grpc::ClientContext cs;api::Scene snapshot;require(scene->GetScene(&cs,{},&snapshot).ok(),"Scene snapshot failed");
        require(snapshot.has_clock()&&snapshot.clock().utc_unix_seconds()==snapshot.utc_unix_seconds()&&snapshot.clock().simulation().live(),"Scene and clock snapshots disagree");
        // Simulated date: the scene instant follows it, the disciplined clock does not.
        api::SetTimeRequest request;request.set_utc_unix_seconds(-14182940.);request.set_rate(0);api::Applied changed;
        {grpc::ClientContext cc;require(time->SetTime(&cc,request,&changed).ok(),"SetTime RPC failed");}
        api::Scene simulated;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        do{grpc::ClientContext cc;require(scene->GetScene(&cc,{},&simulated).ok(),"Scene snapshot failed");}while(simulated.utc_unix_seconds()!=-14182940.&&std::chrono::steady_clock::now()<end);
        const auto& chosen=simulated.clock().simulation();
        require(simulated.utc_unix_seconds()==-14182940.&&chosen.utc_unix_seconds()==-14182940.&&!chosen.live()&&chosen.rate()==0&&chosen.generation()==1&&simulated.clock().utc_unix_seconds()==100.5,"Simulated date not reported");
        {grpc::ClientContext cc;api::SetTimeRequest late;late.set_utc_unix_seconds(5e9);require(time->SetTime(&cc,late,&changed).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Date after 2100 accepted");}
        {grpc::ClientContext cc;api::SetTimeRequest fast;fast.set_rate(1e9);require(time->SetTime(&cc,fast,&changed).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Excessive rate accepted");}
        {grpc::ClientContext cc;require(time->SetTime(&cc,{},&changed).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Empty time change accepted");}
        {grpc::ClientContext cc;api::SetTimeRequest live;live.set_live(true);require(time->SetTime(&cc,live,&changed).ok(),"Return to live time failed");}
        api::ClockStatus back;const auto liveEnd=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        do{grpc::ClientContext cc;require(time->GetTime(&cc,{},&back).ok(),"Clock RPC failed");}while(!back.simulation().live()&&std::chrono::steady_clock::now()<liveEnd);
        require(back.simulation().live()&&back.simulation().utc_unix_seconds()==100.5&&back.simulation().generation()==2,"Live time not restored");}
    api::CreateObjectRequest create;create.set_id("rpc-test");create.set_name("External object");create.set_source("Test fixture");
    auto* g=create.mutable_geometry();g->set_shape(api::Geometry::BOX);g->mutable_size_meters()->set_x(10);g->mutable_size_meters()->set_y(20);g->mutable_size_meters()->set_z(30);
    auto* state=create.mutable_initial_state();state->set_frame(api::GEOCENTRIC_ICRF);state->set_utc_unix_seconds(100);state->mutable_position_meters()->set_x(1e11);state->mutable_attitude()->set_w(1);
    {grpc::ClientContext c;require(scene->CreateObject(&c,create,&applied).ok(),"Create RPC failed");require(applied.revision()>0,"RPC acknowledged before application");}
    {grpc::ClientContext c;require(scene->CreateObject(&c,create,&applied).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Duplicate ID accepted");}
    {grpc::ClientContext c;auto bad=create;bad.set_id("invalid");bad.mutable_initial_state()->mutable_position_meters()->set_x(std::numeric_limits<double>::quiet_NaN());require(scene->CreateObject(&c,bad,&applied).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Non-finite RPC state accepted");}
    {grpc::ClientContext c;api::FocusRequest request;request.set_id(create.id());require(camera->Focus(&c,request,&applied).ok(),"Focus RPC failed");}
    {bool focused=false;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        do{grpc::ClientContext c;api::Camera out;require(camera->GetCamera(&c,{},&out).ok(),"Camera snapshot failed");focused=out.target_id()==create.id();if(!focused)std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(!focused&&std::chrono::steady_clock::now()<deadline);
        require(focused,"Camera snapshot was inconsistent");}
    {grpc::ClientContext c;api::StreamSummary summary;auto stream=scene->StreamStates(&c,&summary);
        for(int i=1;i<=4;++i){api::StateBatch batch;batch.set_id(create.id());auto* s=batch.add_samples();s->CopyFrom(*state);s->set_utc_unix_seconds(100+i);s->mutable_position_meters()->set_x(1e11+i);require(stream->Write(batch),"Stream closed unexpectedly");}
        stream->WritesDone();require(stream->Finish().ok()&&summary.applied_batches()==4,"Streaming batches were lost");}
    {grpc::ClientContext c;api::StateBatch batch;batch.set_id(create.id());batch.add_samples()->CopyFrom(*state);require(scene->PushStates(&c,batch,&applied).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Old streaming sample accepted");}
    {grpc::ClientContext c;api::ObjectRequest request;request.set_id(create.id());require(scene->RemoveObject(&c,request,&applied).ok(),"Remove RPC failed");}
    {grpc::ClientContext c;api::ObjectRequest request;request.set_id("missing");require(scene->RemoveObject(&c,request,&applied).error_code()==grpc::StatusCode::NOT_FOUND,"Missing object error was lost");}
    auto assets=api::AssetService::NewStub(channel);api::RegisterModelRequest asset;asset.set_id("rpc-model");asset.set_source("Generated fixture");const auto bytes=model_fixture::glb();asset.set_glb(bytes.data(),bytes.size());
    {grpc::ClientContext c;require(assets->RegisterModel(&c,asset,&applied).ok(),"Model registration failed");}
    {grpc::ClientContext c;api::ModelAssets models;require(assets->ListModels(&c,{},&models).ok(),"Model listing failed");}
    create.set_id("model-object");g=create.mutable_geometry();g->set_shape(api::Geometry::MODEL);g->set_model_id(asset.id());g->set_meters_per_model_unit(30);
    {grpc::ClientContext c;require(scene->CreateObject(&c,create,&applied).ok(),"Model object creation failed");}
    {grpc::ClientContext c;api::ObjectRequest request;request.set_id(asset.id());require(assets->RemoveModel(&c,request,&applied).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Asset removed while in use");}
    {grpc::ClientContext c;api::StateBatch batch;batch.set_id(create.id());auto* s=batch.add_samples();s->CopyFrom(*state);s->set_utc_unix_seconds(101);require(scene->PushStates(&c,batch,&applied).ok(),"Model streaming state failed");}
    {grpc::ClientContext c;api::FocusRequest request;request.set_id(create.id());require(camera->Focus(&c,request,&applied).ok(),"Model focus failed");}
    {grpc::ClientContext c;api::ObjectRequest request;request.set_id(create.id());require(scene->RemoveObject(&c,request,&applied).ok(),"Model object removal failed");}
    {grpc::ClientContext c;api::ObjectRequest request;request.set_id(asset.id());require(assets->RemoveModel(&c,request,&applied).ok(),"Unused model removal failed");}
    auto layers=api::LayerService::NewStub(channel);api::CreateLayerRequest layer;layer.set_id("rpc-layer");layer.set_name("Geographic fixture");layer.set_frame(api::CreateLayerRequest::EARTH_SURFACE);layer.set_kind(api::CreateLayerRequest::POLYLINE);layer.add_geographic_points()->set_longitude_degrees(179);layer.add_geographic_points()->set_longitude_degrees(-179);
    {grpc::ClientContext c;require(layers->CreateLayer(&c,layer,&applied).ok(),"Layer create RPC failed");}
    {grpc::ClientContext c;api::VisibilityRequest request;request.set_id(layer.id());request.set_visible(false);require(layers->SetVisibility(&c,request,&applied).ok(),"Layer visibility routed to object service");}
    {bool found=false;for(int i=0;i<100&&!found;++i){grpc::ClientContext c;api::Layers result;require(layers->ListLayers(&c,{},&result).ok(),"Layer query failed");if(result.layers_size()==1){const auto& l=result.layers(0);found=l.id()==layer.id()&&!l.visible()&&l.anchor_id()=="earth"&&l.point_count()==41;}if(!found)std::this_thread::sleep_for(std::chrono::milliseconds(1));}require(found,"Layer metadata/snapshot lost");}
    {grpc::ClientContext c;api::ObjectRequest request;request.set_id(layer.id());require(layers->RemoveLayer(&c,request,&applied).ok(),"Layer removal RPC failed");}
    {   // Flight: pad, lockstep steps, events, atomic commands, scene mirror and clock.
        auto flight=api::FlightService::NewStub(channel);api::FlightSample s;
        api::CreateFlightRequest create;create.set_vehicle(api::GENERIC_LAUNCHER);create.set_heading_degrees(90);create.set_utc_unix_seconds(1792238400.);
        create.mutable_site()->set_latitude_degrees(28.6082);create.mutable_site()->set_longitude_degrees(-80.6041);
        {grpc::ClientContext c;api::StepRequest r;r.set_ticks(1);require(flight->Step(&c,r,&s).error_code()==grpc::StatusCode::FAILED_PRECONDITION,"Step without flight accepted");}
        {grpc::ClientContext c;api::CreateFlightRequest bad=create;bad.set_step_seconds(1);require(flight->CreateFlight(&c,bad,&s).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Invalid step accepted");}
        {grpc::ClientContext c;const auto st=flight->CreateFlight(&c,create,&s);if(!st.ok())std::cerr<<st.error_message()<<"\n";require(st.ok(),"CreateFlight failed");}
        require(s.clamped()&&s.tick()==0&&std::abs(s.specific_force_meters_per_second2().x()-9.79)<.02&&s.stages()==2,"Pad sample");
        {const auto snap=bridge.snapshot();require(snap&&std::any_of(snap->objects.begin(),snap->objects.end(),[](const auto& o){return o.id=="flight";}),"Vehicle not in the scene");}
        require(simulation.read().utcSeconds==1792238400.&&simulation.read().rate==0,"Clock not set to the flight");
        {grpc::ClientContext c;api::StepRequest r;r.set_ticks(5);r.add_commands()->set_ignite(true);r.add_commands()->set_throttle(std::nan(""));
            require(flight->Step(&c,r,&s).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"NaN throttle accepted");}
        {grpc::ClientContext c;require(flight->GetFlight(&c,{},&s).ok()&&!s.burning()&&s.tick()==0,"A rejected step changed the flight");}
        {grpc::ClientContext c;api::StepRequest r;r.set_ticks(3000);r.add_commands()->set_ignite(true);require(flight->Step(&c,r,&s).ok(),"Step failed");}
        require(s.tick()==3000&&std::abs(s.elapsed_seconds()-3)<1e-9&&!s.clamped()&&s.geodetic().height_meters()>10&&s.events_size()==1&&s.events(0).kind()==api::FlightEvent::LIFTOFF,"Liftoff");
        {grpc::ClientContext c;c.set_deadline(std::chrono::system_clock::now()+std::chrono::milliseconds(30));api::StepRequest r;r.set_ticks(600000);
            require(!flight->Step(&c,r,&s).ok(),"Long step finished before its deadline");}
        {grpc::ClientContext c;require(flight->GetFlight(&c,{},&s).ok()&&s.tick()==3000,"A cancelled step changed the flight");}
        {grpc::ClientContext c;api::StepRequest r;r.set_ticks(1);r.add_commands()->set_separate(true);r.add_commands()->set_separate(true);
            require(flight->Step(&c,r,&s).error_code()==grpc::StatusCode::FAILED_PRECONDITION,"Separating past the last stage accepted");}
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {grpc::ClientContext c;api::StepRequest r;r.set_ticks(100);require(flight->Step(&c,r,&s).ok()&&s.stage()==0,"Step after rejection");}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        require(std::abs(simulation.read().utcSeconds-s.utc_unix_seconds())<1e-6,"Clock follows the flight");
        {grpc::ClientContext c;api::StepRequest r;r.set_ticks(1);r.add_commands()->set_separate(true);require(flight->Step(&c,r,&s).ok()&&s.stage()==1,"Separation");}
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        {const auto snap=bridge.snapshot();const auto o=std::find_if(snap->objects.begin(),snap->objects.end(),[](const auto& o){return o.id=="flight";});
            require(o!=snap->objects.end()&&o->appearance.sizeMeters.y==27,"Upper stack shown after separation");}
        require(s.atmosphere()=="U.S. Standard Atmosphere 1976","A distant instant uses the standard atmosphere");
        {grpc::ClientContext c;api::CreateFlightRequest live=create;live.set_weather(api::CreateFlightRequest::LIVE_GFS);require(flight->CreateFlight(&c,live,&s).ok(),"Live flight");}
        require(s.atmosphere()=="test breeze"&&std::abs(s.wind_ned_meters_per_second().y()-10)<1e-12,"Live weather in the sample");
        {grpc::ClientContext c;api::CreateFlightRequest south=create;south.set_weather(api::CreateFlightRequest::LIVE_GFS);south.mutable_site()->set_latitude_degrees(-5);
            require(flight->CreateFlight(&c,south,&s).error_code()==grpc::StatusCode::UNAVAILABLE,"Unavailable live weather accepted");}
        {grpc::ClientContext c;require(flight->EndFlight(&c,{},&applied).ok(),"EndFlight failed");}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {const auto snap=bridge.snapshot();require(std::none_of(snap->objects.begin(),snap->objects.end(),[](const auto& o){return o.id=="flight";}),"Vehicle left in the scene");}
    }
    bridge.close();{grpc::ClientContext c;require(scene->CreateObject(&c,create,&applied).error_code()==grpc::StatusCode::UNAVAILABLE,"Shutdown did not reject commands");}
    std::cout<<"gRPC OK: create/focus/stream/remove, validation, revisions, snapshots, flight and shutdown\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
