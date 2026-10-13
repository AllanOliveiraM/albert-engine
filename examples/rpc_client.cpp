#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace api=albert::v1;
void check(const grpc::Status& status){if(!status.ok())throw std::runtime_error(status.error_message());}
int main(int argc,char** argv){try{
    const std::string address=argc>1 ? argv[1] : "127.0.0.1:50051";
    auto channel=grpc::CreateChannel(address,grpc::InsecureChannelCredentials());
    if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(30)))throw std::runtime_error("Abra AlbertEngine --rpc antes de executar o exemplo");
    auto scene=api::SceneService::NewStub(channel);auto camera=api::CameraService::NewStub(channel);
    api::Applied applied;
    if(argc>2 && std::string(argv[2])=="--remove"){grpc::ClientContext c;api::ObjectRequest r;r.set_id("demo-flight");check(scene->RemoveObject(&c,r,&applied));return 0;}
    api::ClockStatus timing;
    {grpc::ClientContext c;auto time=api::TimeService::NewStub(channel);check(time->GetTime(&c,{},&timing));}
    const auto start=std::chrono::steady_clock::now();
    const double utc=timing.utc_unix_seconds();
    std::cout<<"Tempo Albert: "<<timing.source()<<", qualidade "<<timing.quality()<<", incerteza "<<timing.uncertainty_seconds()<<" s\n";
    if(argc>2&&std::string(argv[2])=="--time")return 0;
    api::CreateObjectRequest create;create.set_id("demo-flight");create.set_name("Voo de exemplo (simulado)");create.set_source("Synthetic demonstration; not Starship telemetry");
    auto* g=create.mutable_geometry();g->set_shape(api::Geometry::CYLINDER);g->set_color_rgb(0xcbd5dd);g->mutable_size_meters()->set_x(9);g->mutable_size_meters()->set_y(120);g->mutable_size_meters()->set_z(9);
    auto* state=create.mutable_initial_state();state->set_frame(api::GEOCENTRIC_ICRF);state->set_utc_unix_seconds(utc);state->mutable_position_meters()->set_x(6878137.);state->mutable_attitude()->set_w(1);
    {grpc::ClientContext c;c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));check(scene->CreateObject(&c,create,&applied));}
    {grpc::ClientContext c;api::FocusRequest focus;focus.set_id(create.id());check(camera->Focus(&c,focus,&applied));}
    grpc::ClientContext context;context.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(20));api::StreamSummary summary;
    auto stream=scene->StreamStates(&context,&summary);
    for(int i=1;i<=300;++i){
        std::this_thread::sleep_until(start+std::chrono::milliseconds(i*33));
        const double t=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()+.1;
        api::StateBatch batch;batch.set_id(create.id());auto* s=batch.add_samples();s->set_frame(api::GEOCENTRIC_ICRF);s->set_utc_unix_seconds(utc+t);
        s->mutable_position_meters()->set_x(6878137.+t*50);s->mutable_position_meters()->set_y(t*200);
        s->mutable_velocity_meters_per_second()->set_x(50);s->mutable_velocity_meters_per_second()->set_y(200);
        s->mutable_attitude()->set_z(std::sin(t*.03));s->mutable_attitude()->set_w(std::cos(t*.03));
        if(!stream->Write(batch))break;
    }
    stream->WritesDone();check(stream->Finish());
    std::cout<<summary.applied_batches()<<" lotes aplicados; ultima posicao mantida. Use --remove para remover o exemplo.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
