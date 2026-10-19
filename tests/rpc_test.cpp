#include "rpc/server.hpp"
#include "rpc/wire.hpp"
#include "providers/gfs.hpp"
#include "providers/space_weather.hpp"
#include "physics/weather.hpp"
#include "physics/earth.hpp"
#include <erfa.h>
#include <filesystem>
#include <cmath>
#include <fstream>
#include "model_fixture.hpp"
#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
namespace api=albert::v1;using namespace albert::core;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv){try{
    Scene registry;Bridge bridge;double monotonic=0;std::atomic<int> flightCreates{0};albert::time::Simulation simulation([]{return 100.5;},[&]{return monotonic;});
    std::jthread owner([&](std::stop_token stop){CameraState camera;
        while(!stop.stop_requested()){
            bridge.drain([&](Command& command){std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,Create>){if(value.object.id=="flight")++flightCreates;registry.create(std::move(value.object));}
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
            return albert::providers::gravityModel(body,{std::istreambuf_iterator<char>(in),{}},degree);},
        [](albert::physics::Center body,double lat,double lon){if(lat>88)throw std::invalid_argument("polar gap");return (body==albert::physics::Center::moon?-1000.:-2000.)+lat+lon/1000;}}); // No network in tests.
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
        require(s.engines_size()==1&&!s.engines(0).on()&&!s.engines(0).failed(),"The stage's engine in the sample");
        {grpc::ClientContext c;api::StepRequest r;auto* k=r.add_commands();k->set_ignite(true);k->set_index(2);require(flight->Step(&c,r,&s).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A missing engine accepted");}
        {grpc::ClientContext c;api::StepRequest r;auto* k=r.add_commands();k->mutable_torque_newton_meters()->set_x(1);k->set_index(1);require(flight->Step(&c,r,&s).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"An engine on a torque accepted");}
        {grpc::ClientContext c;api::StepRequest r;r.add_commands()->set_fail(true);require(flight->Step(&c,r,&s).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Fail without an engine accepted");}
        {grpc::ClientContext c;api::StepRequest r;auto* k=r.add_commands();k->set_fail(true);k->set_index(1);r.add_commands()->set_ignite(true);
            require(flight->Step(&c,r,&s).error_code()==grpc::StatusCode::FAILED_PRECONDITION,"Ignition with every engine failed accepted");}
        {grpc::ClientContext c;api::StepRequest r;auto* k=r.add_commands();k->set_throttle(.8);k->set_index(1);r.add_commands()->mutable_gimbal()->set_pitch_radians(.01);
            require(flight->Step(&c,r,&s).ok()&&s.engines(0).throttle()==.8&&s.engines(0).gimbal_command().pitch_radians()==.01&&s.tick()==0,"Engine commands");}
        {grpc::ClientContext c;api::StepRequest r;r.add_commands()->set_throttle(1);r.add_commands()->mutable_gimbal();require(flight->Step(&c,r,&s).ok()&&s.engines(0).throttle()==1,"Back to full throttle");}
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
        require(s.heat_flux_watts_per_meter2()>0&&s.heat_load_joules_per_meter2()>0,"Ascent heating reported");
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
            {grpc::ClientContext c;require(flight->CreateFlight(&c,weathered,&s).ok()&&s.atmosphere()=="NOAA GFS fixture + NRLMSISE-00/HWM14 (at start: F10.7 124.0, 81-day 110.0, Ap 6 (OBS), CelesTrak)","Embedded air");}
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
            using Edit=void(*)(api::CreateFlightRequest&);for(Edit refused:{+[](api::CreateFlightRequest& r){r.mutable_site()->set_latitude_degrees(89);r.set_terrain_height(true);},+[](api::CreateFlightRequest& r){r.set_weather(api::CreateFlightRequest::LIVE_GFS);},+[](api::CreateFlightRequest& r){r.set_gravity_degree(601);},+[](api::CreateFlightRequest& r){r.set_body(static_cast<api::CreateFlightRequest::Body>(7));}}){
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
            { // Review regression: a forged log cannot carry a gravity table longer than its degree allows.
                std::string bytes;for(const auto& k:log)bytes+=k.data();api::FlightLog forged;require(forged.ParseFromString(bytes),"Lunar log");forged.mutable_gravity()->append(1<<20,' ');
                const std::string data=forged.SerializeAsString();api::FlightSample out;grpc::ClientContext c;auto writer=flight->ReplayFlight(&c,&out);
                for(std::size_t at=0;at<data.size();at+=1u<<19){api::FlightLogChunk k;k.set_data(data.substr(at,1u<<19));writer->Write(k);}writer->WritesDone();
                require(writer->Finish().error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Oversized gravity table replayed");}
            {api::CreateFlightRequest ground=moon;ground.set_terrain_height(true);grpc::ClientContext c;
                require(flight->CreateFlight(&c,ground,&s).ok()&&std::abs(s.geodetic().height_meters()-(-1000-85.4+.0312))<30,"Lunar site on the LOLA ground");}
            { // A lander released onto its legs: touchdown event, contact flag, lunar weight on the accelerometer, hop.
                api::CreateFlightRequest lander=moon;lander.set_vehicle(api::GENERIC_LANDER);grpc::ClientContext c;require(flight->CreateFlight(&c,lander,&s).ok(),"Lunar lander");
                {grpc::ClientContext d;api::StepRequest q;q.set_ticks(3000);q.add_commands()->set_release(true);require(flight->Step(&d,q,&s).ok(),"Lander release");}
                bool down=false;for(const auto& e:s.events())down=down||e.kind()==api::FlightEvent::TOUCHDOWN;
                require(down&&s.leg_contact()&&!s.landed()&&std::abs(s.specific_force_meters_per_second2().x()-1.63)<.02,"Lander on its legs");
                {grpc::ClientContext d;api::StepRequest q;q.set_ticks(3000);q.add_commands()->set_ignite(true);require(flight->Step(&d,q,&s).ok()&&!s.leg_contact(),"Lander hop");}
                bool up=false;for(const auto& e:s.events())up=up||(e.kind()==api::FlightEvent::LIFTOFF&&e.elapsed_seconds()<s.elapsed_seconds()-1);require(up,"Hop lift-off event at its instant");}
            api::CreateFlightRequest mars=moon;mars.set_body(api::CreateFlightRequest::MARS);mars.mutable_site()->set_latitude_degrees(18.4);mars.mutable_site()->set_longitude_degrees(77.5);mars.mutable_site()->set_height_meters(-2500);
            {grpc::ClientContext c;require(flight->CreateFlight(&c,mars,&s).ok(),"Martian flight");}
            require(std::abs(s.specific_force_meters_per_second2().x()-3.71)<.03&&std::abs(s.static_pressure_pascals()-albert::physics::marsAtmosphere(s.geodetic().height_meters()).pressure)<1e-6&&s.atmosphere()=="NASA Glenn Mars mean atmosphere","Martian pad");
            { // The keeper: a paused Martian flight follows the scene's Mars when its state changes at the
              // flight's instant (Horizons replacing ERFA, ~1000 km), but not when the clock moves away
              // (review: the correction's drift with the scene's time republished, re-framing the camera
              // and moving the clock back to the flight's instant).
                namespace ph=albert::physics;const double t0=mars.utc_unix_seconds();const int before=flightCreates.load();
                Object m;m.id="mars";m.name="Marte";m.radiusMeters=3396190;
                for(int k=-2;k<=2;++k){const double t=t0+k*3600.;const auto e=ph::geocentric(ph::Center::mars,ph::ttFromUtc(t));
                    m.samples.emplace_back(t,Vector{e.position.x+1e6+3*(t-t0),e.position.y,e.position.z},Vector{e.velocity.x+3,e.velocity.y,e.velocity.z},std::nullopt,true);}
                require(bridge.submit(Create{m}).result.get().code==Code::Ok,"Scene Mars");
                auto fromMars=[&]{const auto snap=bridge.snapshot();Vector f,c;for(const auto& o:snap->objects){if(o.id=="flight")f=o.samples.front().positionMeters;if(o.id=="mars")c=o.samples.front().positionMeters;}
                    return ph::norm(ph::V3{f.x-c.x,f.y-c.y,f.z-c.z});};
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                require(flightCreates.load()==before+1&&fromMars()>3.37e6&&fromMars()<3.41e6,"Keeper moved the flight onto the scene's Mars");
                (void)bridge.submit(SetTime{t0+3600.,{},false,false}).result.get();std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                require(flightCreates.load()==before+1&&std::abs(bridge.snapshot()->utcSeconds-(t0+3600.))<1,"Keeper left the scrubbed clock and the flight alone");
                (void)bridge.submit(SetTime{t0,{},false,false}).result.get();(void)bridge.submit(Remove{"mars"}).result.get();
            }
        }
        { // Physics switches: unset ones resolved (all on by default) and reported; off ones change the flight; point mass.
            api::CreateFlightRequest a=create;a.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);api::FlightSample x;
            {grpc::ClientContext c;require(flight->CreateFlight(&c,a,&x).ok()&&x.physics().has_wind()&&x.physics().third_bodies()&&x.physics().relativity()&&x.physics().atmosphere(),"Switches resolved on");}
            a.mutable_physics()->set_atmosphere(false);a.set_gravity_degree(0);
            {grpc::ClientContext c;require(flight->CreateFlight(&c,a,&x).ok()&&!x.physics().atmosphere()&&x.air_density_kilograms_per_meter3()==0&&x.static_pressure_pascals()==0,"Atmosphere off: vacuum");}
            {const auto snap=bridge.snapshot();(void)snap;}
            a.set_gravity_degree(1);{grpc::ClientContext c;require(flight->CreateFlight(&c,a,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Degree 1 accepted");}
            { // Starts in flight: over Florida at 10 km heading north at 200 m/s; in a 400 km circular orbit from the inertial frame.
                api::CreateFlightRequest f=create;f.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);auto* l=f.mutable_start()->mutable_local();
                l->mutable_position()->set_latitude_degrees(28);l->mutable_position()->set_longitude_degrees(-80);l->mutable_position()->set_height_meters(1e4);l->mutable_velocity_ned_meters_per_second()->set_x(200);
                {grpc::ClientContext c;require(flight->CreateFlight(&c,f,&x).ok()&&!x.clamped()&&std::abs(x.geodetic().height_meters()-1e4)<1e-3&&std::abs(x.velocity_ned_meters_per_second().x()-200)<1e-6,"A start over the ground");}
                api::CreateFlightRequest o=create;o.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);auto* i=o.mutable_start()->mutable_inertial();o.mutable_start()->set_stage(1);
                const double r=6778137,v=std::sqrt(3.986004418e14/r);i->mutable_position_meters()->set_x(r);i->mutable_velocity_meters_per_second()->set_y(v);i->mutable_attitude()->set_w(1);
                {grpc::ClientContext c;require(flight->CreateFlight(&c,o,&x).ok()&&x.stage()==1,"A start in orbit");}
                {grpc::ClientContext c;api::StepRequest q;q.set_ticks(60000);require(flight->Step(&c,q,&x).ok()&&std::abs(x.geodetic().height_meters()-4e5)<2e4,"Still in orbit a minute later");}
                o.mutable_start()->set_stage(5);{grpc::ClientContext c;require(flight->CreateFlight(&c,o,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A missing start stage accepted");}
            }
            { // A wheeled jet on a runway (a definition from scratch): the jet spools up and pushes it, the brakes slow it.
                api::VehicleDefinition plane;plane.set_name("Taxi test");auto* st=plane.add_stages();st->set_name("airframe");st->set_propellant_kilograms(1000);
                st->set_mass_full_kilograms(10000);st->set_mass_empty_kilograms(9000);st->set_cg_full_meters(10);st->set_cg_empty_meters(10);st->set_length_meters(20);
                for(auto* i:{st->mutable_inertia_full_kilogram_meters2(),st->mutable_inertia_empty_kilogram_meters2()}){i->set_x(2e4);i->set_y(1e5);i->set_z(1e5);}
                for(const auto& [x,y]:{std::pair{3.,0.},std::pair{12.,-2.},std::pair{12.,2.}}){auto* w=st->add_wheels();w->set_name("w");w->mutable_position_meters()->set_x(x);w->mutable_position_meters()->set_y(y);
                    w->mutable_position_meters()->set_z(2);w->set_stiffness_newtons_per_meter(1e6);w->set_damping_newton_seconds_per_meter(6e4);if(x<5)w->set_steer_limit_radians(.5);}
                auto* jet=st->add_engines();jet->set_name("jet");jet->mutable_position_meters()->set_x(18);jet->set_air_breathing(true);jet->set_spool_seconds(1);
                auto* tt=jet->mutable_thrust_table();tt->add_mach(0);tt->add_altitude_km(0);tt->add_values(3e4);auto* ft=jet->mutable_fuel_table();ft->add_mach(0);ft->add_altitude_km(0);ft->add_values(2e-5);
                api::CreateFlightRequest f=create;f.set_vehicle(api::CUSTOM);*f.mutable_definition()=plane;f.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);f.mutable_site()->set_height_meters(0);
                f.mutable_physics()->set_wind(false);
                auto* l=f.mutable_start()->mutable_local();l->mutable_position()->set_latitude_degrees(28.6);l->mutable_position()->set_longitude_degrees(-80.6);l->mutable_position()->set_height_meters(2-10000*9.79/3e6);
                {grpc::ClientContext c;const auto st2=flight->CreateFlight(&c,f,&x);if(!st2.ok())std::cerr<<st2.error_message()<<"\n";require(st2.ok()&&x.wheels_size()==3,"A wheeled jet");}
                {grpc::ClientContext c;api::StepRequest q;q.set_ticks(10000);q.add_commands()->set_ignite(true);require(flight->Step(&c,q,&x).ok()&&x.engines(0).spool()>.99&&x.velocity_ned_meters_per_second().x()>10,"The jet pushes it");}
                const double fast=x.velocity_ned_meters_per_second().x();
                {grpc::ClientContext c;api::StepRequest q;q.set_ticks(3000);q.add_commands()->set_cutoff(true);q.add_commands()->set_brake(1);auto* k=q.add_commands();k->set_steer_radians(.2);k->set_index(1);
                    require(flight->Step(&c,q,&x).ok()&&x.wheels(0).brake()==1&&x.wheels(0).steer_radians()==.2&&x.wheels(1).steer_radians()==0&&x.velocity_ned_meters_per_second().x()<fast-5,"Brakes and the nose wheel");}
            }
            { // Splashdown and a catch: the NESC ball hits water at 10 m/s (touchdown, never "landed", the water brakes it); with a hard
              // point at its centre it comes to rest on a tower arm 20 m up.
                api::VehicleRequest vq;vq.set_vehicle(api::NESC_SPHERE);api::VehicleDefinition ball;{grpc::ClientContext c;require(flight->GetVehicle(&c,vq,&ball).ok(),"Ball");}
                api::CreateFlightRequest w=create;w.set_vehicle(api::CUSTOM);*w.mutable_definition()=ball;w.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);w.set_water(true);w.mutable_site()->set_height_meters(0);
                auto* l=w.mutable_start()->mutable_local();l->mutable_position()->set_latitude_degrees(28);l->mutable_position()->set_longitude_degrees(-79);l->mutable_position()->set_height_meters(5);
                l->mutable_velocity_ned_meters_per_second()->set_z(10);
                {grpc::ClientContext c;require(flight->CreateFlight(&c,w,&x).ok(),"Over water");}
                {grpc::ClientContext c;api::StepRequest q;q.set_ticks(2000);require(flight->Step(&c,q,&x).ok()&&!x.landed()&&std::any_of(x.events().begin(),x.events().end(),[](const auto& e){return e.kind()==api::FlightEvent::TOUCHDOWN;})
                    &&x.geodetic().height_meters()<0&&x.velocity_ned_meters_per_second().z()<10,"Splashdown");}
                api::CreateFlightRequest t=create;t.set_vehicle(api::CUSTOM);*t.mutable_definition()=ball;t.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);
                auto* h=t.mutable_definition()->mutable_stages(0)->add_hard_points_meters();h->set_x(ball.stages(0).cg_full_meters());
                auto* arm=t.add_structures();arm->set_name("arm");arm->mutable_start_enu_meters()->set_x(-3);arm->mutable_start_enu_meters()->set_z(20);arm->mutable_end_enu_meters()->set_x(3);arm->mutable_end_enu_meters()->set_z(20);arm->set_radius_meters(.2);
                auto* s=t.mutable_start()->mutable_local();s->mutable_position()->set_latitude_degrees(28.6082);s->mutable_position()->set_longitude_degrees(-80.6041);s->mutable_position()->set_height_meters(t.site().height_meters()+20.3);
                {grpc::ClientContext c;require(flight->CreateFlight(&c,t,&x).ok(),"Over the arm");}
                {grpc::ClientContext c;api::StepRequest q;q.set_ticks(3000);require(flight->Step(&c,q,&x).ok()&&std::abs(x.velocity_ned_meters_per_second().z())<1e-2
                    &&std::abs(x.geodetic().height_meters()-t.site().height_meters()-20.2)<.01&&x.contact_force_newtons().z()<-100,"Caught on the arm");} // Held up: body -z, level attitude.
            }
            { // Reentry heating: the NESC ball at 70 km and 7 km/s, nose along the velocity, a windward node (limit 1000 K) and a lee one.
                api::VehicleRequest vq;vq.set_vehicle(api::NESC_SPHERE);api::VehicleDefinition ball;{grpc::ClientContext c;require(flight->GetVehicle(&c,vq,&ball).ok(),"Ball");}
                for(const double nx:{1.,-1.}){auto* n=ball.mutable_stages(0)->add_thermal_nodes();n->set_name(nx>0?"front":"back");n->mutable_normal()->set_x(nx);n->set_area_meters2(.01);
                    n->set_heat_capacity_joules_per_kelvin(50);n->set_limit_kelvin(1000);}
                api::CreateFlightRequest f=create;f.set_vehicle(api::CUSTOM);*f.mutable_definition()=ball;f.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);
                auto* l=f.mutable_start()->mutable_local();l->mutable_position()->set_latitude_degrees(28);l->mutable_position()->set_longitude_degrees(-80);l->mutable_position()->set_height_meters(7e4);
                l->mutable_velocity_ned_meters_per_second()->set_x(7000);l->mutable_velocity_ned_meters_per_second()->set_z(1000);l->set_pitch_degrees(-std::atan(1000./7000)*180/3.141592653589793);
                {grpc::ClientContext c;require(flight->CreateFlight(&c,f,&x).ok()&&x.thermal_nodes_size()==2&&x.thermal_nodes(0).heat_flux_watts_per_meter2()>1e5&&x.thermal_nodes(1).heat_flux_watts_per_meter2()==0,"Windward and lee nodes");}
                {grpc::ClientContext c;api::StepRequest q;q.set_ticks(5000);require(flight->Step(&c,q,&x).ok(),"Reentry steps");}
                std::printf("reentry 5 s: front %.0f K, back %.0f K, %d events\n",x.thermal_nodes(0).temperature_kelvin(),x.thermal_nodes(1).temperature_kelvin(),x.events_size());
                require(x.thermal_nodes(0).over_limit()&&x.thermal_nodes(1).temperature_kelvin()<300&&std::any_of(x.events().begin(),x.events().end(),[](const auto& e){return e.kind()==api::FlightEvent::OVERHEAT&&e.index()==0;}),"The front overheats, the back does not");
                {grpc::ClientContext c;api::StepRequest q;q.set_ticks(1000);require(flight->Step(&c,q,&x).ok()&&std::none_of(x.events().begin(),x.events().end(),[](const auto& e){return e.kind()==api::FlightEvent::OVERHEAT;}),"Reported once");}
                const auto hash=x.state_hash();auto log=[&]{grpc::ClientContext c;auto reader=flight->ExportFlight(&c,{});api::FlightLogChunk k;std::vector<api::FlightLogChunk> v;while(reader->Read(&k))v.push_back(k);require(reader->Finish().ok(),"Export");return v;}();
                api::FlightSample again;grpc::ClientContext c;auto writer=flight->ReplayFlight(&c,&again);for(const auto& k:log)writer->Write(k);writer->WritesDone();require(writer->Finish().ok()&&again.state_hash()==hash,"Reentry replays with its temperatures");
            }
            { // Turbulence: a different flight from the calm one, the same twice for one seed.
                api::CreateFlightRequest t=create;t.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);auto hash=[&](double sigma,std::uint64_t seed){t.mutable_physics()->set_turbulence_meters_per_second(sigma);t.mutable_physics()->set_turbulence_seed(seed);
                    {grpc::ClientContext c;require(flight->CreateFlight(&c,t,&x).ok(),"Turbulent flight");}grpc::ClientContext c;api::StepRequest q;q.set_ticks(4000);q.add_commands()->set_ignite(true);require(flight->Step(&c,q,&x).ok(),"Turbulent steps");return x.state_hash();};
                const auto calm=hash(0,1),gusty=hash(3,1),again=hash(3,1);require(gusty!=calm&&gusty==again&&x.physics().turbulence_meters_per_second()==3,"Turbulence changes the flight, reproducibly");
                t.mutable_physics()->set_turbulence_meters_per_second(-1);grpc::ClientContext c;require(flight->CreateFlight(&c,t,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Negative turbulence accepted");
            }
            // Sensors: a GNSS fix and a radar range at the pad (the stack's base on the ground), measured again after a step.
            api::CreateFlightRequest b=create;b.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);auto* q=b.mutable_sensors();q->mutable_gnss()->set_enabled(true);
            q->mutable_radar_altimeter()->set_enabled(true);q->mutable_star_tracker()->set_enabled(true);q->set_seed(9);
            {grpc::ClientContext c;require(flight->CreateFlight(&c,b,&x).ok()&&x.gnss().valid()&&x.has_radar_altimeter()&&x.has_star_tracker(),"Sensors at the pad");}
            const double offset=std::hypot(x.gnss().position_ecef_meters().x()-x.position_ecef_meters().x(),x.gnss().position_ecef_meters().y()-x.position_ecef_meters().y(),x.gnss().position_ecef_meters().z()-x.position_ecef_meters().z());
            require(offset>0&&offset<30,"GNSS near the truth");
            // Measured at the end of the step that completes their period: both at 0.15 s; 30 ms later neither is due.
            {grpc::ClientContext c;api::StepRequest r;r.set_ticks(150);require(flight->Step(&c,r,&x).ok()&&std::abs(x.gnss().elapsed_seconds()-.15)<1e-9&&std::abs(x.radar_altimeter().elapsed_seconds()-.15)<1e-9,"Sensors at their rates");}
            {grpc::ClientContext c;api::StepRequest r;r.set_ticks(30);require(flight->Step(&c,r,&x).ok()&&std::abs(x.gnss().elapsed_seconds()-.15)<1e-9&&std::abs(x.radar_altimeter().elapsed_seconds()-.15)<1e-9,"Not before their period");}
            {auto bad=b;bad.mutable_sensors()->mutable_gnss()->set_period_seconds(0);grpc::ClientContext c;require(flight->CreateFlight(&c,bad,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A zero GNSS period accepted");}
        }
        { // Every preset as a definition (GetVehicle) flies as CUSTOM exactly as the preset; malformed definitions are refused.
            auto fly=[&](const api::CreateFlightRequest& r,bool engine){api::FlightSample x;
                {grpc::ClientContext c;const auto st=flight->CreateFlight(&c,r,&x);if(!st.ok())std::cerr<<st.error_message()<<"\n";require(st.ok(),"Flight from a definition");}
                grpc::ClientContext c;api::StepRequest q;q.set_ticks(1500);if(engine)q.add_commands()->set_ignite(true);else q.add_commands()->set_release(true);
                require(flight->Step(&c,q,&x).ok(),"Steps of a defined vehicle");return x.state_hash();};
            for(const auto kind:{api::GENERIC_LAUNCHER,api::NESC_TWO_STAGE,api::NESC_SPHERE,api::GENERIC_LANDER,api::GENERIC_LUNAR_ASCENT,api::GENERIC_MARS_ASCENT,api::SATURN_V}){
                api::VehicleRequest q;q.set_vehicle(kind);api::VehicleDefinition d;{grpc::ClientContext c;require(flight->GetVehicle(&c,q,&d).ok()&&d.stages_size()>0,"GetVehicle");}
                api::CreateFlightRequest a=create;a.set_vehicle(kind);a.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);api::CreateFlightRequest b=a;b.set_vehicle(api::CUSTOM);*b.mutable_definition()=d;
                const bool engine=d.stages(0).engines_size()>0;require(fly(a,engine)==fly(b,engine),"A preset's definition flies as the preset");
            }
            api::VehicleRequest q;q.set_vehicle(api::GENERIC_LAUNCHER);api::VehicleDefinition d;{grpc::ClientContext c;require(flight->GetVehicle(&c,q,&d).ok(),"GetVehicle");}
            auto refused=[&](api::VehicleDefinition bad,const char* what){api::CreateFlightRequest r=create;r.set_vehicle(api::CUSTOM);*r.mutable_definition()=std::move(bad);
                api::FlightSample x;grpc::ClientContext c;require(flight->CreateFlight(&c,r,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,what);};
            {auto bad=d;bad.mutable_stages(0)->mutable_aerodynamics()->mutable_lift()->set_mach(1,-1);refused(bad,"Decreasing table breakpoints accepted");}
            {auto bad=d;bad.mutable_stages(0)->mutable_engines(0)->set_vacuum_thrust_newtons(0);refused(bad,"An engine without thrust accepted");}
            {auto bad=d;bad.mutable_stages(0)->set_mass_empty_kilograms(1);refused(bad,"Inconsistent masses accepted");}
            {auto bad=d;bad.clear_stages();refused(bad,"A vehicle without stages accepted");}
            {api::CreateFlightRequest r=create;*r.mutable_definition()=d;api::FlightSample x;grpc::ClientContext c;require(flight->CreateFlight(&c,r,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A definition with a preset accepted");}
            { // Thrusters and a flap on the launcher's first stage: the torque command fires them, both take direct commands.
                auto winged=d;auto* st=winged.mutable_stages(0);
                for(const double x:{2.,40.})for(const double z:{1.,-1.}){auto* t=st->add_thrusters();t->set_name("q");t->mutable_position_meters()->set_x(x);t->mutable_direction()->set_z(z);t->set_thrust_newtons(500);t->set_exhaust_velocity_meters_per_second(700);}
                auto* f=st->add_surfaces();f->set_name("flap");f->set_limit_radians(.5);auto* m=f->mutable_pitch();m->add_mach(0);m->add_angle_degrees(0);m->add_deflection_degrees(-10);m->add_deflection_degrees(10);m->add_values(-.1);m->add_values(.1);
                api::CreateFlightRequest r=create;r.set_vehicle(api::CUSTOM);*r.mutable_definition()=winged;api::FlightSample x;
                {grpc::ClientContext c;require(flight->CreateFlight(&c,r,&x).ok()&&x.thruster_duty_size()==4&&x.surfaces_size()==1,"Thrusters and surfaces in the sample");}
                {grpc::ClientContext c;api::StepRequest q;q.add_commands()->mutable_torque_newton_meters()->set_y(1000);auto* k=q.add_commands();k->set_surface_radians(.1);k->set_index(1);
                    const auto st=flight->Step(&c,q,&x);
                    require(st.ok()&&std::abs(x.thruster_moment_newton_meters().y()-1000)<1e-3&&x.surfaces(0).deflection_radians()==.1,"Torque by thrusters, the flap set");}
                {grpc::ClientContext c;api::StepRequest q;auto* k=q.add_commands();k->set_thruster_duty(1);k->set_index(5);require(flight->Step(&c,q,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A missing thruster accepted");}
                {grpc::ClientContext c;api::StepRequest q;q.add_commands()->set_thruster_duty(0);auto* k=q.add_commands();k->set_thruster_duty(.25);k->set_index(2);
                    require(flight->Step(&c,q,&x).ok()&&x.thruster_duty(0)==0&&x.thruster_duty(1)==.25,"Thrusters set directly");}
            }
        }
        { // Two flights at once: the default and "booster", each its own scene object, data port and steps; ended apart.
            api::CreateFlightRequest b=create;b.set_flight_id("booster");b.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);api::FlightSample one,two;
            {grpc::ClientContext c;const auto st=flight->CreateFlight(&c,b,&two);if(!st.ok())std::cerr<<st.error_message()<<"\n";require(st.ok()&&two.flight_id()=="booster","A second flight");}
            {grpc::ClientContext c;require(flight->GetFlight(&c,{},&one).ok()&&one.flight_id()=="flight"&&one.data_port()!=two.data_port(),"Both flights, apart");}
            {grpc::ClientContext c;api::StepRequest q;q.set_flight_id("booster");q.set_ticks(2000);q.add_commands()->set_ignite(true);require(flight->Step(&c,q,&two).ok()&&two.tick()==2000,"Steps of the second");}
            {grpc::ClientContext c;api::FlightSample again;require(flight->GetFlight(&c,{},&again).ok()&&again.tick()==one.tick(),"The first untouched");}
            {const auto snap=bridge.snapshot();require(std::any_of(snap->objects.begin(),snap->objects.end(),[](const auto& o){return o.id=="flight.booster";})&&std::any_of(snap->objects.begin(),snap->objects.end(),[](const auto& o){return o.id=="flight";}),"Both vehicles in the scene");}
            {grpc::ClientContext c;api::FlightList l;require(flight->ListFlights(&c,{},&l).ok()&&l.flight_ids_size()==2,"Two flights listed");}
            {grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("booster");require(flight->EndFlight(&c,q,&applied).ok(),"End the second");}
            {grpc::ClientContext c;api::FlightList l;require(flight->ListFlights(&c,{},&l).ok()&&l.flight_ids_size()==1&&l.flight_ids(0)=="flight","One flight left");}
            {grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("no spaces");api::FlightSample x;require(flight->GetFlight(&c,q,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A bad id accepted");}
            // Review regressions: a failed creation leaves no flight behind; non-finite masses and start rates are refused.
            api::VehicleRequest vq;vq.set_vehicle(api::GENERIC_LAUNCHER);api::VehicleDefinition d;{grpc::ClientContext c;require(flight->GetVehicle(&c,vq,&d).ok(),"GetVehicle");}
            api::CreateFlightRequest g=create;g.set_flight_id("ghost");g.set_vehicle(api::CUSTOM);*g.mutable_definition()=d;g.mutable_definition()->mutable_stages(0)->set_mass_empty_kilograms(1);api::FlightSample x;
            {grpc::ClientContext c;require(flight->CreateFlight(&c,g,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"Inconsistent masses accepted");}
            {grpc::ClientContext c;api::FlightList l;require(flight->ListFlights(&c,{},&l).ok()&&l.flight_ids_size()==1,"A failed creation left a flight behind");}
            {grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("ghost");require(flight->EndFlight(&c,q,&applied).error_code()==grpc::StatusCode::FAILED_PRECONDITION,"A ghost flight to end");}
            *g.mutable_definition()=d;g.mutable_definition()->mutable_stages(0)->set_mass_full_kilograms(std::nan(""));
            {grpc::ClientContext c;require(flight->CreateFlight(&c,g,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A NaN mass accepted");}
            api::CreateFlightRequest n=create;n.set_flight_id("ghost");auto* l=n.mutable_start()->mutable_local();l->mutable_position()->set_height_meters(1e4);l->mutable_angular_rate_radians_per_second()->set_x(std::nan(""));
            {grpc::ClientContext c;require(flight->CreateFlight(&c,n,&x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A NaN start rate accepted");}
        }
        { // Spawn: the launcher in flight drops its first stage as flight "stage1" (the NESC 1-slug ball, for the test); the child starts at
          // the parent's instant and velocity, its log replays alone, and replaying the parent creates it again.
            auto exportOf=[&](const std::string& id){grpc::ClientContext c;api::FlightRequest q;q.set_flight_id(id);auto reader=flight->ExportFlight(&c,q);api::FlightLogChunk k;std::vector<api::FlightLogChunk> v;
                while(reader->Read(&k))v.push_back(k);require(reader->Finish().ok(),"ExportFlight");return v;};
            auto replayOf=[&](const std::vector<api::FlightLogChunk>& v,api::FlightSample& out){grpc::ClientContext c;auto writer=flight->ReplayFlight(&c,&out);for(const auto& k:v)writer->Write(k);writer->WritesDone();return writer->Finish();};
            api::CreateFlightRequest a=create;a.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);api::FlightSample p,before,x;
            {grpc::ClientContext c;require(flight->CreateFlight(&c,a,&p).ok(),"Parent flight");}
            {grpc::ClientContext c;api::StepRequest q;q.set_ticks(3000);q.add_commands()->set_ignite(true);require(flight->Step(&c,q,&before).ok(),"Parent in flight");}
            api::VehicleRequest vr;vr.set_vehicle(api::NESC_SPHERE);api::VehicleDefinition ball;{grpc::ClientContext c;require(flight->GetVehicle(&c,vr,&ball).ok(),"Ball definition");}
            auto spawnStep=[&](const std::string& id,api::FlightSample& out){grpc::ClientContext c;api::StepRequest q;auto* sp=q.add_commands()->mutable_spawn();sp->set_flight_id(id);*sp->mutable_definition()=ball;sp->set_nose_offset_meters(40);return flight->Step(&c,q,&out);};
            require(spawnStep("flight",x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A spawn into its own id accepted");
            require(spawnStep("",x).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"A spawn without an id accepted");
            {const auto st=spawnStep("stage1",x);if(!st.ok())std::cerr<<st.error_message()<<"\n";require(st.ok()&&x.stage()==1,"Spawn separates the parent");}
            api::FlightSample kid;{grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("stage1");require(flight->GetFlight(&c,q,&kid).ok()&&kid.tick()==0&&kid.flight_id()=="stage1","The spawned flight");}
            const double dv=std::hypot(kid.velocity_meters_per_second().x()-before.velocity_meters_per_second().x(),kid.velocity_meters_per_second().y()-before.velocity_meters_per_second().y(),kid.velocity_meters_per_second().z()-before.velocity_meters_per_second().z());
            std::printf("spawn: child %.3f s after the parent's start, velocity off by %.4f m/s, mass %.3f kg\n",kid.utc_unix_seconds()-p.utc_unix_seconds(),dv,kid.mass_kilograms());
            require(std::abs(kid.utc_unix_seconds()-before.utc_unix_seconds())<1e-6&&dv<.5&&std::abs(kid.mass_kilograms()-14.5939029)<1e-3,"The child at the parent's instant and velocity");
            require(kid.physics().turbulence_seed()!=before.physics().turbulence_seed(),"The child's gusts reseeded");
            {grpc::ClientContext c;api::StepRequest q;q.set_flight_id("stage1");q.set_ticks(2000);require(flight->Step(&c,q,&kid).ok(),"The child steps alone");}
            {const auto log=exportOf("stage1");api::FlightSample again;require(replayOf(log,again).ok()&&again.state_hash()==kid.state_hash()&&again.flight_id()=="stage1","The child replays alone");}
            {grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("stage1");require(flight->EndFlight(&c,q,&applied).ok(),"End the child");}
            {const auto log=exportOf("flight");api::FlightSample again;require(replayOf(log,again).ok()&&again.stage()==1,"Parent replays with its spawn");
                grpc::ClientContext c;api::FlightList l;require(flight->ListFlights(&c,{},&l).ok()&&l.flight_ids_size()==2,"The replay spawned the child again");}
            {grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("stage1");(void)flight->EndFlight(&c,q,&applied);}
            {grpc::ClientContext c;api::StepRequest q;auto* sp=q.add_commands()->mutable_spawn();sp->set_flight_id("again");*sp->mutable_definition()=ball;require(flight->Step(&c,q,&x).error_code()==grpc::StatusCode::FAILED_PRECONDITION,"A spawn from the last stage accepted");}
        }
        {grpc::ClientContext c;require(flight->EndFlight(&c,{},&applied).ok(),"EndFlight failed");}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {const auto snap=bridge.snapshot();require(std::none_of(snap->objects.begin(),snap->objects.end(),[](const auto& o){return o.id=="flight";}),"Vehicle left in the scene");}
    }
    bridge.close();{grpc::ClientContext c;require(scene->CreateObject(&c,create,&applied).error_code()==grpc::StatusCode::UNAVAILABLE,"Shutdown did not reject commands");}
    std::cout<<"gRPC OK: create/focus/stream/remove, validation, revisions, snapshots, flight and shutdown\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
