#include "rpc/server.hpp"
#include "model_fixture.hpp"
#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
namespace api=albert::v1;using namespace albert::core;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{
    Scene registry;Bridge bridge;
    std::jthread owner([&](std::stop_token stop){CameraState camera;
        while(!stop.stop_requested()){
            bridge.drain([&](Command& command){std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,Create>)registry.create(std::move(value.object));
                else if constexpr(std::is_same_v<T,Remove>){registry.remove(value.id);if(camera.target==value.id)camera.target="earth";}
                else if constexpr(std::is_same_v<T,InstallModel>)registry.installModel(std::move(value.model));
                else if constexpr(std::is_same_v<T,RemoveModel>)registry.removeModel(value.id);
                else if constexpr(std::is_same_v<T,Visibility>)registry.visible(value.id,value.enabled);
                else if constexpr(std::is_same_v<T,Append>)registry.samples(value.id,std::move(value.values));
                else if constexpr(std::is_same_v<T,Focus>){registry.get(value.id);camera.target=value.id;registry.cameraChanged();}
            },command);return Reply{Code::Ok,{},registry.revision()};});
            auto snapshot=registry.snapshot(100.5,camera);snapshot.clock.utcSeconds=100.5;snapshot.clock.quality=albert::time::Quality::Holdover;
            snapshot.clock.source="fixture-one, fixture-two";snapshot.clock.uncertaintySeconds=.03;snapshot.clock.generation=2;
            bridge.publish(std::move(snapshot));std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    albert::rpc::Server server(bridge,0);
    auto channel=grpc::CreateChannel("127.0.0.1:"+std::to_string(server.port()),grpc::InsecureChannelCredentials());
    require(channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(3)),"Server did not start");
    auto scene=api::SceneService::NewStub(channel);auto camera=api::CameraService::NewStub(channel);api::Applied applied;
    {auto time=api::TimeService::NewStub(channel);grpc::ClientContext c;api::ClockStatus timing;
        require(time->GetTime(&c,{},&timing).ok(),"Clock RPC failed");
        require(timing.quality()==api::ClockStatus::HOLDOVER&&timing.utc_unix_seconds()==100.5&&!timing.authenticated()&&timing.generation()==2,"Clock quality/provenance lost in RPC");
        grpc::ClientContext cs;api::Scene snapshot;require(scene->GetScene(&cs,{},&snapshot).ok(),"Scene snapshot failed");
        require(snapshot.has_clock()&&snapshot.clock().utc_unix_seconds()==snapshot.utc_unix_seconds(),"Scene and clock snapshots disagree");}
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
    bridge.close();{grpc::ClientContext c;require(scene->CreateObject(&c,create,&applied).error_code()==grpc::StatusCode::UNAVAILABLE,"Shutdown did not reject commands");}
    std::cout<<"gRPC OK: create/focus/stream/remove, validation, revisions, snapshots and shutdown\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
