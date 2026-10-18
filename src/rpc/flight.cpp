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
#include <deque>
#include <thread>
#include <cstring>
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
namespace {
struct Prepared {ph::Vehicle vehicle;double utc=0,pitch=90,seconds=.001,coast=1;unsigned degree=20;};
Prepared prepare(const api::CreateFlightRequest& r,double utc){
    Prepared p;
    switch(r.vehicle()){
    case api::GENERIC_LAUNCHER:{const double payload=r.has_payload_kilograms()?r.payload_kilograms():5000.;
        if(!(payload>=0&&payload<=30000))throw std::invalid_argument("Payload must be 0-30000 kg");p.vehicle=ph::genericLauncher(payload);break;}
    case api::NESC_TWO_STAGE:p.vehicle=ph::nescTwoStage();break;
    case api::NESC_SPHERE:p.vehicle=ph::nescSphere();break;
    default:throw std::invalid_argument("Unknown vehicle");
    }
    const auto& site=r.site();p.pitch=r.has_pitch_degrees()?r.pitch_degrees():90.;p.seconds=r.has_step_seconds()?r.step_seconds():.001;p.coast=r.has_coast_step_seconds()?r.coast_step_seconds():1.;
    if(!(std::abs(site.latitude_degrees())<=90&&std::abs(site.longitude_degrees())<=360&&site.height_meters()>=-500&&site.height_meters()<=1e4))throw std::invalid_argument("Invalid launch site");
    if(!finite(r.heading_degrees())||!(std::abs(p.pitch)<=90)||!(p.seconds>=1e-5&&p.seconds<=.1)||!(p.coast>=0&&p.coast<=10))throw std::invalid_argument("Invalid heading, pitch, step or coast step");
    if(!(utc>=time::Simulation::earliestUtc&&utc<=time::Simulation::latestUtc))throw std::invalid_argument("Instant outside 1900-2100");
    p.utc=utc;p.degree=r.has_gravity_degree()?r.gravity_degree():20u;
    if(p.degree<2||p.degree>static_cast<unsigned>(ph::egm2008Maximum))throw std::invalid_argument("Gravity degree must be 2-70");
    return p;
}
}
struct FlightServices::Impl {
    struct Command {std::uint32_t kind;double a=0,b=0,c=0;};
    core::Bridge& bridge;AirSources sources;std::unique_ptr<wire::Server> data;std::mutex mutex;std::optional<ph::Flight> flight;std::optional<ph::Imu> imu;ph::Imu::Reading reading;std::string atmosphere;
    ph::Tt step=ph::second/1000,start=0;std::uint64_t tick=0;int shown=0;std::vector<api::FlightEvent> events;
    std::chrono::steady_clock::time_point published{};
    Impl(core::Bridge& b,AirSources s):bridge(b),sources(std::move(s)){
        if(!sources.weather)sources.weather=[](double lat,double lon,double utc){auto d=providers::gfs().fetch(lat,lon,utc);return LiveAir{d.model,std::move(d.grib)};};
        if(!sources.upper)sources.upper=[](double utc){UpperAir a;try{a.model=providers::spaceWeather().msis(utc,&a.days);}catch(const std::exception&){a={};}return a;};
    }
    // Replay log: the creation and every committed step (u32 ticks, u32 count, then per command u32 kind and three doubles).
    api::FlightLog log;std::string record;
    // Real time: the pacing thread, its settings, delayed commands and samples.
    struct Realtime {double rate=1;std::uint32_t sensor=0,actuator=0,every=10;double timeout=0;int missing=0;
        std::deque<std::pair<std::uint64_t,std::vector<Command>>> queue;std::deque<wire::Response> samples;
        std::chrono::steady_clock::time_point command;bool policyApplied=false;std::uint64_t late=0,policies=0;double worst=0;};
    Realtime realtime;std::thread pacer;std::atomic_bool pacing{false};
    void status(api::RealtimeStatus* o){o->set_running(pacing);o->set_tick(tick);o->set_late_ticks(realtime.late);o->set_worst_lag_seconds(realtime.worst);o->set_policy_events(realtime.policies);}
    void stopPacing(){pacing=false;if(pacer.joinable())pacer.join();} // Without the mutex held.
    void pace(){
        const auto begin=std::chrono::steady_clock::now();std::uint64_t n=0;
        while(pacing){
            const auto deadline=begin+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(static_cast<double>(++n)*ph::secondsSinceJ2000(step)/realtime.rate));
            std::this_thread::sleep_until(deadline);
            std::optional<wire::Response> out; // Pushed after unlocking: a slow client never holds the flight.
            {std::lock_guard lock(mutex);if(!flight||!pacing)break;
            auto& rt=realtime;const auto now=std::chrono::steady_clock::now();
            const double lag=std::chrono::duration<double>(now-deadline).count();rt.worst=std::max(rt.worst,lag);if(lag>ph::secondsSinceJ2000(step)/rt.rate)++rt.late;
            if(rt.timeout>0&&!rt.policyApplied&&std::chrono::duration<double>(now-rt.command).count()>rt.timeout){
                rt.policyApplied=true;
                if(rt.missing==api::RealtimeRequest::NEUTRAL)rt.queue.push_front({tick,{{wire::gimbal},{wire::torque}}});
                else if(rt.missing==api::RealtimeRequest::CUTOFF)rt.queue.push_front({tick,{{wire::cutoff}}});
                if(rt.missing!=api::RealtimeRequest::HOLD)++rt.policies;
            }
            std::vector<Command> due;while(!rt.queue.empty()&&rt.queue.front().first<=tick){for(auto& c:rt.queue.front().second)due.push_back(c);rt.queue.pop_front();}
            if(!run(due,1,[]{return false;}).ok())(void)run({},1,[]{return false;}); // A refused command must not stop the clock.
            wire::Response r;fill(r);r.reserved=rt.sensor;rt.samples.push_back(r);while(rt.samples.size()>rt.sensor+1)rt.samples.pop_front();
            if(tick%rt.every==0&&rt.samples.size()==rt.sensor+1)out=rt.samples.front();}
            if(out)(void)data->push(*out);
        }
    }
    void note(const std::vector<Command>& commands,std::uint32_t ticks){
        auto put=[&](const void* p,std::size_t n){record.append(static_cast<const char*>(p),n);};
        const auto count=static_cast<std::uint32_t>(commands.size());put(&ticks,4);put(&count,4);
        for(const auto& c:commands){put(&c.kind,4);put(&c.a,8);put(&c.b,8);put(&c.c,8);}
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
    grpc::Status create(const api::CreateFlightRequest& r,const Prepared& p,std::shared_ptr<const physics::AirModel> air,std::string atmosphere,
        std::vector<std::string> grib,std::vector<ph::SpaceWeatherDay> days,const std::function<bool()>& cancelled,api::FlightSample* out);
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
        *flight=std::move(next);*imu=std::move(sensor);tick+=ticks;eventBits|=bits;note(commands,ticks);
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
        if(!impl_->flight){r=wire::Response{};r.status=wire::noFlight;return true;}
        if(impl_->pacing){auto& rt=impl_->realtime;rt.queue.push_back({impl_->tick+rt.actuator,std::move(commands)});rt.command=std::chrono::steady_clock::now();rt.policyApplied=false;return false;}
        const auto s=impl_->run(commands,q.ticks,[]{return false;});impl_->fill(r);
        r.status=s.ok()?wire::ok:s.error_code()==grpc::StatusCode::INVALID_ARGUMENT?wire::invalid:wire::precondition;
        return true;
    });
}
FlightServices::~FlightServices(){impl_->stopPacing();impl_->data.reset();}
grpc::Status FlightServices::Impl::create(const api::CreateFlightRequest& r,const Prepared& p,std::shared_ptr<const physics::AirModel> air,std::string atmosphere,
    std::vector<std::string> grib,std::vector<ph::SpaceWeatherDay> days,const std::function<bool()>& cancelled,api::FlightSample* out){
    const auto& site=r.site();const ph::Tt t=ph::ttFromUtc(p.utc),stepNs=static_cast<ph::Tt>(std::llround(p.seconds*1e9));
    ph::Environment env;env.air=air;env.planet.degree=static_cast<int>(p.degree);env.rotation=ph::Rotation::iau(ph::secondsSinceJ2000(t)-(p.utc-946728000.));env.ground=site.height_meters();
    std::optional<ph::Flight> made;
    try{made.emplace(p.vehicle,env,ph::Flight::pad(env,p.vehicle,t,{site.latitude_degrees()*deg,site.longitude_degrees()*deg,site.height_meters()},r.heading_degrees()*deg,p.pitch*deg));}
    catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    made->steps(stepNs,static_cast<ph::Tt>(std::llround(p.coast*1e9)));
    if(cancelled())return{grpc::StatusCode::CANCELLED,"Flight creation cancelled; current flight kept"};
    stopPacing();std::lock_guard lock(mutex);
    flight=std::move(made);step=stepNs;start=t;tick=0;events.clear();eventBits=0;this->atmosphere=atmosphere;
    { // The IMU starts with one sample at the pad.
        const auto g=r.imu().grade();imu.emplace(ph::imuModel(g==api::ImuSettings::TACTICAL?ph::ImuGrade::tactical:g==api::ImuSettings::INDUSTRIAL?ph::ImuGrade::industrial:ph::ImuGrade::ideal),r.imu().seed());
        const ph::Sample x=flight->sample();const double dt=p.seconds;
        reading=imu->measure(dt,x.specificForce,flight->state().rate,x.specificForce*dt,flight->state().rate*dt,x.air.pressure);
    }
    log=api::FlightLog{};record.clear();log.set_engine("Albert Engine "+std::string(ALBERT_VERSION));*log.mutable_request()=r;log.mutable_request()->set_utc_unix_seconds(p.utc);log.set_atmosphere(atmosphere);
    for(auto& b:grib)log.add_gfs(std::move(b));
    for(const auto& d:days){auto* x=log.add_space_weather();x->set_day(d.day);x->set_f107(d.f107);x->set_f107a(d.f107a);x->set_ap_average(d.apAverage);x->set_type(d.type);if(d.hasAp)for(double a:d.ap)x->add_ap(a);}
    shown=0;(void)wait(bridge.submit(core::Remove{objectId}));
    if(auto s=failed(wait(bridge.submit(core::Create{object()})));!s.ok())return s;
    if(auto s=failed(wait(bridge.submit(core::SetTime{ph::utcFromTt(t),0.,false})));!s.ok())return s;
    if(r.focus_camera())(void)wait(bridge.submit(core::Focus{objectId,true}));
    published=std::chrono::steady_clock::now();fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::CreateFlight(grpc::ServerContext* context,const api::CreateFlightRequest* r,api::FlightSample* out){
    Prepared p;
    const auto shown=impl_->bridge.snapshot();
    try{p=prepare(*r,r->has_utc_unix_seconds()?r->utc_unix_seconds():shown?shown->simulation.utcSeconds:time::simulation().read().utcSeconds);}
    catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    std::shared_ptr<const physics::AirModel> air;std::string atmosphere="U.S. Standard Atmosphere 1976";std::vector<std::string> grib;std::vector<ph::SpaceWeatherDay> days;
    if(r->weather()!=api::CreateFlightRequest::STANDARD_ATMOSPHERE){ // Weather only while Albert follows the real clock.
        const bool live=!shown||shown->simulation.live;const double now=shown?shown->clock.utcSeconds:std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        UpperAir upper=impl_->sources.upper(p.utc);air=upper.model;days=std::move(upper.days); // NRLMSISE-00 over the weather, or alone.
        std::string unavailable;
        if(r->weather()==api::CreateFlightRequest::LIVE_GFS||(live&&std::abs(p.utc-now)<=7200)){
            try{auto w=impl_->sources.weather(r->site().latitude_degrees(),r->site().longitude_degrees(),p.utc);auto model=w.model;
                if(const auto grid=std::dynamic_pointer_cast<const physics::Weather>(model))model=grid->above(air);
                air=std::move(model);grib=std::move(w.grib);}
            catch(const std::exception& e){if(r->weather()==api::CreateFlightRequest::LIVE_GFS)return{grpc::StatusCode::UNAVAILABLE,std::string("Live weather unavailable: ")+e.what()};unavailable=std::string(" (live weather unavailable: ")+e.what()+")";}
        }
        atmosphere=(air?air->source():atmosphere)+unavailable;
    }
    return impl_->create(*r,p,std::move(air),std::move(atmosphere),std::move(grib),std::move(days),[&]{return context->IsCancelled();},out);
}
grpc::Status FlightServices::ExportFlight(grpc::ServerContext*,const api::Empty*,grpc::ServerWriter<api::FlightLogChunk>* writer){
    std::string bytes;
    {std::lock_guard lock(impl_->mutex);if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
        api::FlightLog log=impl_->log;log.set_steps(impl_->record);log.set_state_hash(impl_->flight->hash());log.set_tick(impl_->tick);bytes=log.SerializeAsString();}
    for(std::size_t at=0;at<bytes.size();at+=1u<<20){api::FlightLogChunk c;c.set_data(bytes.substr(at,1u<<20));if(!writer->Write(c))return{grpc::StatusCode::CANCELLED,"Client stopped reading"};}
    return grpc::Status::OK;
}
grpc::Status FlightServices::ReplayFlight(grpc::ServerContext* context,grpc::ServerReader<api::FlightLogChunk>* reader,api::FlightSample* out){
    std::string bytes;api::FlightLogChunk c;
    while(reader->Read(&c)){bytes+=c.data();if(bytes.size()>(512u<<20))return{grpc::StatusCode::RESOURCE_EXHAUSTED,"Flight log above 512 MiB"};}
    api::FlightLog log;if(!log.ParseFromString(bytes))return{grpc::StatusCode::INVALID_ARGUMENT,"Malformed flight log"};
    const auto& r=log.request();Prepared p;
    try{p=prepare(r,r.utc_unix_seconds());}catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    // Air from the embedded inputs only: never the network.
    std::shared_ptr<const physics::AirModel> air;std::vector<ph::SpaceWeatherDay> days;std::vector<std::string> grib(log.gfs().begin(),log.gfs().end());
    for(const auto& d:log.space_weather()){ph::SpaceWeatherDay x;x.day=d.day();x.f107=d.f107();x.f107a=d.f107a();x.apAverage=d.ap_average();x.type=d.type();x.hasAp=d.ap_size()==8;for(int i=0;i<d.ap_size()&&i<8;++i)x.ap[static_cast<std::size_t>(i)]=d.ap(i);days.push_back(x);}
    try{
        if(!days.empty())air=providers::msisFrom(days,p.utc);
        if(!grib.empty()){std::vector<std::vector<providers::GribField>> hours;for(const auto& b:grib)hours.push_back(providers::decodeGrib2(b));
            const auto grid=std::make_shared<const physics::Weather>(providers::gfsGrid(hours,log.atmosphere().substr(0,log.atmosphere().find(" + "))));air=grid->above(air);}
    }catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,std::string("Flight log air data: ")+e.what()};}
    const std::string expected=air?air->source():"U.S. Standard Atmosphere 1976";
    if(log.atmosphere().rfind(expected,0)!=0)return{grpc::StatusCode::FAILED_PRECONDITION,"The log does not carry the air data it used ("+log.atmosphere()+")"};
    api::FlightSample created;
    if(auto s=impl_->create(r,p,std::move(air),log.atmosphere(),std::move(grib),std::move(days),[&]{return context->IsCancelled();},&created);!s.ok())return s;
    const std::string& steps=log.steps();
    for(std::size_t at=0;at<steps.size();){
        auto get=[&](void* d,std::size_t n){if(at+n>steps.size())throw std::invalid_argument("Truncated flight log");std::memcpy(d,steps.data()+at,n);at+=n;};
        std::uint32_t ticks=0,count=0;std::vector<Impl::Command> commands;
        try{get(&ticks,4);get(&count,4);if(count>64)throw std::invalid_argument("Malformed flight log step");commands.resize(count);for(auto& k:commands){get(&k.kind,4);get(&k.a,8);get(&k.b,8);get(&k.c,8);}}
        catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
        std::lock_guard lock(impl_->mutex);
        if(auto s=impl_->run(commands,ticks,[&]{return context->IsCancelled();});!s.ok())return s;
    }
    std::lock_guard lock(impl_->mutex);impl_->fill(out);
    if(out->state_hash()!=log.state_hash()||out->tick()!=log.tick())return{grpc::StatusCode::DATA_LOSS,"Replay diverged from the recorded state"};
    return grpc::Status::OK;
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
    if(impl_->pacing)return{grpc::StatusCode::FAILED_PRECONDITION,"Real-time mode is running"};
    if(auto s=impl_->run(commands,r->ticks(),[&]{return context->IsCancelled();});!s.ok())return s;
    impl_->fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::GetFlight(grpc::ServerContext*,const api::Empty*,api::FlightSample* out){
    std::lock_guard lock(impl_->mutex);if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};impl_->fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::EndFlight(grpc::ServerContext*,const api::Empty*,api::Applied* out){
    impl_->stopPacing();std::lock_guard lock(impl_->mutex);if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
    impl_->flight.reset();(void)impl_->bridge.submit(core::FlightTelemetry{});const auto reply=wait(impl_->bridge.submit(core::Remove{objectId}));out->set_revision(reply.revision);
    return reply.code==core::Code::Ok||reply.code==core::Code::NotFound?grpc::Status::OK:grpc::Status{grpc::StatusCode::UNAVAILABLE,reply.message};
}
grpc::Status FlightServices::StartRealtime(grpc::ServerContext*,const api::RealtimeRequest* r,api::RealtimeStatus* out){
    const double rate=r->rate()==0?1.:r->rate();
    if(!(rate>0&&rate<=100)||!(r->command_timeout_seconds()>=0)||r->sensor_delay_ticks()>100000||r->actuator_delay_ticks()>100000)return{grpc::StatusCode::INVALID_ARGUMENT,"Invalid real-time settings"};
    impl_->stopPacing();std::lock_guard lock(impl_->mutex);
    if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
    auto& rt=impl_->realtime;rt=Impl::Realtime{};rt.rate=rate;rt.sensor=r->sensor_delay_ticks();rt.actuator=r->actuator_delay_ticks();rt.every=r->sample_every_ticks()?r->sample_every_ticks():10;
    rt.timeout=r->command_timeout_seconds();rt.missing=r->missing();rt.command=std::chrono::steady_clock::now();
    impl_->pacing=true;impl_->pacer=std::thread([this]{impl_->pace();});impl_->status(out);return grpc::Status::OK;
}
grpc::Status FlightServices::StopRealtime(grpc::ServerContext*,const api::Empty*,api::RealtimeStatus* out){
    impl_->stopPacing();std::lock_guard lock(impl_->mutex);impl_->status(out);return grpc::Status::OK;
}
}
