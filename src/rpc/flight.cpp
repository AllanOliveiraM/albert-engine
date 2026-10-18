#include "flight.hpp"
#include "flight_wire.hpp"
#include "wire.hpp"
#include "core/simulation.hpp"
#include "physics/vehicles.hpp"
#include "physics/sensors.hpp"
#include "physics/weather.hpp"
#include "providers/gfs.hpp"
#include "providers/space_weather.hpp"
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
    core::Bridge& bridge;AirSources sources;std::unique_ptr<wire::Server> data;std::mutex mutex;std::optional<ph::Flight> flight;std::optional<ph::Imu> imu;ph::Imu::Reading reading;std::string atmosphere;
    ph::Tt step=ph::second/1000,start=0;std::uint64_t tick=0;int shown=0;std::vector<api::FlightEvent> events;
    std::chrono::steady_clock::time_point published{};
    Impl(core::Bridge& b,AirSources s):bridge(b),sources(std::move(s)){
        if(!sources.weather)sources.weather=[](double lat,double lon,double utc){return std::shared_ptr<const physics::AirModel>(providers::gfs().around(lat,lon,utc));};
        if(!sources.upper)sources.upper=[](double utc)->std::shared_ptr<const physics::AirModel>{try{return providers::spaceWeather().msis(utc);}catch(const std::exception&){return nullptr;}};
    }
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
            const auto snap=bridge.snapshot();const bool followed=snap&&snap->camera.target==objectId;
            (void)bridge.submit(core::Remove{objectId});
            if(wait(bridge.submit(core::Create{object()})).code!=core::Code::Ok)return; // Retried on the next publication.
            shown=flight->state().stage;if(followed)(void)bridge.submit(core::Focus{objectId,false});
        }else (void)bridge.submit(core::Append{objectId,{s}});
        (void)bridge.submit(core::SetTime{std::clamp(s.utcSeconds,time::Simulation::earliestUtc,time::Simulation::latestUtc),{},false,true});
        (void)bridge.submit(telemetry());
    }
    core::FlightTelemetry telemetry()const{
        const ph::Sample x=flight->sample();const auto& st=flight->state();
        return{true,st.burning,st.clamped,st.landed,flight->vehicle().name,atmosphere,elapsed(),x.geodetic.height,ph::norm(x.nedVelocity),x.mach,x.dynamicPressure,x.mass,st.propellant,x.air.wind.x,x.air.wind.y,st.stage,static_cast<int>(flight->vehicle().stages.size())};
    }
    struct Command {std::uint32_t kind;double a=0,b=0,c=0;};
    std::uint32_t eventBits=0; // For the data plane, since its last response.
    grpc::Status run(const std::vector<Command>& commands,std::uint32_t ticks,const std::function<bool()>& cancelled){
        if(!flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
        if(ticks>600000)return{grpc::StatusCode::INVALID_ARGUMENT,"At most 600000 ticks per step"};
        ph::Flight next=*flight; // Commands apply atomically.
        try{for(const auto& c:commands)switch(c.kind){
            case wire::ignite:next.ignite();break;case wire::cutoff:next.cutoff();break;case wire::separate:next.separate();break;case wire::release:next.release();break;
            case wire::throttle:next.throttle(c.a);break;case wire::gimbal:next.gimbal(c.a,c.b);break;case wire::torque:next.torque({c.a,c.b,c.c});break;
            default:throw std::invalid_argument("Unknown command");}}
        catch(const std::invalid_argument& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}catch(const std::logic_error& e){return{grpc::StatusCode::FAILED_PRECONDITION,e.what()};}
        // Integrated on the copy and committed whole: a cancelled call leaves the flight untouched.
        bool changed=std::any_of(commands.begin(),commands.end(),[](const Command& c){return c.kind<=wire::release;});
        const std::size_t pending=events.size();std::uint32_t bits=0;
        auto event=[&](api::FlightEvent::Kind kind,std::uint32_t bit){api::FlightEvent e;e.set_kind(kind);e.set_elapsed_seconds(ph::secondsSinceJ2000(next.state().time-start));e.set_stage(static_cast<unsigned>(next.state().stage));events.push_back(std::move(e));bits|=bit;changed=true;};
        const ph::Tt total=step*static_cast<ph::Tt>(ticks),chunk=step*4096;
        for(ph::Tt left=total;left>0;){
            if(cancelled()){events.resize(pending);return{grpc::StatusCode::CANCELLED,"Step cancelled; flight unchanged"};}
            const auto before=next.state();left-=next.advance(std::min(left,chunk));const auto& after=next.state();
            if(before.clamped&&!after.clamped)event(api::FlightEvent::LIFTOFF,wire::liftoff);
            if(before.burning&&!after.burning&&after.propellant<=0)event(api::FlightEvent::DEPLETED,wire::depleted);
            if(!before.landed&&after.landed)event(api::FlightEvent::TOUCHDOWN,wire::touchdown);
        }
        if(cancelled()){events.resize(pending);return{grpc::StatusCode::CANCELLED,"Step cancelled; flight unchanged"};}
        ph::Imu sensor=*imu;const auto inc=next.increments();
        if(inc.seconds>0)reading=sensor.measure(inc.seconds,inc.velocity/inc.seconds,inc.angle/inc.seconds,inc.velocity,inc.angle,next.sample().air.pressure);
        *flight=std::move(next);*imu=std::move(sensor);tick+=ticks;eventBits|=bits;
        if(ticks)publish(changed||flight->state().stage!=shown);
        return grpc::Status::OK;
    }
    void fill(wire::Response& o){
        const ph::Sample x=flight->sample();const auto& s=flight->state();
        auto put3=[](double* d,ph::V3 v){d[0]=v.x;d[1]=v.y;d[2]=v.z;};
        o=wire::Response{};o.tick=tick;o.elapsed=elapsed();o.utc=ph::utcFromTt(s.time);
        put3(o.specificForce,reading.specificForce);put3(o.rate,reading.rate);put3(o.deltaVelocity,reading.deltaVelocity);put3(o.deltaAngle,reading.deltaAngle);o.pressure=reading.pressure;
        put3(o.position,s.position);put3(o.velocity,s.velocity);o.attitude[0]=s.attitude.x;o.attitude[1]=s.attitude.y;o.attitude[2]=s.attitude.z;o.attitude[3]=s.attitude.w;
        o.geodetic[0]=x.geodetic.latitude/deg;o.geodetic[1]=x.geodetic.longitude/deg;o.geodetic[2]=x.geodetic.height;put3(o.nedVelocity,x.nedVelocity);
        o.attitudeNed[0]=x.nedAttitude.x;o.attitudeNed[1]=x.nedAttitude.y;o.attitudeNed[2]=x.nedAttitude.z;o.attitudeNed[3]=x.nedAttitude.w;
        o.mach=x.mach;o.dynamicPressure=x.dynamicPressure;o.alpha=x.alpha;o.beta=x.beta;o.mass=x.mass;o.propellant=s.propellant;o.thrust=x.thrust;
        o.stage=static_cast<std::uint32_t>(s.stage);o.flags=(s.burning?wire::burning:0u)|(s.clamped?wire::clamped:0u)|(s.landed?wire::landed:0u);o.events=eventBits;eventBits=0;o.hash=flight->hash();
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
        o->set_state_hash(flight->hash());o->set_vehicle(v.name);o->set_step_seconds(ph::secondsSinceJ2000(step));o->set_atmosphere(atmosphere);put(x.air.wind,o->mutable_wind_ned_meters_per_second());o->set_data_port(data?data->port():0);
        put(reading.specificForce,o->mutable_measured_specific_force_meters_per_second2());put(reading.rate,o->mutable_measured_angular_rate_radians_per_second());
        put(reading.deltaVelocity,o->mutable_delta_velocity_meters_per_second());put(reading.deltaAngle,o->mutable_delta_angle_radians());o->set_measured_static_pressure_pascals(reading.pressure);
    }
};
FlightServices::FlightServices(core::Bridge& bridge,AirSources sources):impl_(std::make_unique<Impl>(bridge,std::move(sources))){
    impl_->data=std::make_unique<wire::Server>([this](const wire::Request& q,wire::Response& r){
        std::vector<Impl::Command> commands; // Fixed order: cutoff, separate, release, ignite, throttle, gimbal, torque.
        for(const std::uint32_t k:{wire::cutoff,wire::separate,wire::release,wire::ignite})if(q.commands&k)commands.push_back({k});
        if(q.commands&wire::throttle)commands.push_back({wire::throttle,q.throttleValue});
        if(q.commands&wire::gimbal)commands.push_back({wire::gimbal,q.gimbalPitch,q.gimbalYaw});
        if(q.commands&wire::torque)commands.push_back({wire::torque,q.torqueValue[0],q.torqueValue[1],q.torqueValue[2]});
        std::lock_guard lock(impl_->mutex);
        if(!impl_->flight){r=wire::Response{};r.status=wire::noFlight;return;}
        const auto s=impl_->run(commands,q.ticks,[]{return false;});impl_->fill(r);
        r.status=s.ok()?wire::ok:s.error_code()==grpc::StatusCode::INVALID_ARGUMENT?wire::invalid:wire::precondition;
    });
}
FlightServices::~FlightServices()=default;
grpc::Status FlightServices::CreateFlight(grpc::ServerContext* context,const api::CreateFlightRequest* r,api::FlightSample* out){
    std::optional<ph::Flight> made;ph::Tt step=0,t=0;std::string atmosphere="U.S. Standard Atmosphere 1976";
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
        const double coast=r->has_coast_step_seconds()?r->coast_step_seconds():1.;
        if(!finite(r->heading_degrees())||!(std::abs(pitch)<=90)||!(seconds>=1e-5&&seconds<=.1)||!(coast>=0&&coast<=10))throw std::invalid_argument("Invalid heading, pitch, step or coast step");
        const auto shown=impl_->bridge.snapshot();const double utc=r->has_utc_unix_seconds()?r->utc_unix_seconds():shown?shown->simulation.utcSeconds:time::simulation().read().utcSeconds;
        if(!(utc>=time::Simulation::earliestUtc&&utc<=time::Simulation::latestUtc))throw std::invalid_argument("Instant outside 1900-2100");
        t=ph::ttFromUtc(utc);step=static_cast<ph::Tt>(std::llround(seconds*1e9));
        const unsigned degree=r->has_gravity_degree()?r->gravity_degree():20u;
        if(degree<2||degree>static_cast<unsigned>(ph::egm2008Maximum))throw std::invalid_argument("Gravity degree must be 2-70");
        std::shared_ptr<const physics::AirModel> air;
        if(r->weather()!=api::CreateFlightRequest::STANDARD_ATMOSPHERE){ // Weather only while Albert follows the real clock.
            const bool live=!shown||shown->simulation.live;const double now=shown?shown->clock.utcSeconds:std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
            air=impl_->sources.upper(utc); // NRLMSISE-00 over the weather, or alone.
            std::string unavailable;
            if(r->weather()==api::CreateFlightRequest::LIVE_GFS||(live&&std::abs(utc-now)<=7200)){
                try{auto weather=impl_->sources.weather(site.latitude_degrees(),site.longitude_degrees(),utc);
                    if(const auto grid=std::dynamic_pointer_cast<const physics::Weather>(weather))weather=grid->above(air);
                    air=std::move(weather);}
                catch(const std::exception& e){if(r->weather()==api::CreateFlightRequest::LIVE_GFS)return{grpc::StatusCode::UNAVAILABLE,std::string("Live weather unavailable: ")+e.what()};unavailable=std::string(" (live weather unavailable: ")+e.what()+")";}
            }
            atmosphere=(air?air->source():atmosphere)+unavailable;
        }
        ph::Environment env;env.air=air;env.planet.degree=static_cast<int>(degree);env.rotation=ph::Rotation::iau(ph::secondsSinceJ2000(t)-(utc-946728000.));env.ground=site.height_meters();
        made.emplace(vehicle,env,ph::Flight::pad(env,vehicle,t,{site.latitude_degrees()*deg,site.longitude_degrees()*deg,site.height_meters()},r->heading_degrees()*deg,pitch*deg));
        made->steps(step,static_cast<ph::Tt>(std::llround(coast*1e9)));
    }catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    if(context->IsCancelled())return{grpc::StatusCode::CANCELLED,"Flight creation cancelled; current flight kept"};
    std::lock_guard lock(impl_->mutex);auto& m=*impl_;
    m.flight=std::move(made);m.step=step;m.start=t;m.tick=0;m.events.clear();m.eventBits=0;m.atmosphere=atmosphere;
    { // The IMU starts with one sample at the pad.
        const auto g=r->imu().grade();m.imu.emplace(ph::imuModel(g==api::ImuSettings::TACTICAL?ph::ImuGrade::tactical:g==api::ImuSettings::INDUSTRIAL?ph::ImuGrade::industrial:ph::ImuGrade::ideal),r->imu().seed());
        const ph::Sample x=m.flight->sample();const double dt=ph::secondsSinceJ2000(step);
        m.reading=m.imu->measure(dt,x.specificForce,m.flight->state().rate,x.specificForce*dt,m.flight->state().rate*dt,x.air.pressure);
    }
    m.shown=0;(void)wait(m.bridge.submit(core::Remove{objectId}));
    if(auto s=failed(wait(m.bridge.submit(core::Create{m.object()})));!s.ok())return s;
    if(auto s=failed(wait(m.bridge.submit(core::SetTime{ph::utcFromTt(t),0.,false})));!s.ok())return s;
    if(r->focus_camera())(void)wait(m.bridge.submit(core::Focus{objectId,true}));
    m.published=std::chrono::steady_clock::now();m.fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::Step(grpc::ServerContext* context,const api::StepRequest* r,api::FlightSample* out){
    std::vector<Impl::Command> commands;
    for(const auto& c:r->commands())switch(c.command_case()){
        case api::FlightCommand::kIgnite:if(c.ignite())commands.push_back({wire::ignite});break;
        case api::FlightCommand::kCutoff:if(c.cutoff())commands.push_back({wire::cutoff});break;
        case api::FlightCommand::kSeparate:if(c.separate())commands.push_back({wire::separate});break;
        case api::FlightCommand::kRelease:if(c.release())commands.push_back({wire::release});break;
        case api::FlightCommand::kThrottle:commands.push_back({wire::throttle,c.throttle()});break;
        case api::FlightCommand::kGimbal:commands.push_back({wire::gimbal,c.gimbal().pitch_radians(),c.gimbal().yaw_radians()});break;
        case api::FlightCommand::kTorqueNewtonMeters:commands.push_back({wire::torque,c.torque_newton_meters().x(),c.torque_newton_meters().y(),c.torque_newton_meters().z()});break;
        default:return{grpc::StatusCode::INVALID_ARGUMENT,"Empty command"};
    }
    std::lock_guard lock(impl_->mutex);
    if(auto s=impl_->run(commands,r->ticks(),[&]{return context->IsCancelled();});!s.ok())return s;
    impl_->fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::GetFlight(grpc::ServerContext*,const api::Empty*,api::FlightSample* out){
    std::lock_guard lock(impl_->mutex);if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};impl_->fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::EndFlight(grpc::ServerContext*,const api::Empty*,api::Applied* out){
    std::lock_guard lock(impl_->mutex);if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
    impl_->flight.reset();(void)impl_->bridge.submit(core::FlightTelemetry{});const auto reply=wait(impl_->bridge.submit(core::Remove{objectId}));out->set_revision(reply.revision);
    return reply.code==core::Code::Ok||reply.code==core::Code::NotFound?grpc::Status::OK:grpc::Status{grpc::StatusCode::UNAVAILABLE,reply.message};
}
}
