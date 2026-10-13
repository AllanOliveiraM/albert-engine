#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <fstream>
#include <cmath>
#include <iostream>
namespace api=albert::v1;
struct Context:grpc::ClientContext {Context(){set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(10));}};
void check(const grpc::Status& s){if(!s.ok())throw std::runtime_error(s.error_message());}
int main(int argc,char** argv){try{
    if(argc<2)throw std::runtime_error("Usage: albert_model_client model.glb [127.0.0.1:50051] [meters_per_model_unit]");
    const bool remove=std::string(argv[1])=="--remove";
    const double scale=argc>3?std::stod(argv[3]):1.;if(!std::isfinite(scale)||scale<=0)throw std::runtime_error("Invalid physical scale");
    std::string data;
    if(!remove){std::ifstream input(argv[1],std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Cannot open GLB");const auto size=input.tellg();if(size<=0||size>768*1024)throw std::runtime_error("GLB must be at most 768 KiB");data.resize(static_cast<size_t>(size));input.seekg(0);if(!input.read(data.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("Cannot read complete GLB");}
    auto channel=grpc::CreateChannel(argc>2?argv[2]:"127.0.0.1:50051",grpc::InsecureChannelCredentials());if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(5)))throw std::runtime_error("Start AlbertEngine --rpc first");
    auto assets=api::AssetService::NewStub(channel);auto scene=api::SceneService::NewStub(channel);auto camera=api::CameraService::NewStub(channel);auto clock=api::TimeService::NewStub(channel);api::Applied applied;api::ClockStatus time;
    if(remove){api::ObjectRequest request;request.set_id("example-vehicle");{Context c;const auto s=scene->RemoveObject(&c,request,&applied);if(s.error_code()!=grpc::StatusCode::NOT_FOUND)check(s);}request.set_id("example-model");{Context c;const auto s=assets->RemoveModel(&c,request,&applied);if(s.error_code()!=grpc::StatusCode::NOT_FOUND)check(s);}std::cout<<"Example object and unused model removed.\n";return 0;}
    {Context c;check(clock->GetTime(&c,{},&time));}
    {Context c;api::RegisterModelRequest model;model.set_id("example-model");model.set_source("External static GLB demonstration");model.set_glb(std::move(data));check(assets->RegisterModel(&c,model,&applied));}
    {Context c;api::CreateObjectRequest object;object.set_id("example-vehicle");object.set_name("Modelo externo (demonstracao)");object.set_source("Synthetic fixed position, not mission telemetry");auto* g=object.mutable_geometry();g->set_shape(api::Geometry::MODEL);g->set_model_id("example-model");g->set_meters_per_model_unit(scale);auto* state=object.mutable_initial_state();state->set_frame(api::GEOCENTRIC_ICRF);state->set_utc_unix_seconds(time.utc_unix_seconds());state->mutable_position_meters()->set_x(10000000);state->mutable_attitude()->set_w(1);check(scene->CreateObject(&c,object,&applied));}
    {Context c;api::FocusRequest focus;focus.set_id("example-vehicle");check(camera->Focus(&c,focus,&applied));}
    std::cout<<"Model registered and focused. Position is synthetic; camera orbit/zoom and state streaming remain available.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
