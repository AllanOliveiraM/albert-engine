#include "flight.hpp"
#include "core/simulation.hpp"
#include "physics/vehicles.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <stdexcept>
namespace albert::rpc {
namespace {
namespace api=::albert::v1;namespace ph=physics;
constexpr double deg=3.14159265358979323846/180;constexpr const char* objectId="flight";
void put(ph::V3 v,api::Vector3* o){o->set_x(v.x);o->set_y(v.y);o->set_z(v.z);}
void put(ph::Q q,api::Quaternion* o){o->set_x(q.x);o->set_y(q.y);o->set_z(q.z);o->set_w(q.w);}
bool finite(double v){return std::isfinite(v);}
core::Reply wait(core::Ticket t){
    if(t.result.wait_for(std::chrono::seconds(3))!=std::future_status::ready){*t.cancelled=true;return{core::Code::Unavailable,"Application did not apply the scene command in time",0};}
    return t.result.get();
}
grpc::Status failed(const core::Reply& r){return r.code==core::Code::Ok?grpc::Status::OK:grpc::Status{grpc::StatusCode::UNAVAILABLE,r.message};}
}
struct FlightServices::Impl {
    core::Bridge& bridge;std::mutex mutex;std::optional<ph::Flight> flight;
    ph::Tt step=ph::second/1000,start=0;std::uint64_t tick=0;int shown=0;std::vector<api::FlightEvent> events;
    std::chrono::steady_clock::time_point published{};
    explicit Impl(core::Bridge& b):bridge(b){}
    double elapsed()const{return ph::secondsSinceJ2000(flight->state().time-start);}
    core::Sample scene()const{ // Cylinder axis (local y) along body x, centred on the stack.
        const auto& s=flight->state();const auto& st=flight->vehicle().stages[s.stage];
        const double f=st.propellant>0?s.propellant/st.propellant:0,cg=st.cgEmpty+(st.cgFull-st.cgEmpty)*f;
        const ph::Q q=s.attitude*ph::Q{std::sqrt(.5),0,0,-std::sqrt(.5)};
        const ph::V3 p=s.position+ph::matrix(s.attitude)*ph::V3{cg-st.length/2,0,0};
        return core::Sample{ph::utcFromTt(s.time),{p.x,p.y,p.z},{s.velocity.x,s.velocity.y,s.velocity.z},core::Quaternion{q.x,q.y,q.z,q.w},true};
    }
    core::Object object()const{
        const auto& v=flight->vehicle();const double length=v.stages[flight->state().stage].length;
        core::Object o;o.id=objectId;o.name=v.name;o.source="Albert flight physics";o.appearance={core::Shape::Cylinder,{v.diameter,length,v.diameter},0xe8e8e8};
        o.radiusMeters=length/2;o.defaultDistanceMeters=6*length;o.minDistanceMeters=length;o.samples.push_back(scene());return o;
    }
    void publish(bool force){
        const auto now=std::chrono::steady_clock::now();
        if(!force&&now-published<std::chrono::milliseconds(33))return;
        published=now;const core::Sample s=scene();
        if(flight->state().stage!=shown){ // The stack got shorter: replace the cylinder, keeping the camera on it.
            shown=flight->state().stage;const auto snap=bridge.snapshot();const bool followed=snap&&snap->camera.target==objectId;
            (void)bridge.submit(core::Remove{objectId});(void)bridge.submit(core::Create{object()});if(followed)(void)bridge.submit(core::Focus{objectId,false});
        }else (void)bridge.submit(core::Append{objectId,{s}});
        (void)bridge.submit(core::SetTime{std::clamp(s.utcSeconds,time::Simulation::earliestUtc,time::Simulation::latestUtc),{},false,true});
    }
    void fill(api::FlightSample* o){
        const ph::Sample x=flight->sample();const auto& s=flight->state();const auto& v=flight->vehicle();
        o->set_tick(tick);o->set_elapsed_seconds(elapsed());o->set_utc_unix_seconds(ph::utcFromTt(s.time));
        put(x.specificForce,o->mutable_specific_force_meters_per_second2());put(s.rate,o->mutable_angular_rate_radians_per_second());
        o->set_static_pressure_pascals(x.air.pressure);o->set_air_temperature_kelvin(x.air.temperature);
        put(s.position,o->mutable_position_meters());put(s.velocity,o->mutable_velocity_meters_per_second());put(s.attitude,o->mutable_attitude());
        put(x.fixedPosition,o->mutable_position_ecef_meters());put(x.fixedVelocity,o->mutable_velocity_ecef_meters_per_second());
        auto* g=o->mutable_geodetic();g->set_latitude_degrees(x.geodetic.latitude/deg);g->set_longitude_degrees(x.geodetic.longitude/deg);g->set_height_meters(x.geodetic.height);
        put(x.nedVelocity,o->mutable_velocity_ned_meters_per_second());put(x.nedAttitude,o->mutable_attitude_ned());
        o->set_mach(x.mach);o->set_dynamic_pressure_pascals(x.dynamicPressure);o->set_alpha_radians(x.alpha);o->set_beta_radians(x.beta);o->set_air_density_kilograms_per_meter3(x.air.density);
        o->set_stage(static_cast<unsigned>(s.stage));o->set_stages(static_cast<unsigned>(v.stages.size()));o->set_propellant_kilograms(s.propellant);o->set_mass_kilograms(x.mass);o->set_thrust_newtons(x.thrust);
        o->set_burning(s.burning);o->set_clamped(s.clamped);o->set_landed(s.landed);o->set_throttle(s.throttle);
        o->mutable_gimbal()->set_pitch_radians(s.gimbalPitch);o->mutable_gimbal()->set_yaw_radians(s.gimbalYaw);
        for(auto& e:events)*o->add_events()=std::move(e);events.clear();
        o->set_state_hash(flight->hash());o->set_vehicle(v.name);o->set_step_seconds(ph::secondsSinceJ2000(step));
    }
    void event(api::FlightEvent::Kind kind,int stage){api::FlightEvent e;e.set_kind(kind);e.set_elapsed_seconds(elapsed());e.set_stage(static_cast<unsigned>(stage));events.push_back(std::move(e));}
};
FlightServices::FlightServices(core::Bridge& bridge):impl_(std::make_unique<Impl>(bridge)){}
FlightServices::~FlightServices()=default;
grpc::Status FlightServices::CreateFlight(grpc::ServerContext*,const api::CreateFlightRequest* r,api::FlightSample* out){
    std::optional<ph::Flight> made;ph::Tt step=0,t=0;
    try{
        ph::Vehicle vehicle;
        switch(r->vehicle()){
        case api::GENERIC_LAUNCHER:{const double payload=r->has_payload_kilograms()?r->payload_kilograms():5000.;
            if(!(payload>=0&&payload<=30000))throw std::invalid_argument("Payload must be 0-30000 kg");vehicle=ph::genericLauncher(payload);break;}
        case api::NESC_TWO_STAGE:vehicle=ph::nescTwoStage();break;
        case api::NESC_SPHERE:vehicle=ph::nescSphere();break;
        default:throw std::invalid_argument("Unknown vehicle");
        }
        const auto& site=r->site();const double pitch=r->has_pitch_degrees()?r->pitch_degrees():90.,seconds=r->has_step_seconds()?r->step_seconds():.001;
        if(!(std::abs(site.latitude_degrees())<=90&&std::abs(site.longitude_degrees())<=360&&site.height_meters()>=-500&&site.height_meters()<=1e4))throw std::invalid_argument("Invalid launch site");
        if(!finite(r->heading_degrees())||!(std::abs(pitch)<=90)||!(seconds>=1e-5&&seconds<=.1))throw std::invalid_argument("Invalid heading, pitch or step");
        const auto shown=impl_->bridge.snapshot();const double utc=r->has_utc_unix_seconds()?r->utc_unix_seconds():shown?shown->simulation.utcSeconds:time::simulation().read().utcSeconds;
        if(!(utc>=time::Simulation::earliestUtc&&utc<=time::Simulation::latestUtc))throw std::invalid_argument("Instant outside 1900-2100");
        t=ph::ttFromUtc(utc);step=static_cast<ph::Tt>(std::llround(seconds*1e9));
        ph::Environment env;env.rotation=ph::Rotation::iau(ph::secondsSinceJ2000(t)-(utc-946728000.));env.ground=site.height_meters();
        made.emplace(vehicle,env,ph::Flight::pad(env,vehicle,t,{site.latitude_degrees()*deg,site.longitude_degrees()*deg,site.height_meters()},r->heading_degrees()*deg,pitch*deg));
    }catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    std::lock_guard lock(impl_->mutex);auto& m=*impl_;
    m.flight=std::move(made);m.step=step;m.start=t;m.tick=0;m.events.clear();
    m.shown=0;(void)wait(m.bridge.submit(core::Remove{objectId}));
    if(auto s=failed(wait(m.bridge.submit(core::Create{m.object()})));!s.ok())return s;
    if(auto s=failed(wait(m.bridge.submit(core::SetTime{ph::utcFromTt(t),0.,false})));!s.ok())return s;
    if(r->focus_camera())(void)wait(m.bridge.submit(core::Focus{objectId,true}));
    m.published=std::chrono::steady_clock::now();m.fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::Step(grpc::ServerContext*,const api::StepRequest* r,api::FlightSample* out){
    std::lock_guard lock(impl_->mutex);auto& m=*impl_;
    if(!m.flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
    if(r->ticks()>600000)return{grpc::StatusCode::INVALID_ARGUMENT,"At most 600000 ticks per step"};
    ph::Flight next=*m.flight; // Commands apply atomically.
    try{
        for(const auto& c:r->commands())switch(c.command_case()){
            case api::FlightCommand::kIgnite:if(c.ignite())next.ignite();break;
            case api::FlightCommand::kCutoff:if(c.cutoff())next.cutoff();break;
            case api::FlightCommand::kSeparate:if(c.separate())next.separate();break;
            case api::FlightCommand::kRelease:if(c.release())next.release();break;
            case api::FlightCommand::kThrottle:next.throttle(c.throttle());break;
            case api::FlightCommand::kGimbal:next.gimbal(c.gimbal().pitch_radians(),c.gimbal().yaw_radians());break;
            case api::FlightCommand::kTorqueNewtonMeters:next.torque({c.torque_newton_meters().x(),c.torque_newton_meters().y(),c.torque_newton_meters().z()});break;
            default:throw std::invalid_argument("Empty command");
        }
    }catch(const std::invalid_argument& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}catch(const std::logic_error& e){return{grpc::StatusCode::FAILED_PRECONDITION,e.what()};}
    *m.flight=std::move(next);bool changed=std::any_of(r->commands().begin(),r->commands().end(),[](const auto& c){return c.command_case()<=api::FlightCommand::kRelease;});
    for(std::uint32_t i=0;i<r->ticks();++i){
        for(ph::Tt left=m.step;left>0;){
            const auto before=m.flight->state();left-=m.flight->advance(left);const auto& after=m.flight->state();
            if(before.clamped&&!after.clamped){m.event(api::FlightEvent::LIFTOFF,after.stage);changed=true;}
            if(before.burning&&!after.burning&&after.propellant<=0){m.event(api::FlightEvent::DEPLETED,after.stage);changed=true;}
            if(!before.landed&&after.landed){m.event(api::FlightEvent::TOUCHDOWN,after.stage);changed=true;}
        }
        ++m.tick;
    }
    if(r->ticks())m.publish(changed||m.flight->state().stage!=m.shown);
    m.fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::GetFlight(grpc::ServerContext*,const api::Empty*,api::FlightSample* out){
    std::lock_guard lock(impl_->mutex);if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};impl_->fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::EndFlight(grpc::ServerContext*,const api::Empty*,api::Applied* out){
    std::lock_guard lock(impl_->mutex);if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
    impl_->flight.reset();const auto reply=wait(impl_->bridge.submit(core::Remove{objectId}));out->set_revision(reply.revision);
    return reply.code==core::Code::Ok||reply.code==core::Code::NotFound?grpc::Status::OK:grpc::Status{grpc::StatusCode::UNAVAILABLE,reply.message};
}
}
