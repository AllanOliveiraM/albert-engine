#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <iostream>
#include <thread>
namespace api=albert::v1;
void check(const grpc::Status& s){if(!s.ok())throw std::runtime_error(s.error_message());}
int main(int argc,char** argv){try{
    const std::string address=argc>1?argv[1]:"127.0.0.1:50051";
    auto channel=grpc::CreateChannel(address,grpc::InsecureChannelCredentials());
    if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(5)))throw std::runtime_error("Abra AlbertEngine --rpc antes de executar o exemplo");
    auto catalog=api::CatalogService::NewStub(channel);
    if(argc<3){grpc::ClientContext c;api::CatalogList result;check(catalog->ListObjects(&c,{},&result));for(const auto& item:result.items())std::cout<<item.id()<<" — "<<item.name()<<(item.present_in_scene()?" (na cena)":" (esfera simplificada)")<<'\n';return 0;}
    if(std::string(argv[2])=="--jobs"){grpc::ClientContext c;api::CatalogJobs result;check(catalog->GetJobs(&c,{},&result));for(const auto& job:result.jobs())std::cout<<job.id()<<" "<<job.object_id()<<" stage="<<job.stage()<<" "<<job.error()<<'\n';return 0;}
    if(std::string(argv[2])=="--remove"&&argc>3){auto scene=api::SceneService::NewStub(channel);grpc::ClientContext c;api::ObjectRequest request;request.set_id(argv[3]);api::Applied applied;check(scene->RemoveObject(&c,request,&applied));return 0;}
    {grpc::ClientContext c;api::CatalogList list;api::CatalogQuery q;q.set_query(argv[2]);check(catalog->ListObjects(&c,q,&list));
        for(const auto& item:list.items())if(item.id()==argv[2]&&item.present_in_scene()){auto camera=api::CameraService::NewStub(channel);grpc::ClientContext context;api::FocusRequest focus;focus.set_id(item.id());api::Applied applied;check(camera->Focus(&context,focus,&applied));std::cout<<"Objeto existente focado: "<<item.id()<<'\n';return 0;}}
    api::CatalogJob job;{grpc::ClientContext c;api::ImportCatalogObjectRequest request;request.set_catalog_id(argv[2]);request.set_focus_camera(true);check(catalog->ImportObject(&c,request,&job));}
    std::cout<<"Importacao "<<job.id()<<" iniciada; consultas ocorrem dentro do Albert.\n";
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(std::chrono::steady_clock::now()<end){grpc::ClientContext c;api::CatalogJobRequest request;request.set_id(job.id());check(catalog->GetJob(&c,request,&job));
        if(job.stage()==api::CatalogJob::TRACKING){std::cout<<"Objeto "<<job.object_id()<<" aplicado, revisao "<<job.applied_revision()<<"; atualizacao automatica ativa. Visual: esfera simplificada.\n";return 0;}
        if(job.stage()==api::CatalogJob::FAILED||job.stage()==api::CatalogJob::REMOVED)throw std::runtime_error(job.error());std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    throw std::runtime_error("A operacao continua no Albert; consulte --jobs");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
