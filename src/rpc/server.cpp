#include "server.hpp"
#include "albert/v1/engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
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
class Services final:public api::SceneService::Service,public api::CameraService::Service {
public:
    explicit Services(core::Bridge& bridge):bridge_(bridge){}
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
        out->set_revision(s->revision);out->set_utc_unix_seconds(s->utcSeconds);camera(s->camera,out->mutable_camera());
        for(const auto& o:s->objects){auto* result=out->add_objects();result->set_id(o.id);result->set_name(o.name);result->set_source(o.source);result->set_builtin(o.builtin);result->set_visible(o.visible);result->set_radius_meters(o.radiusMeters);
            auto* g=result->mutable_geometry();g->set_shape(static_cast<api::Geometry::Shape>(o.appearance.shape));vector(o.appearance.sizeMeters,g->mutable_size_meters());g->set_color_rgb(o.appearance.color);
            if(!o.samples.empty()){sample(o.samples.back(),result->mutable_state());result->set_stale(!o.builtin&&(s->utcSeconds<o.samples.back().utcSeconds||s->utcSeconds-o.samples.back().utcSeconds>1.));}}
        return grpc::Status::OK;
    }
    grpc::Status GetCapabilities(grpc::ServerContext*,const api::Empty*,api::Capabilities* out) override{
        out->add_frames(api::GEOCENTRIC_ICRF);out->set_max_objects(core::Scene::maxObjects);out->set_max_samples_per_batch(core::Scene::maxSamples);
        for(auto shape:{api::Geometry::SPHERE,api::Geometry::BOX,api::Geometry::CYLINDER})out->add_shapes(shape);return grpc::Status::OK;
    }
    grpc::Status CreateObject(grpc::ServerContext* c,const api::CreateObjectRequest* r,api::Applied* out) override{
        try{core::Object o;o.id=r->id();o.name=r->name();o.source=r->source();const auto& g=r->geometry();
            if(g.shape()<api::Geometry::SPHERE||g.shape()>api::Geometry::CYLINDER)throw std::invalid_argument("Unsupported geometry");
            o.appearance={static_cast<core::Shape>(g.shape()),vector(g.size_meters()),g.has_color_rgb()?g.color_rgb():0x77cddd};o.samples.push_back(sample(r->initial_state()));return apply(c,core::Create{std::move(o)},out);
        }catch(const std::exception& e){return {grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
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
private:core::Bridge& bridge_;
};
}
struct Server::Impl {core::Bridge& bridge;Services services;std::unique_ptr<grpc::Server> server;int port=0;explicit Impl(core::Bridge& b):bridge(b),services(b){}};
Server::Server(core::Bridge& bridge,unsigned port):impl_(std::make_unique<Impl>(bridge)){
    if(port>65535)throw std::invalid_argument("Invalid gRPC port");grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:"+std::to_string(port),grpc::InsecureServerCredentials(),&impl_->port);
    builder.RegisterService(static_cast<api::SceneService::Service*>(&impl_->services));builder.RegisterService(static_cast<api::CameraService::Service*>(&impl_->services));
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
