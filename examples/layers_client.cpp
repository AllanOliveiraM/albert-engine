#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <iostream>
namespace api=albert::v1;
struct Context:grpc::ClientContext{Context(){set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(10));}};
void check(const grpc::Status& status){if(!status.ok())throw std::runtime_error(status.error_message());}
int main(int argc,char** argv){try{
    auto channel=grpc::CreateChannel(argc>1?argv[1]:"127.0.0.1:50051",grpc::InsecureChannelCredentials());if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(5)))throw std::runtime_error("Start AlbertEngine --rpc first");auto service=api::LayerService::NewStub(channel);api::Applied applied;
    if(argc>2&&std::string(argv[2])=="--remove"){for(const char* id:{"example-places","example-line"}){Context c;api::ObjectRequest request;request.set_id(id);const auto status=service->RemoveLayer(&c,request,&applied);if(status.error_code()!=grpc::StatusCode::NOT_FOUND)check(status);}return 0;}
    auto request=[](const char* id,const char* name,api::CreateLayerRequest::Kind kind){api::CreateLayerRequest r;r.set_id(id);r.set_name(name);r.set_source("Coordenadas ilustrativas; nao representam rota de navegacao");r.set_frame(api::CreateLayerRequest::EARTH_SURFACE);r.set_kind(kind);return r;};
    auto places=request("example-places","Lugares de exemplo",api::CreateLayerRequest::POINTS);places.set_width_pixels(12);places.set_color_rgb(0x72e5ee);
    for(const auto& p:{std::pair{-46.6333,-23.5505},std::pair{-43.1729,-22.9068},std::pair{-47.8828,-15.7939}}){auto* point=places.add_geographic_points();point->set_longitude_degrees(p.first);point->set_latitude_degrees(p.second);}
    {Context c;check(service->CreateLayer(&c,places,&applied));}
    auto line=request("example-line","Linha ilustrativa",api::CreateLayerRequest::POLYLINE);line.set_width_pixels(3);line.set_color_rgb(0xffcc88);for(const auto& p:places.geographic_points())line.add_geographic_points()->CopyFrom(p);
    {Context c;check(service->CreateLayer(&c,line,&applied));}
    auto camera=api::CameraService::NewStub(channel);{Context c;api::FocusRequest focus;focus.set_id("earth");check(camera->Focus(&c,focus,&applied));}
    std::cout<<"Earth surface points/line created. Control visibility/removal in Cena > Camadas. Coordinates are illustrative.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
