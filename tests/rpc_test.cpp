#include "rpc/server.hpp"
#include "rpc/wire.hpp"
#include "providers/gfs.hpp"
#include "providers/space_weather.hpp"
#include "physics/weather.hpp"
#include <erfa.h>
#include <filesystem>
#include <cmath>
#include <fstream>
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
int main(int argc,char** argv){try{
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
    // Replay inputs: the Tampa GFS analysis and ten synthetic space-weather days around 2026-12-01.
    std::string grib;{std::ifstream in{argc>1?argv[1]:"",std::ios::binary};grib.assign(std::istreambuf_iterator<char>(in),{});}
    std::vector<albert::physics::SpaceWeatherDay> days;for(int d=0;d<10;++d){albert::physics::SpaceWeatherDay x;x.day=std::floor(1796083200./86400.)-5+d;x.f107=120+d;x.f107a=110;x.apAverage=6;x.ap.fill(4+d);x.hasAp=true;x.type="OBS";days.push_back(x);}
    albert::rpc::Server server(bridge,0,nullptr,{[&](double lat,double,double)->albert::rpc::LiveAir{if(lat<0)throw std::runtime_error("offline");if(lat==1)std::this_thread::sleep_for(std::chrono::milliseconds(300));
            if(lat==27.5)return{std::make_shared<albert::physics::Weather>(albert::providers::gfsGrid({albert::providers::decodeGrib2(grib)},"NOAA GFS fixture")),{grib}};return{std::make_shared<Breeze>(),{}};},
        [&](double utc){albert::rpc::UpperAir a;if(std::abs(utc-1796083200.)<86400){a.model=albert::providers::msisFrom(days,utc);a.days=days;}return a;},
        [&](albert::physics::Center body,int degree){std::ifstream in{std::filesystem::path(argc>1?argv[1]:"").parent_path()/(body==albert::physics::Center::moon?"moon_gravity_degree8.tab":"mars_gravity_degree8.tab"),std::ios::binary};
            return albert::providers::gravityModel(body,{std::istreambuf_iterator<char>(in),{}},degree);}}); // No network in tests.
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
        require(std::abs(s.delta_velocity_meters_per_second().x()-s.measured_specific_force_meters_per_second2().x()*3)<1e-9&&s.measured_static_pressure_pascals()==s.static_pressure_pascals(),"Ideal IMU over the step");
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
        {grpc::ClientContext c;api::CreateFlightRequest noisy=create;noisy.mutable_imu()->set_grade(api::ImuSettings::TACTICAL);noisy.mutable_imu()->set_seed(5);require(flight->CreateFlight(&c,noisy,&s).ok(),"Tactical IMU");}
        {const double f=s.measured_specific_force_meters_per_second2().x(),ideal=s.specific_force_meters_per_second2().x();
            require(f!=ideal&&std::abs(f-ideal)<.05&&std::abs(s.measured_static_pressure_pascals()-s.static_pressure_pascals())<200,"Tactical IMU errors");}
        {grpc::ClientContext c;api::CreateFlightRequest live=create;live.set_weather(api::CreateFlightRequest::LIVE_GFS);require(flight->CreateFlight(&c,live,&s).ok(),"Live flight");}
        require(s.atmosphere()=="test breeze"&&std::abs(s.wind_ned_meters_per_second().y()-10)<1e-12,"Live weather in the sample");
        {grpc::ClientContext c;api::CreateFlightRequest south=create;south.set_weather(api::CreateFlightRequest::LIVE_GFS);south.mutable_site()->set_latitude_degrees(-5);
            require(flight->CreateFlight(&c,south,&s).error_code()==grpc::StatusCode::UNAVAILABLE,"Unavailable live weather accepted");}
        {grpc::ClientContext c;c.set_deadline(std::chrono::system_clock::now()+std::chrono::milliseconds(100));api::CreateFlightRequest slow=create;slow.set_weather(api::CreateFlightRequest::LIVE_GFS);slow.mutable_site()->set_latitude_degrees(1);
            require(!flight->CreateFlight(&c,slow,&s).ok(),"Slow creation finished before its deadline");}
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        {grpc::ClientContext c;require(flight->GetFlight(&c,{},&s).ok()&&s.atmosphere()=="test breeze"&&std::abs(s.geodetic().latitude_degrees()-28.6082)<1e-6,"A cancelled creation replaced the flight");}
        { // Binary data plane: same flight, same core as Step; atomic commands; throughput.
            {grpc::ClientContext c;require(flight->CreateFlight(&c,create,&s).ok()&&s.data_port()>0,"Data port");}
            albert::wire::Connection wire(s.data_port());albert::wire::Request q;q.ticks=3000;q.commands=albert::wire::ignite;
            auto r=wire.exchange(q);
            require(r.status==albert::wire::ok&&r.tick==3000&&(r.events&albert::wire::liftoff)&&!(r.flags&albert::wire::clamped)&&r.geodetic[2]>10,"Data plane liftoff");
            {grpc::ClientContext c;require(flight->GetFlight(&c,{},&s).ok()&&s.state_hash()==r.hash&&s.tick()==3000,"Data plane and gRPC share the flight");}
            q.ticks=5;q.commands=albert::wire::throttle;q.throttleValue=std::nan("");r=wire.exchange(q);require(r.status==albert::wire::invalid&&r.tick==3000,"Rejected frame changed the flight");
            q.ticks=1;q.commands=albert::wire::gimbal;q.gimbalPitch=.01;const auto t0=std::chrono::steady_clock::now();constexpr int n=20000;
            for(int i=0;i<n;++i)r=wire.exchange(q);
            const double each=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count()/n;
            std::printf("data plane: %.1f us per 1 ms tick (%.0fx real time), tick %llu\n",each,1000/each,static_cast<unsigned long long>(r.tick));
            require(r.status==albert::wire::ok&&r.tick==3000+n&&each<500,"Data plane throughput");
        }
        { // Real time: paced ticks, delayed samples and commands, missing-command policy.
            {grpc::ClientContext c;require(flight->CreateFlight(&c,create,&s).ok(),"Real-time flight");}
            albert::wire::Connection link(s.data_port());api::RealtimeStatus st;
            api::RealtimeRequest rq;rq.set_rate(5);rq.set_sensor_delay_ticks(5);rq.set_actuator_delay_ticks(20);rq.set_sample_every_ticks(10);rq.set_command_timeout_seconds(.1);rq.set_missing(api::RealtimeRequest::NEUTRAL);
            {grpc::ClientContext c;require(flight->StartRealtime(&c,rq,&st).ok()&&st.running(),"StartRealtime");}
            albert::wire::Response r{};for(int i=0;i<30;++i){r=link.receive();require(r.tick%10==5&&r.reserved==5,"Delayed samples every 10 ticks");}
            {grpc::ClientContext c;api::StepRequest q;q.set_ticks(1);require(flight->Step(&c,q,&s).error_code()==grpc::StatusCode::FAILED_PRECONDITION,"Step during real time");}
            const std::uint64_t before=r.tick;albert::wire::Request ignite;ignite.commands=albert::wire::ignite|albert::wire::gimbal;ignite.gimbalPitch=.02;link.send(ignite);
            do r=link.receive();while(!(r.flags&albert::wire::burning));
            std::printf("real time: ignition commanded after sample %llu, first burning sample %llu\n",static_cast<unsigned long long>(before),static_cast<unsigned long long>(r.tick));
            require(r.tick>=before+25,"Actuator and sensor delays");
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            {grpc::ClientContext c;require(flight->GetFlight(&c,{},&s).ok()&&s.gimbal().pitch_radians()==0&&s.burning(),"Missing commands: neutral gimbal, engine kept");}
            {grpc::ClientContext c;require(flight->StopRealtime(&c,{},&st).ok()&&!st.running()&&st.policy_events()==1&&st.tick()>before,"StopRealtime");}
            std::printf("real time: %llu ticks, %llu late, worst lag %.2f ms\n",static_cast<unsigned long long>(st.tick()),static_cast<unsigned long long>(st.late_ticks()),st.worst_lag_seconds()*1e3);
            {grpc::ClientContext c;api::StepRequest q;q.set_ticks(1);require(flight->Step(&c,q,&s).ok(),"Step after real time");}
            std::vector<api::FlightLogChunk> chunks;{grpc::ClientContext c;auto reader=flight->ExportFlight(&c,{});api::FlightLogChunk k;while(reader->Read(&k))chunks.push_back(k);require(reader->Finish().ok(),"Export real time");}
            api::FlightSample replayed;{grpc::ClientContext c;auto writer=flight->ReplayFlight(&c,&replayed);for(const auto& k:chunks)writer->Write(k);writer->WritesDone();
                require(writer->Finish().ok()&&replayed.state_hash()==s.state_hash(),"A real-time flight replays exactly");}
        }
        { // Review regressions: concurrent starts, prompt stop at a slow rate, events between
          // samples, abrupt disconnect, command limit, compact logs, forged space-weather days.
            {grpc::ClientContext c;require(flight->CreateFlight(&c,create,&s).ok(),"Regression flight");}
            {api::RealtimeRequest slow;slow.set_rate(.001);std::vector<std::thread> starts;
                for(int i=0;i<4;++i)starts.emplace_back([&]{grpc::ClientContext c;api::RealtimeStatus st;(void)flight->StartRealtime(&c,slow,&st);});for(auto& t:starts)t.join();
                const auto t0=std::chrono::steady_clock::now();grpc::ClientContext c;api::RealtimeStatus st;require(flight->StopRealtime(&c,{},&st).ok()&&!st.running(),"Stop after concurrent starts");
                require(std::chrono::steady_clock::now()-t0<std::chrono::milliseconds(500),"Stop waits for a slow tick");}
            {albert::wire::Connection link(s.data_port());api::RealtimeRequest rq;rq.set_rate(2);rq.set_sample_every_ticks(10);
                {grpc::ClientContext c;api::RealtimeStatus st;require(flight->StartRealtime(&c,rq,&st).ok(),"Start for events");}
                albert::wire::Request ignite;ignite.commands=albert::wire::ignite;link.send(ignite);std::uint32_t seen=0;
                for(int i=0;i<200&&!(seen&albert::wire::liftoff);++i)seen|=link.receive().events;
                require(seen&albert::wire::liftoff,"Liftoff reaches the client between samples");
            } // The client disconnects while samples are pushed.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {grpc::ClientContext c;api::RealtimeStatus st;require(flight->StopRealtime(&c,{},&st).ok(),"Stop after a disconnect");}
            {grpc::ClientContext c;api::StepRequest q;q.set_ticks(1);for(int i=0;i<65;++i)q.add_commands()->set_throttle(1);require(flight->Step(&c,q,&s).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"65 commands accepted");}
            {albert::wire::Connection wire(s.data_port());albert::wire::Request q;for(int i=0;i<20000;++i)wire.exchange(q);}
            std::size_t largest=0,total=0;{grpc::ClientContext c;auto reader=flight->ExportFlight(&c,{});api::FlightLogChunk k;while(reader->Read(&k)){largest=std::max(largest,k.data().size());total+=k.data().size();}require(reader->Finish().ok(),"Export");}
            std::printf("log after 20000 identical ticks: %zu bytes\n",total);require(largest<=(1u<<19)&&total<4096,"Compact log in small chunks");
            api::FlightLog forged;*forged.mutable_request()=create;forged.mutable_request()->set_utc_unix_seconds(1792238400.);forged.add_space_weather()->set_day(0);forged.add_space_weather()->set_day(1e8);forged.set_atmosphere("x");
            api::FlightLogChunk k;k.set_data(forged.SerializeAsString());api::FlightSample out;grpc::ClientContext c;auto writer=flight->ReplayFlight(&c,&out);writer->Write(k);writer->WritesDone();
            require(writer->Finish().error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Forged space-weather span accepted");
        }
        { // Replay: export, recreate without network, same hash; air data travels inside the log.
            auto exported=[&]{grpc::ClientContext c;auto reader=flight->ExportFlight(&c,{});api::FlightLogChunk k;std::vector<api::FlightLogChunk> v;while(reader->Read(&k))v.push_back(k);require(reader->Finish().ok(),"ExportFlight");return v;};
            auto replay=[&](const std::vector<api::FlightLogChunk>& v,api::FlightSample& out){grpc::ClientContext c;auto writer=flight->ReplayFlight(&c,&out);for(const auto& k:v)writer->Write(k);writer->WritesDone();return writer->Finish();};
            api::CreateFlightRequest noisy=create;noisy.mutable_imu()->set_grade(api::ImuSettings::INDUSTRIAL);noisy.mutable_imu()->set_seed(11);
            {grpc::ClientContext c;require(flight->CreateFlight(&c,noisy,&s).ok(),"Replay flight");}
            {grpc::ClientContext c;api::StepRequest r;r.set_ticks(2000);r.add_commands()->set_ignite(true);r.add_commands()->mutable_gimbal()->set_pitch_radians(.01);require(flight->Step(&c,r,&s).ok(),"Replay steps");}
            {albert::wire::Connection wire(s.data_port());albert::wire::Request q;q.commands=albert::wire::gimbal|albert::wire::torque;q.gimbalYaw=-.02;q.torqueValue[2]=100;for(int i=0;i<500;++i)wire.exchange(q);}
            {grpc::ClientContext c;api::StepRequest r;r.set_ticks(3000);r.add_commands()->set_throttle(.8);require(flight->Step(&c,r,&s).ok(),"Replay steps");}
            const std::uint64_t hash=s.state_hash();const double force=s.measured_specific_force_meters_per_second2().x();const auto log=exported();
            api::FlightSample again;const auto status=replay(log,again);if(!status.ok())std::cerr<<status.error_message()<<"\n";
            require(status.ok()&&again.state_hash()==hash&&again.tick()==5500&&again.measured_specific_force_meters_per_second2().x()==force,"Replay reproduces the flight");
            api::CreateFlightRequest weathered=create;weathered.set_weather(api::CreateFlightRequest::LIVE_GFS);weathered.set_utc_unix_seconds(1796083200.);weathered.mutable_site()->set_latitude_degrees(27.5);weathered.mutable_site()->set_longitude_degrees(-82.4);
            {grpc::ClientContext c;require(flight->CreateFlight(&c,weathered,&s).ok()&&s.atmosphere()=="NOAA GFS fixture + NRLMSISE-00 (at start: F10.7 124.0, 81-day 110.0, Ap 6 (OBS), CelesTrak)","Embedded air");}
            {grpc::ClientContext c;api::StepRequest r;r.set_ticks(20000);r.add_commands()->set_ignite(true);require(flight->Step(&c,r,&s).ok()&&s.wind_ned_meters_per_second().x()!=0,"Weathered flight");}
            const auto weatherLog=exported();api::FlightSample second;
            require(replay(weatherLog,second).ok()&&second.state_hash()==s.state_hash()&&second.atmosphere()==s.atmosphere(),"Replay with embedded GFS and space weather");
            api::CreateFlightRequest breeze=create;breeze.set_weather(api::CreateFlightRequest::LIVE_GFS);
            {grpc::ClientContext c;require(flight->CreateFlight(&c,breeze,&s).ok(),"Breeze flight");}
            api::FlightSample third;require(replay(exported(),third).error_code()==grpc::StatusCode::FAILED_PRECONDITION,"Replay without the air data accepted");
        }
        { // Moon and Mars: pad weight from the GRAIL and MRO fields, Martian air, a lunar vacuum, the
          // vehicle drawn at the body (ERFA here: the fixture scene has no lunar state), self-contained logs.
            auto exported=[&]{grpc::ClientContext c;auto reader=flight->ExportFlight(&c,{});api::FlightLogChunk k;std::vector<api::FlightLogChunk> v;while(reader->Read(&k))v.push_back(k);require(reader->Finish().ok(),"ExportFlight");return v;};
            api::CreateFlightRequest moon=create;moon.set_body(api::CreateFlightRequest::MOON);moon.mutable_site()->set_latitude_degrees(-85.4);moon.mutable_site()->set_longitude_degrees(31.2);moon.mutable_site()->set_height_meters(-3000);
            {grpc::ClientContext c;require(flight->CreateFlight(&c,moon,&s).error_code()==grpc::StatusCode::UNAVAILABLE,"Lunar degree above the fixture accepted");}
            moon.set_gravity_degree(8);
            using Edit=void(*)(api::CreateFlightRequest&);for(Edit refused:{+[](api::CreateFlightRequest& r){r.set_terrain_height(true);},+[](api::CreateFlightRequest& r){r.set_weather(api::CreateFlightRequest::LIVE_GFS);},+[](api::CreateFlightRequest& r){r.set_gravity_degree(601);},+[](api::CreateFlightRequest& r){r.set_body(static_cast<api::CreateFlightRequest::Body>(7));}}){
                api::CreateFlightRequest bad=moon;refused(bad);grpc::ClientContext c;require(flight->CreateFlight(&c,bad,&s).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Invalid lunar flight accepted");}
            {grpc::ClientContext c;require(flight->CreateFlight(&c,moon,&s).ok(),"Lunar flight");}
            require(std::abs(s.specific_force_meters_per_second2().x()-1.63)<.02&&s.static_pressure_pascals()==0&&s.atmosphere()=="Vacuum (no lunar atmosphere)"&&s.geodetic().height_meters()>-3000&&s.geodetic().height_meters()<-2950,"Lunar pad"); // The CG, above the base.
            {grpc::ClientContext c;api::StepRequest r;r.set_ticks(4000);r.add_commands()->set_ignite(true);require(flight->Step(&c,r,&s).ok()&&s.events_size()==1&&s.geodetic().height_meters()>-2900,"Lunar liftoff");}
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            {const auto snap=bridge.snapshot();const auto o=std::find_if(snap->objects.begin(),snap->objects.end(),[](const auto& o){return o.id=="flight";});require(o!=snap->objects.end(),"Lunar vehicle in the scene");
                double pv[2][3];eraMoon98(2451545.0,(albert::physics::secondsSinceJ2000(albert::physics::ttFromUtc(o->samples.front().utcSeconds)))/86400.,pv);constexpr double au=149597870700.;
                const auto& p=o->samples.front().positionMeters;require(std::abs(albert::physics::norm(albert::physics::V3{p.x-pv[0][0]*au,p.y-pv[0][1]*au,p.z-pv[0][2]*au})-(1737400.+s.geodetic().height_meters()))<100,"Vehicle drawn at the Moon");}
            const std::uint64_t hash=s.state_hash();const auto log=exported();
            {api::FlightSample again;grpc::ClientContext c;auto writer=flight->ReplayFlight(&c,&again);for(const auto& k:log)writer->Write(k);writer->WritesDone();require(writer->Finish().ok()&&again.state_hash()==hash,"Lunar replay from the embedded field");}
            api::CreateFlightRequest mars=moon;mars.set_body(api::CreateFlightRequest::MARS);mars.mutable_site()->set_latitude_degrees(18.4);mars.mutable_site()->set_longitude_degrees(77.5);mars.mutable_site()->set_height_meters(-2500);
            {grpc::ClientContext c;require(flight->CreateFlight(&c,mars,&s).ok(),"Martian flight");}
            require(std::abs(s.specific_force_meters_per_second2().x()-3.71)<.03&&std::abs(s.static_pressure_pascals()-albert::physics::marsAtmosphere(s.geodetic().height_meters()).pressure)<1e-6&&s.atmosphere()=="NASA Glenn Mars mean atmosphere","Martian pad");
        }
        {grpc::ClientContext c;require(flight->EndFlight(&c,{},&applied).ok(),"EndFlight failed");}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {const auto snap=bridge.snapshot();require(std::none_of(snap->objects.begin(),snap->objects.end(),[](const auto& o){return o.id=="flight";}),"Vehicle left in the scene");}
    }
    bridge.close();{grpc::ClientContext c;require(scene->CreateObject(&c,create,&applied).error_code()==grpc::StatusCode::UNAVAILABLE,"Shutdown did not reject commands");}
    std::cout<<"gRPC OK: create/focus/stream/remove, validation, revisions, snapshots, flight and shutdown\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
