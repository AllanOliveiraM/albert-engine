#include "catalog/catalog.hpp"
#include <atomic>
#include <algorithm>
#include "core/time.hpp"
#include <iostream>
#include <thread>
#ifdef ALBERT_TEST_RPC
#include "rpc/server.hpp"
#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
namespace api=albert::v1;
#endif
using namespace albert;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    constexpr double utc=946728000.;
    std::atomic<double> now=utc;std::atomic_bool failRequests=false;
    providers::Horizons provider([&](const std::string&,DownloadProgress*){if(failRequests)throw std::runtime_error("fixture network unavailable");return "$$SOE\n2451544.9,date,100000000,2,3,4,5,6,\n2451545.1,date,100000010,4,6,4,5,6,\n$$EOE\n";},[&]{return now.load();});
    core::Bridge bridge;core::Scene registry;std::atomic_bool holdSnapshot=true;
    bridge.publish(registry.snapshot(utc,{}));
    std::jthread owner([&](std::stop_token stop){core::CameraState camera;
        while(!stop.stop_requested()){
            bridge.drain([&](core::Command& command){std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,core::Create>)registry.create(std::move(value.object));
                else if constexpr(std::is_same_v<T,core::Replace>)registry.replace(value.id,std::move(value.values));
                else if constexpr(std::is_same_v<T,core::Remove>){registry.remove(value.id);if(camera.target==value.id)camera.target="earth";}
                else if constexpr(std::is_same_v<T,core::Append>)registry.samples(value.id,std::move(value.values));
                else if constexpr(std::is_same_v<T,core::Focus>){registry.get(value.id);camera.target=value.id;registry.cameraChanged();}
            },command);return core::Reply{core::Code::Ok,{},registry.revision()};});
            if(!holdSnapshot)bridge.publish(registry.snapshot(now.load(),camera));std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    catalog::Manager manager(bridge,provider,[&]{return now.load();});
    const auto id=manager.importObject({"mars",{},true,{}});
    auto await=[&](std::uint64_t job,catalog::Stage expected){const double end=time::monotonicSeconds()+3.;catalog::Job result;
        do{result=manager.job(job);if(result.stage==expected)return result;if(result.stage==catalog::Stage::Failed)throw std::runtime_error(result.error);std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(time::monotonicSeconds()<end);
        throw std::runtime_error("Catalog stage timeout");};
    auto job=await(id,catalog::Stage::Tracking);require(job.revision>0&&job.fetchedUtc==utc&&job.validUntilUtc>utc,"Catalog source/revision/validity missing");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));require(manager.job(id).stage==catalog::Stage::Tracking,"Snapshot lag was mistaken for object removal");holdSnapshot=false;
    const auto deadline=time::monotonicSeconds()+1;while(bridge.snapshot()->revision<job.revision&&time::monotonicSeconds()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto snapshot=bridge.snapshot();const auto object=std::find_if(snapshot->objects.begin(),snapshot->objects.end(),[](const auto& o){return o.id=="mars";});
    require(object!=snapshot->objects.end()&&object->providerManaged&&snapshot->camera.target=="mars","Import did not share the actual scene/camera");
    now=utc+3600;
    {const double end=time::monotonicSeconds()+3.;while(manager.job(id).fetchedUtc<utc+3600&&time::monotonicSeconds()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));require(manager.job(id).fetchedUtc==utc+3600,"Clock/suspension change did not refresh provider state");}
    failRequests=true;now=utc+7200;await(id,catalog::Stage::Degraded);
    require(!manager.job(id).error.empty(),"Provider failure was hidden");
    failRequests=false;now=utc+3601;await(id,catalog::Stage::Tracking);job=manager.job(id);
    auto reject=bridge.submit(core::Append{"mars",{{utc+1000,{}}}});require(reject.result.get().code==core::Code::Invalid,"External stream overwrote provider ownership");
#ifdef ALBERT_TEST_RPC
    rpc::Server server(bridge,0,&manager);auto channel=grpc::CreateChannel("127.0.0.1:"+std::to_string(server.port()),grpc::InsecureChannelCredentials());
    require(channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(2)),"Catalog RPC startup failed");auto client=api::CatalogService::NewStub(channel);
    {grpc::ClientContext context;api::CatalogQuery q;q.set_query("MARTE");api::CatalogList list;require(client->ListObjects(&context,q,&list).ok()&&list.items_size()==1&&list.items(0).present_in_scene(),"Catalog discovery/presence RPC failed");}
    {grpc::ClientContext context;api::ImportCatalogObjectRequest request;request.set_catalog_id("venus");api::CatalogJob response;require(client->ImportObject(&context,request,&response).ok(),"Catalog import RPC failed");await(response.id(),catalog::Stage::Tracking);}
    {grpc::ClientContext context;api::CatalogJobRequest request;request.set_id(id);api::CatalogJob response;require(client->GetJob(&context,request,&response).ok()&&response.stage()==api::CatalogJob::TRACKING&&response.applied_revision()==job.revision,"Catalog job RPC lost application state");}
    {grpc::ClientContext context;api::CatalogJobRequest request;request.set_id(999999);api::CatalogJob response;require(client->GetJob(&context,request,&response).error_code()==grpc::StatusCode::NOT_FOUND,"Unknown catalog job accepted");}
#endif
    auto removal=bridge.submit(core::Remove{"mars"});require(removal.result.get().code==core::Code::Ok,"Imported object removal failed");await(id,catalog::Stage::Removed);
    const auto retry=manager.retry(id);await(retry,catalog::Stage::Tracking);
    manager.close();bridge.close();std::cout<<"Catalog OK: asynchronous import, owner application, lagging snapshots, source ownership, automatic removal detection, retry and RPC\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
