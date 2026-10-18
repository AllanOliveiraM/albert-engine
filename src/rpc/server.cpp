#include "server.hpp"
#include "flight.hpp"
#include "core/model.hpp"
#include "core/layers.hpp"
#include "catalog/catalog.hpp"
#include <cctype>
#include <algorithm>
#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <thread>
#include <deque>
#include <stdexcept>
namespace albert::rpc {
namespace {
namespace api=::albert::v1;
core::Vector vector(const api::Vector3& v){return {v.x(),v.y(),v.z()};}
void vector(core::Vector v,api::Vector3* out){out->set_x(v.x);out->set_y(v.y);out->set_z(v.z);}
core::Sample sample(const api::State& v){
    if(v.frame()!=api::GEOCENTRIC_ICRF)throw std::invalid_argument("Only geocentric ICRF is supported");
    if(!v.has_position_meters())throw std::invalid_argument("Position is required");
    core::Sample s;s.utcSeconds=v.utc_unix_seconds();s.positionMeters=vector(v.position_meters());
    s.hasVelocity=v.has_velocity_meters_per_second();if(s.hasVelocity)s.velocityMetersPerSecond=vector(v.velocity_meters_per_second());
    if(v.has_attitude()){const auto& q=v.attitude();s.attitude=core::Quaternion{q.x(),q.y(),q.z(),q.w()};}core::validate(s);return s;
}
void sample(const core::Sample& v,api::State* out){
    out->set_utc_unix_seconds(v.utcSeconds);out->set_frame(api::GEOCENTRIC_ICRF);vector(v.positionMeters,out->mutable_position_meters());
    if(v.hasVelocity)vector(v.velocityMetersPerSecond,out->mutable_velocity_meters_per_second());
    if(v.attitude){auto* q=out->mutable_attitude();q->set_x(v.attitude->x);q->set_y(v.attitude->y);q->set_z(v.attitude->z);q->set_w(v.attitude->w);}
}
void camera(const core::CameraState& v,api::Camera* out){out->set_target_id(v.target);vector(v.positionMeters,out->mutable_position_meters());out->set_distance_meters(v.distanceMeters);out->set_transitioning(v.transitioning);}
core::Append append(const api::StateBatch& r){
    if(r.samples_size()<1||r.samples_size()>static_cast<int>(core::Scene::maxSamples))throw std::invalid_argument("Invalid batch size");
    core::Append command;command.id=r.id();for(const auto& s:r.samples())command.values.push_back(sample(s));return command;
}
grpc::Status status(const core::Reply& r){
    using S=grpc::StatusCode;S code=S::OK;
    switch(r.code){case core::Code::Ok:break;case core::Code::Invalid:code=S::INVALID_ARGUMENT;break;case core::Code::NotFound:code=S::NOT_FOUND;break;case core::Code::Exhausted:code=S::RESOURCE_EXHAUSTED;break;case core::Code::Unavailable:code=S::UNAVAILABLE;break;case core::Code::Cancelled:code=S::CANCELLED;break;}
    return {code,r.message};
}
void clockStatus(const time::Status& s,api::ClockStatus* out){
    out->set_quality(static_cast<api::ClockStatus::Quality>(s.quality));out->set_utc_unix_seconds(s.utcSeconds);
    out->set_uncertainty_seconds(s.uncertaintySeconds);out->set_last_sync_utc_unix_seconds(s.lastSyncUtc);
    out->set_age_seconds(s.ageSeconds);out->set_round_trip_seconds(s.delaySeconds);out->set_source(s.source);
    out->set_authenticated(s.authenticated);out->set_synchronizing(s.synchronizing);out->set_error(s.error);
    out->set_generation(s.generation);out->set_pending_correction_seconds(s.correctionSeconds);
}
void simulation(const core::Snapshot& snapshot,api::SimulationTime* out){
    const auto& s=snapshot.simulation;out->set_utc_unix_seconds(s.utcSeconds);out->set_rate(s.rate);out->set_live(s.live);out->set_limited(s.limited);out->set_generation(s.generation);
    out->set_approximate_positions(snapshot.approximatePositions);
}
class Services final:public api::SceneService::Service,public api::CameraService::Service,public api::TimeService::Service,public api::CatalogService::Service,public api::AssetService::Service {
public:
    explicit Services(core::Bridge& bridge,catalog::Manager* catalog):bridge_(bridge),catalog_(catalog){}
    grpc::Status apply(grpc::ServerContext* context,core::Command command,api::Applied* out){
        auto ticket=bridge_.submit(std::move(command));return await(context,ticket,out);
    }
    grpc::Status await(grpc::ServerContext* context,core::Ticket& ticket,api::Applied* out){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(ticket.result.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready){
            if(context->IsCancelled()||std::chrono::steady_clock::now()>=deadline){*ticket.cancelled=true;return {context->IsCancelled()?grpc::StatusCode::CANCELLED:grpc::StatusCode::DEADLINE_EXCEEDED,"Application did not apply command in time"};}
        }
        auto reply=ticket.result.get();out->set_revision(reply.revision);return status(reply);
    }
    grpc::Status GetScene(grpc::ServerContext*,const api::Empty*,api::Scene* out) override{
        const auto s=bridge_.snapshot();if(!s)return {grpc::StatusCode::UNAVAILABLE,"Scene is initializing"};
        clockStatus(s->clock,out->mutable_clock());simulation(*s,out->mutable_clock()->mutable_simulation());
        out->set_revision(s->revision);out->set_utc_unix_seconds(s->utcSeconds);camera(s->camera,out->mutable_camera());
        for(const auto& o:s->objects){auto* result=out->add_objects();result->set_id(o.id);result->set_name(o.name);result->set_source(o.source);result->set_builtin(o.builtin);result->set_visible(o.visible);result->set_radius_meters(o.radiusMeters);result->set_provider_managed(o.providerManaged);
            auto* g=result->mutable_geometry();g->set_shape(static_cast<api::Geometry::Shape>(o.appearance.shape));vector(o.appearance.sizeMeters,g->mutable_size_meters());g->set_color_rgb(o.appearance.color);g->set_model_id(o.appearance.modelId);if(o.appearance.shape==core::Shape::Model)g->set_meters_per_model_unit(o.appearance.metersPerUnit);
            if(!o.samples.empty()){sample(o.samples.back(),result->mutable_state());result->set_stale(s->utcSeconds<o.samples.back().utcSeconds||s->utcSeconds-o.samples.back().utcSeconds>1.);}}
        return grpc::Status::OK;
    }
    grpc::Status GetTime(grpc::ServerContext*,const api::Empty*,api::ClockStatus* out) override{
        const auto s=bridge_.snapshot();if(!s)return {grpc::StatusCode::UNAVAILABLE,"Scene is initializing"};
        clockStatus(s->clock,out);simulation(*s,out->mutable_simulation());return grpc::Status::OK;
    }
    grpc::Status SetTime(grpc::ServerContext* c,const api::SetTimeRequest* r,api::Applied* out) override{
        core::SetTime command;command.live=r->live();
        if(r->has_utc_unix_seconds())command.utcSeconds=r->utc_unix_seconds();
        if(r->has_rate())command.rate=r->rate();
        if(!command.live&&!command.utcSeconds&&!command.rate)return {grpc::StatusCode::INVALID_ARGUMENT,"Nothing to change"};
        return apply(c,std::move(command),out);
    }
    grpc::Status GetCapabilities(grpc::ServerContext*,const api::Empty*,api::Capabilities* out) override{
        out->set_catalog_available(catalog_!=nullptr);out->set_max_model_bytes(core::maxModelBytes);out->set_max_layers(core::Scene::maxLayers);out->set_max_layer_input_points(core::Scene::maxLayerInputPoints);out->add_frames(api::GEOCENTRIC_ICRF);out->set_max_objects(core::Scene::maxObjects);out->set_max_samples_per_batch(core::Scene::maxSamples);
        for(auto shape:{api::Geometry::SPHERE,api::Geometry::BOX,api::Geometry::CYLINDER,api::Geometry::MODEL})out->add_shapes(shape);return grpc::Status::OK;
    }
    grpc::Status CreateObject(grpc::ServerContext* c,const api::CreateObjectRequest* r,api::Applied* out) override{
        try{core::Object o;o.id=r->id();o.name=r->name();o.source=r->source();const auto& g=r->geometry();
            if(g.shape()<api::Geometry::SPHERE||g.shape()>api::Geometry::MODEL)throw std::invalid_argument("Unsupported geometry");
            o.appearance={static_cast<core::Shape>(g.shape()),vector(g.size_meters()),g.has_color_rgb()?g.color_rgb():0x77cddd};o.appearance.modelId=g.model_id();o.appearance.metersPerUnit=g.has_meters_per_model_unit()?g.meters_per_model_unit():1.;o.samples.push_back(sample(r->initial_state()));return apply(c,core::Create{std::move(o)},out);
        }catch(const std::exception& e){return {grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    }
    grpc::Status CreateLayer(grpc::ServerContext* c,const api::CreateLayerRequest* r,api::Applied* out){
        try{if(r->kind()<api::CreateLayerRequest::POINTS||r->kind()>api::CreateLayerRequest::POLYLINE||r->frame()<api::CreateLayerRequest::GEOCENTRIC_ICRF||r->frame()>api::CreateLayerRequest::EARTH_SURFACE||r->positions_meters_size()>static_cast<int>(core::Scene::maxLayerInputPoints)||r->geographic_points_size()>static_cast<int>(core::Scene::maxLayerInputPoints))throw std::invalid_argument("Invalid layer kind/frame/count");
            if(r->has_clearance_meters()&&r->frame()!=api::CreateLayerRequest::EARTH_SURFACE)throw std::invalid_argument("Terrain clearance requires Earth surface frame");
            core::Layer l;l.id=r->id();l.name=r->name();l.source=r->source();l.anchor=r->anchor_id();l.kind=static_cast<core::LayerKind>(r->kind()-1);l.frame=static_cast<core::LayerFrame>(r->frame()-1);l.color=r->has_color_rgb()?r->color_rgb():0x66d9ee;l.widthPixels=r->has_width_pixels()?r->width_pixels():(l.kind==core::LayerKind::Points?8.f:3.f);l.clearanceMeters=r->has_clearance_meters()?r->clearance_meters():100.;
            std::vector<core::Vector> positions;std::vector<core::Geographic> geographic;for(const auto& p:r->positions_meters())positions.push_back(vector(p));for(const auto& p:r->geographic_points())geographic.push_back({p.longitude_degrees(),p.latitude_degrees()});return apply(c,core::CreateLayer{core::prepareLayer(std::move(l),positions,geographic)} ,out);
        }catch(const std::length_error& e){return {grpc::StatusCode::RESOURCE_EXHAUSTED,e.what()};}catch(const std::exception& e){return {grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    }
    grpc::Status ListLayers(grpc::ServerContext*,const api::Empty*,api::Layers* out){
        const auto s=bridge_.snapshot();if(!s)return {grpc::StatusCode::UNAVAILABLE,"Scene is initializing"};for(const auto& l:s->layers){auto* v=out->add_layers();v->set_id(l.id);v->set_name(l.name);v->set_source(l.source);v->set_anchor_id(l.anchor);v->set_kind(static_cast<api::CreateLayerRequest::Kind>(int(l.kind)+1));v->set_frame(static_cast<api::CreateLayerRequest::Frame>(int(l.frame)+1));v->set_point_count(l.points);v->set_color_rgb(l.color);v->set_width_pixels(l.widthPixels);v->set_clearance_meters(l.clearanceMeters);v->set_visible(l.visible);}return grpc::Status::OK;
    }
    grpc::Status RemoveLayer(grpc::ServerContext* c,const api::ObjectRequest* r,api::Applied* out){return apply(c,core::RemoveLayer{r->id()},out);}
    grpc::Status RegisterModel(grpc::ServerContext* c,const api::RegisterModelRequest* r,api::Applied* out) override{
        try{const auto& bytes=r->glb();auto model=core::decodeModel(r->id(),r->source(),{reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size()});return apply(c,core::InstallModel{std::move(model)},out);}
        catch(const std::length_error& e){return {grpc::StatusCode::RESOURCE_EXHAUSTED,e.what()};}catch(const std::exception& e){return {grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    }
    grpc::Status RemoveModel(grpc::ServerContext* c,const api::ObjectRequest* r,api::Applied* out) override{return apply(c,core::RemoveModel{r->id()},out);}
    grpc::Status ListModels(grpc::ServerContext*,const api::Empty*,api::ModelAssets* out) override{
        const auto s=bridge_.snapshot();if(!s)return {grpc::StatusCode::UNAVAILABLE,"Scene is initializing"};for(const auto& model:s->models){auto* v=out->add_assets();v->set_id(model.id);v->set_source(model.source);v->set_bytes(model.bytes);v->set_vertices(model.vertices);v->set_triangles(model.triangles);vector(model.size,v->mutable_size_model_units());v->set_radius_model_units(model.radius);}return grpc::Status::OK;
    }
    grpc::Status RemoveObject(grpc::ServerContext* c,const api::ObjectRequest* r,api::Applied* out) override{return apply(c,core::Remove{r->id()},out);}
    grpc::Status SetVisibility(grpc::ServerContext* c,const api::VisibilityRequest* r,api::Applied* out) override{return apply(c,core::Visibility{r->id(),r->visible()},out);}
    grpc::Status PushStates(grpc::ServerContext* c,const api::StateBatch* r,api::Applied* out) override{
        try{return apply(c,append(*r),out);
        }catch(const std::exception& e){return {grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    }
    grpc::Status StreamStates(grpc::ServerContext* c,grpc::ServerReader<api::StateBatch>* reader,api::StreamSummary* out) override{
        api::StateBatch batch;api::Applied applied;std::deque<core::Ticket> pending;
        struct CancelPending {std::deque<core::Ticket>& tickets;~CancelPending(){for(auto& t:tickets)*t.cancelled=true;}} guard{pending};
        auto consume=[&](){auto result=await(c,pending.front(),&applied);pending.pop_front();if(result.ok()){out->set_applied_batches(out->applied_batches()+1);out->set_revision(applied.revision());}return result;};
        try{
            while(reader->Read(&batch)){
                pending.push_back(bridge_.submit(append(batch)));
                while(!pending.empty()&&(pending.size()>=32||pending.front().result.wait_for(std::chrono::seconds(0))==std::future_status::ready)){
                    auto result=consume();if(!result.ok())return result;
                }
            }
            while(!pending.empty()){auto result=consume();if(!result.ok())return result;}
            return c->IsCancelled()?grpc::Status(grpc::StatusCode::CANCELLED,"Stream cancelled"):grpc::Status::OK;
        }catch(const std::exception& e){return {grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    }
    grpc::Status Focus(grpc::ServerContext* c,const api::FocusRequest* r,api::Applied* out) override{return apply(c,core::Focus{r->id(),r->reset_zoom()},out);}
    grpc::Status Orbit(grpc::ServerContext* c,const api::OrbitRequest* r,api::Applied* out) override{return apply(c,core::Orbit{r->azimuth_radians(),r->elevation_radians(),r->has_distance_scale()?r->distance_scale():1.},out);}
    grpc::Status GetCamera(grpc::ServerContext*,const api::Empty*,api::Camera* out) override{auto s=bridge_.snapshot();if(!s)return {grpc::StatusCode::UNAVAILABLE,"Scene is initializing"};camera(s->camera,out);return grpc::Status::OK;}
    static void catalogJob(const catalog::Job& job,api::CatalogJob* out){
        out->set_id(job.id);out->set_catalog_id(job.catalogId);out->set_object_id(job.objectId);out->set_stage(static_cast<api::CatalogJob::Stage>(job.stage));
        out->set_error(job.error);out->set_fetched_utc_unix_seconds(job.fetchedUtc);out->set_valid_until_utc_unix_seconds(job.validUntilUtc);out->set_applied_revision(job.revision);
    }
    grpc::Status ListObjects(grpc::ServerContext*,const api::CatalogQuery* request,api::CatalogList* out) override{
        if(!catalog_)return {grpc::StatusCode::UNAVAILABLE,"Catalog is not enabled"};
        if(request->query().size()>128)return {grpc::StatusCode::INVALID_ARGUMENT,"Catalog query exceeds limit"};
        auto lower=[](std::string text){for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;};
        const auto query=lower(request->query());const auto snapshot=bridge_.snapshot();
        for(const auto& item:catalog::entries()){
            if(!query.empty()&&lower(std::string(item.id)+" "+item.name).find(query)==std::string::npos)continue;
            auto* result=out->add_items();result->set_id(std::string(item.id));result->set_name(item.name);result->set_provider("NASA/JPL Horizons");result->set_reference_radius_meters(item.radiusMeters);const auto present=snapshot?std::find_if(snapshot->objects.begin(),snapshot->objects.end(),[&](const auto& o){return o.id==item.id;}):std::vector<core::Object>::const_iterator{};
            result->set_representation(snapshot&&present!=snapshot->objects.end()&&present->builtin?"bundled NASA visual imagery; resolution/registration limits in source records":"simplified sphere; not photorealistic imagery");
            result->set_present_in_scene(snapshot&&std::any_of(snapshot->objects.begin(),snapshot->objects.end(),[&](const auto& o){return o.id==item.id;}));
        }
        return grpc::Status::OK;
    }
    grpc::Status SearchObjects(grpc::ServerContext* context,const api::CatalogQuery* request,api::CatalogList* out) override{
        if(!catalog_)return {grpc::StatusCode::UNAVAILABLE,"Catalog is not enabled"};
        DownloadProgress progress;std::jthread cancellation([&](std::stop_token stop){while(!stop.stop_requested()){if(context->IsCancelled()){progress.cancelled=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}});
        return catalogCall([&]{for(const auto& item:catalog_->search(request->query(),&progress)){auto* value=out->add_items();value->set_id(item.id);value->set_name(item.name);value->set_provider("NASA/JPL SBDB + Horizons");value->set_reference_radius_meters(item.radiusMeters);value->set_representation(item.radiusMeters>0?"simplified sphere; measured/reference diameter":"simplified sphere; diameter unknown; illustrative radius on import");}});
    }
    template<class F>grpc::Status catalogCall(F fn){
        if(!catalog_)return {grpc::StatusCode::UNAVAILABLE,"Catalog is not enabled"};
        try{fn();return grpc::Status::OK;}catch(const std::out_of_range& e){return {grpc::StatusCode::NOT_FOUND,e.what()};}catch(const std::length_error& e){return {grpc::StatusCode::RESOURCE_EXHAUSTED,e.what()};}catch(const std::invalid_argument& e){return {grpc::StatusCode::INVALID_ARGUMENT,e.what()};}catch(const std::exception& e){return {grpc::StatusCode::UNAVAILABLE,e.what()};}
    }
    grpc::Status ImportObject(grpc::ServerContext*,const api::ImportCatalogObjectRequest* request,api::CatalogJob* out) override{
        return catalogCall([&]{catalog::Import value{request->catalog_id(),request->object_id(),request->focus_camera(),{}};if(request->has_color_rgb())value.color=request->color_rgb();catalogJob(catalog_->job(catalog_->importObject(std::move(value))),out);});
    }
    grpc::Status GetJob(grpc::ServerContext*,const api::CatalogJobRequest* request,api::CatalogJob* out) override{return catalogCall([&]{catalogJob(catalog_->job(request->id()),out);});}
    grpc::Status GetJobs(grpc::ServerContext*,const api::Empty*,api::CatalogJobs* out) override{return catalogCall([&]{for(const auto& job:catalog_->jobs())catalogJob(job,out->add_jobs());});}
    grpc::Status RetryJob(grpc::ServerContext*,const api::CatalogJobRequest* request,api::CatalogJob* out) override{return catalogCall([&]{catalogJob(catalog_->job(catalog_->retry(request->id())),out);});}
private:core::Bridge& bridge_;catalog::Manager* catalog_;

};
}
struct LayerServices final:api::LayerService::Service {
    Services& services;
    explicit LayerServices(Services& s):services(s){}
    grpc::Status CreateLayer(grpc::ServerContext* c,const api::CreateLayerRequest* r,api::Applied* out)override{return services.CreateLayer(c,r,out);}
    grpc::Status ListLayers(grpc::ServerContext* c,const api::Empty* r,api::Layers* out)override{return services.ListLayers(c,r,out);}
    grpc::Status RemoveLayer(grpc::ServerContext* c,const api::ObjectRequest* r,api::Applied* out)override{return services.RemoveLayer(c,r,out);}
    grpc::Status SetVisibility(grpc::ServerContext* c,const api::VisibilityRequest* r,api::Applied* out)override{return services.apply(c,core::LayerVisibility{r->id(),r->visible()},out);}
};
struct Server::Impl {core::Bridge& bridge;Services services;LayerServices layers;FlightServices flights;std::unique_ptr<grpc::Server> server;int port=0;Impl(core::Bridge& b,catalog::Manager* c,AirSources a):bridge(b),services(b,c),layers(services),flights(b,std::move(a)){}};
Server::Server(core::Bridge& bridge,unsigned port,catalog::Manager* catalog,AirSources air):impl_(std::make_unique<Impl>(bridge,catalog,std::move(air))){
    if(port>65535)throw std::invalid_argument("Invalid gRPC port");grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:"+std::to_string(port),grpc::InsecureServerCredentials(),&impl_->port);
    builder.RegisterService(static_cast<api::SceneService::Service*>(&impl_->services));builder.RegisterService(static_cast<api::CameraService::Service*>(&impl_->services));builder.RegisterService(static_cast<api::TimeService::Service*>(&impl_->services));builder.RegisterService(static_cast<api::CatalogService::Service*>(&impl_->services));builder.RegisterService(static_cast<api::AssetService::Service*>(&impl_->services));
    builder.RegisterService(&impl_->layers);builder.RegisterService(&impl_->flights);
    builder.SetMaxReceiveMessageSize(1024*1024);builder.SetMaxSendMessageSize(4*1024*1024);
    grpc::ResourceQuota quota;quota.SetMaxThreads(16);quota.Resize(64*1024*1024);builder.SetResourceQuota(quota);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS,1);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS,1);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS,4);
    builder.AddChannelArgument("grpc.max_concurrent_streams",16);
    impl_->server=builder.BuildAndStart();if(!impl_->server||!impl_->port)throw std::runtime_error("Cannot bind local gRPC server");
}
unsigned Server::port() const{return static_cast<unsigned>(impl_->port);}
Server::~Server(){impl_->bridge.close();impl_->server->Shutdown(std::chrono::system_clock::now()+std::chrono::seconds(1));}
}
