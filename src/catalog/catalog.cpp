#include "catalog.hpp"
#include "core/time.hpp"
#include "core/astro_time.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <map>
#include <thread>
namespace albert::catalog {
namespace {
// Reference radii for simplified spheres, not detailed shape/terrain models.
const Entry known[]{
    {"sun","Sol",10,695700000.,0xfff0cc},{"earth","Terra",399,6371008.4,0x81abc2},{"moon","Lua",301,1737400.,0xbbbbb5},
    {"mercury","Mercurio",199,2439400.,0xaaaaa3},{"venus","Venus",299,6051800.,0xdbbe8e},
    {"mars","Marte",499,3389500.,0xc59279},{"jupiter","Jupiter",599,69911000.,0xcfbaa5},
    {"saturn","Saturno",699,58232000.,0xd8c49d},{"uranus","Urano",799,25362000.,0x98c3c8},
    {"neptune","Netuno",899,24622000.,0x6487bb},{"pluto","Plutao",999,1188300.,0xb6aba0}};
bool running(Stage s){return s!=Stage::Failed&&s!=Stage::Removed;}
bool exists(const std::shared_ptr<const core::Snapshot>& snapshot,const std::string& id){return snapshot&&std::any_of(snapshot->objects.begin(),snapshot->objects.end(),[&](const auto& o){return o.id==id;});}
}
std::span<const Entry> entries(){return known;}
const Entry& entry(std::string_view id){for(const auto& e:known)if(e.id==id)return e;throw std::out_of_range("Unknown catalog item");}
const char* stageName(Stage s){switch(s){case Stage::Queued:return "Na fila";case Stage::Loading:return "Buscando posicoes";case Stage::Applying:return "Preparando objeto";case Stage::Tracking:return "Atualizacao automatica";case Stage::Degraded:return "Aguardando a fonte";case Stage::Failed:return "Falha ao importar";case Stage::Removed:return "Objeto removido";}return "";}
struct Manager::Impl {
    struct Record {Import request;Job job;bool created=false;double due=0;unsigned failures=0;};
    core::Bridge& bridge;providers::Horizons& provider;Now utcNow;
    mutable std::mutex mutex;std::condition_variable changed;std::map<std::uint64_t,Record> records;
    std::uint64_t sequence=0;bool closed=false;DownloadProgress progress;std::jthread worker;
    Impl(core::Bridge& b,providers::Horizons& p,Now clock):bridge(b),provider(p),utcNow(std::move(clock)){
        if(!utcNow)utcNow=[](){return time::clock().utcSeconds();};
        worker=std::jthread([this](std::stop_token stop){run(stop);});
    }
    core::Reply apply(core::Command command,std::stop_token stop){
        auto ticket=bridge.submit(std::move(command));const double end=time::monotonicSeconds()+3;
        while(ticket.result.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready){
            if(stop.stop_requested()||time::monotonicSeconds()>end){*ticket.cancelled=true;throw std::runtime_error("Scene did not apply provider command in time");}
        }
        auto reply=ticket.result.get();if(reply.code!=core::Code::Ok)throw std::runtime_error(reply.message);return reply;
    }
    void run(std::stop_token stop) noexcept{try{
        double lastUtc=utcNow();
        while(!stop.stop_requested()){
            Record work;
            {
                std::unique_lock lock(mutex);const auto snapshot=bridge.snapshot();const double now=time::monotonicSeconds(),utc=utcNow();
                if(std::abs(utc-lastUtc)>30.)for(auto& [id,r]:records){(void)id;if(r.created&&running(r.job.stage))r.due=0;}
                lastUtc=utc;
                auto selected=records.end();
                for(auto i=records.begin();i!=records.end();++i){auto& r=i->second;
                    if(!running(r.job.stage))continue;
                    if(r.created&&snapshot&&snapshot->revision>=r.job.revision&&!exists(snapshot,r.job.objectId)){r.job.stage=Stage::Removed;continue;}
                    if(now>=r.due&&(selected==records.end()||r.due<selected->second.due))selected=i;
                }
                if(selected==records.end()){changed.wait_for(lock,std::chrono::milliseconds(200));continue;}
                selected->second.job.stage=Stage::Loading;work=selected->second;
            }
            try{
                const auto& item=entry(work.request.catalogId);const double utc=utcNow();
                if(!work.created&&exists(bridge.snapshot(),work.job.objectId))throw std::invalid_argument("Object ID already exists in the scene");
                const auto table=provider.query({item.horizonsId,"500@399",std::max(0.,utc-60),utc+1800,"5 min"},&progress);
                std::vector<core::Sample> samples;samples.reserve(table.points.size());
                for(const auto& point:table.points)samples.emplace_back(astro::tdbToUtc(point.jdTDB),point.positionMeters,point.velocityMetersPerSecond,std::nullopt,true);
                if(samples.size()>core::Scene::maxSamples)throw std::length_error("Provider history exceeds scene limit");
                {std::lock_guard lock(mutex);records.at(work.job.id).job.stage=Stage::Applying;}
                core::Reply applied;
                if(!work.created){core::Object object;object.id=work.job.objectId;object.name=item.name;object.source="NASA/JPL Horizons target="+std::to_string(item.horizonsId)+"; simplified sphere";object.providerManaged=true;
                    object.appearance={core::Shape::Sphere,{item.radiusMeters*2,item.radiusMeters*2,item.radiusMeters*2},work.request.color.value_or(item.color)};object.samples=std::move(samples);
                    applied=apply(core::Create{std::move(object)},stop);work.created=true;work.job.revision=applied.revision;
                    if(work.request.focus)applied=apply(core::Focus{work.job.objectId,false},stop);
                }else applied=apply(core::Replace{work.job.objectId,std::move(samples)},stop);
                work.job.stage=Stage::Tracking;work.job.error.clear();work.job.fetchedUtc=table.fetchedUtc;work.job.validUntilUtc=astro::tdbToUtc(table.points.back().jdTDB);work.job.revision=applied.revision;work.failures=0;work.due=time::monotonicSeconds()+300;
            }catch(const std::exception& e){work.job.error=e.what();
                const auto snapshot=bridge.snapshot();
                if(work.created&&snapshot&&snapshot->revision>=work.job.revision&&!exists(snapshot,work.job.objectId))work.job.stage=Stage::Removed;
                else work.job.stage=work.created?Stage::Degraded:Stage::Failed;
                work.due=time::monotonicSeconds()+std::min(300.,15.*(1u<<std::min(work.failures++,5u)));
            }
            {std::lock_guard lock(mutex);records.at(work.job.id)=std::move(work);}
        }
    }catch(const std::exception& e){std::lock_guard lock(mutex);closed=true;for(auto& [id,r]:records){(void)id;if(running(r.job.stage)){r.job.stage=Stage::Failed;r.job.error=e.what();}}}
    }
};
Manager::Manager(core::Bridge& b,providers::Horizons& p,Now now):impl_(std::make_unique<Impl>(b,p,std::move(now))){}
Manager::~Manager(){close();}
void Manager::close(){if(!impl_)return;{std::lock_guard lock(impl_->mutex);impl_->closed=true;impl_->progress.cancelled=true;}impl_->worker.request_stop();impl_->changed.notify_all();if(impl_->worker.joinable())impl_->worker.join();}
std::uint64_t Manager::importObject(Import r){
    const auto& item=entry(r.catalogId);if(r.objectId.empty())r.objectId=item.id;
    if(!core::validObjectId(r.objectId)||(r.color&&*r.color>0xffffff))throw std::invalid_argument("Invalid catalog presentation");
    std::lock_guard lock(impl_->mutex);if(impl_->closed)throw std::runtime_error("Catalog is shutting down");
    size_t active=0,queued=0;
    for(const auto& [id,record]:impl_->records){(void)id;if(running(record.job.stage)){++active;if(record.job.stage==Stage::Queued)++queued;if(record.job.objectId==r.objectId)throw std::invalid_argument("Object already has a catalog operation");}}
    if(active>=16||queued>=8)throw std::length_error("Catalog operation limit reached");
    if(impl_->records.size()>=64){const auto old=std::find_if(impl_->records.begin(),impl_->records.end(),[](const auto& pair){return !running(pair.second.job.stage);});if(old!=impl_->records.end())impl_->records.erase(old);}
    const auto id=++impl_->sequence;Job job{id,r.catalogId,r.objectId,Stage::Queued,{},0,0,0};impl_->records.emplace(id,Impl::Record{std::move(r),std::move(job),false,time::monotonicSeconds(),0});impl_->changed.notify_all();return id;
}
std::vector<Job> Manager::jobs()const{std::lock_guard lock(impl_->mutex);std::vector<Job> result;for(const auto& [id,r]:impl_->records){(void)id;result.push_back(r.job);}return result;}
Job Manager::job(std::uint64_t id)const{std::lock_guard lock(impl_->mutex);return impl_->records.at(id).job;}
bool Manager::active()const{std::lock_guard lock(impl_->mutex);return std::any_of(impl_->records.begin(),impl_->records.end(),[](const auto& r){return running(r.second.job.stage);});}
}

namespace albert::catalog {
std::uint64_t Manager::retry(std::uint64_t id){Import request;{std::lock_guard lock(impl_->mutex);request=impl_->records.at(id).request;}return importObject(std::move(request));}
bool Manager::busy()const{std::lock_guard lock(impl_->mutex);return std::any_of(impl_->records.begin(),impl_->records.end(),[](const auto& r){return r.second.job.stage==Stage::Queued||r.second.job.stage==Stage::Loading||r.second.job.stage==Stage::Applying;});}
}
