#include "flight.hpp"
#include "flight_wire.hpp"
#include "wire.hpp"
#include "core/simulation.hpp"
#include "physics/vehicles.hpp"
#include "physics/sensors.hpp"
#include "physics/weather.hpp"
#include "providers/gfs.hpp"
#include "providers/space_weather.hpp"
#include "providers/terrain.hpp"
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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
// Vehicle definitions (api::VehicleDefinition) to and from the core's model; the core checks the physics (masses, inertias,
// engines), this the shape and size of what arrives.
ph::V3 get(const api::Vector3& v){return{v.x(),v.y(),v.z()};}
ph::Table table(const api::AeroTable& t,const std::string& what){
    if(t.mach_size()==0&&t.angle_degrees_size()==0&&t.values_size()==0)return{{0},{0},{0}};
    if(t.mach_size()<1||t.angle_degrees_size()<1||t.mach_size()>128||t.angle_degrees_size()>128||t.values_size()!=t.mach_size()*t.angle_degrees_size())throw std::invalid_argument("Malformed "+what+" table");
    ph::Table r{{t.mach().begin(),t.mach().end()},{t.angle_degrees().begin(),t.angle_degrees().end()},{t.values().begin(),t.values().end()}};
    for(const auto* b:{&r.mach,&r.angle})for(std::size_t i=0;i<b->size();++i)if(!finite((*b)[i])||(i&&!((*b)[i]>(*b)[i-1])))throw std::invalid_argument(what+" table breakpoints must increase");
    for(double v:r.values)if(!finite(v))throw std::invalid_argument("Non-finite "+what+" coefficient");
    return r;
}
void increasing(const std::vector<double>& b,const std::string& what){for(std::size_t i=0;i<b.size();++i)if(!finite(b[i])||(i&&!(b[i]>b[i-1])))throw std::invalid_argument(what+" table breakpoints must increase");}
ph::Table3 table(const api::SurfaceTable& t,const std::string& what){
    if(t.mach_size()==0&&t.angle_degrees_size()==0&&t.deflection_degrees_size()==0&&t.values_size()==0)return{};
    if(t.mach_size()<1||t.angle_degrees_size()<1||t.deflection_degrees_size()<1||t.mach_size()>64||t.angle_degrees_size()>64||t.deflection_degrees_size()>64
        ||t.values_size()!=t.mach_size()*t.angle_degrees_size()*t.deflection_degrees_size())throw std::invalid_argument("Malformed "+what+" table");
    ph::Table3 r{{t.mach().begin(),t.mach().end()},{t.angle_degrees().begin(),t.angle_degrees().end()},{t.deflection_degrees().begin(),t.deflection_degrees().end()},{t.values().begin(),t.values().end()}};
    increasing(r.mach,what);increasing(r.angle,what);increasing(r.deflection,what);for(double v:r.values)if(!finite(v))throw std::invalid_argument("Non-finite "+what+" coefficient");
    return r;
}
ph::Vehicle vehicleFrom(const api::VehicleDefinition& d){
    if(d.stages_size()<1||d.stages_size()>32||d.name().size()>128)throw std::invalid_argument("A vehicle needs 1-32 stages and a name of at most 128 bytes");
    ph::Vehicle v;v.name=d.name().empty()?"Custom vehicle":d.name();v.torque=d.attitude_torque_newton_meters();v.diameter=d.has_diameter_meters()?d.diameter_meters():1.;
    v.sunArea=d.solar_area_meters2();v.reflectivity=d.has_reflectivity()?d.reflectivity():1.3;v.jetDamping=!d.has_jet_damping()||d.jet_damping();v.inertiaRate=d.has_inertia_rate()?d.inertia_rate():1.;
    if(!(v.torque>=0)||!(v.diameter>0)||!(v.sunArea>=0)||!(v.reflectivity>=0)||!finite(v.inertiaRate)||!std::isfinite(v.torque))throw std::invalid_argument("Invalid vehicle torque, diameter, solar area, reflectivity or inertia rate");
    for(const auto& sd:d.stages()){
        if(sd.engines_size()>64||sd.legs_meters_size()>32||sd.surfaces_size()>32||sd.thrusters_size()>64)throw std::invalid_argument("At most 64 engines, 32 legs, 32 surfaces and 64 thrusters a stage");
        ph::Stage st;st.name=sd.name().substr(0,128);st.propellant=sd.propellant_kilograms();st.massFull=sd.mass_full_kilograms();st.massEmpty=sd.mass_empty_kilograms();
        st.cgFull=sd.cg_full_meters();st.cgEmpty=sd.cg_empty_meters();st.inertiaFull=get(sd.inertia_full_kilogram_meters2());st.inertiaEmpty=get(sd.inertia_empty_kilogram_meters2());
        st.length=sd.length_meters();st.reference=sd.aerodynamic_reference_meters();st.noseRadius=sd.has_nose_radius_meters()?sd.nose_radius_meters():1.;
        st.legStiffness=sd.leg_stiffness_newtons_per_meter();st.legDamping=sd.leg_damping_newton_seconds_per_meter();st.friction=sd.has_friction()?sd.friction():.6;
        for(double x:{st.cgFull,st.cgEmpty,st.reference})if(!finite(x))throw std::invalid_argument("Non-finite position: "+st.name);
        if(!(st.length>0)||!(st.noseRadius>0)||!(st.legStiffness>=0)||!(st.legDamping>=0)||!(st.friction>=0)||!std::isfinite(st.length+st.legStiffness+st.legDamping+st.friction))throw std::invalid_argument("Invalid length, nose radius or legs: "+st.name);
        for(const auto& l:sd.legs_meters()){const ph::V3 p=get(l);if(!finite(p.x+p.y+p.z))throw std::invalid_argument("Non-finite leg: "+st.name);st.legs.push_back(p);}
        if(!st.legs.empty()&&!(st.legStiffness>0))throw std::invalid_argument("Legs need a stiffness: "+st.name);
        for(const auto& e:sd.engines())st.engines.push_back({e.name().substr(0,128),get(e.position_meters()),e.has_direction()?get(e.direction()):ph::V3{1,0,0},e.vacuum_thrust_newtons(),
            e.exhaust_velocity_meters_per_second(),e.exit_area_meters2(),e.has_min_throttle()?e.min_throttle():1.,e.gimbal_radians(),e.gimbal_rate_radians_per_second()});
        for(const auto& f:sd.surfaces()){ph::Surface x;x.name=f.name().substr(0,128);x.limit=f.limit_radians();x.rate=f.rate_radians_per_second();
            x.lift=table(f.lift(),"surface lift");x.drag=table(f.drag(),"surface drag");x.side=table(f.side(),"surface side force");x.roll=table(f.roll(),"surface roll");x.pitch=table(f.pitch(),"surface pitch");x.yaw=table(f.yaw(),"surface yaw");
            st.surfaces.push_back(std::move(x));}
        for(const auto& t:sd.thrusters())st.rcs.push_back({t.name().substr(0,128),get(t.position_meters()),get(t.direction()),t.thrust_newtons(),t.exhaust_velocity_meters_per_second()});
        const auto& a=sd.aerodynamics();auto& ae=st.aero;ae.area=a.reference_area_meters2();ae.length=a.reference_length_meters();ae.span=a.reference_span_meters();
        if(!(ae.area>=0)||!std::isfinite(ae.area)||!finite(ae.length)||!finite(ae.span))throw std::invalid_argument("Invalid aerodynamic references: "+st.name);
        if(ae.length==0)ae.length=1;if(ae.span==0)ae.span=1;
        ae.lift=table(a.lift(),"lift");ae.drag=table(a.drag(),"drag");ae.side=table(a.side(),"side force");ae.roll=table(a.roll(),"roll");ae.pitch=table(a.pitch(),"pitch");ae.yaw=table(a.yaw(),"yaw");
        ae.rollDamping=a.roll_damping();ae.pitchDamping=a.pitch_damping();ae.yawDamping=a.yaw_damping();
        if(!finite(ae.rollDamping+ae.pitchDamping+ae.yawDamping))throw std::invalid_argument("Non-finite damping: "+st.name);
        v.stages.push_back(std::move(st));
    }
    return v;
}
void put(const ph::Table& t,api::AeroTable* o){for(double x:t.mach)o->add_mach(x);for(double x:t.angle)o->add_angle_degrees(x);for(double x:t.values)o->add_values(x);}
void put(const ph::Table3& t,api::SurfaceTable* o){for(double x:t.mach)o->add_mach(x);for(double x:t.angle)o->add_angle_degrees(x);for(double x:t.deflection)o->add_deflection_degrees(x);for(double x:t.values)o->add_values(x);}
void put(const ph::Vehicle& v,api::VehicleDefinition* o){
    o->set_name(v.name);o->set_attitude_torque_newton_meters(v.torque);o->set_diameter_meters(v.diameter);o->set_solar_area_meters2(v.sunArea);o->set_reflectivity(v.reflectivity);
    o->set_jet_damping(v.jetDamping);o->set_inertia_rate(v.inertiaRate);
    for(const auto& st:v.stages){auto* d=o->add_stages();
        d->set_name(st.name);d->set_propellant_kilograms(st.propellant);d->set_mass_full_kilograms(st.massFull);d->set_mass_empty_kilograms(st.massEmpty);d->set_cg_full_meters(st.cgFull);d->set_cg_empty_meters(st.cgEmpty);
        put(st.inertiaFull,d->mutable_inertia_full_kilogram_meters2());put(st.inertiaEmpty,d->mutable_inertia_empty_kilogram_meters2());d->set_length_meters(st.length);
        d->set_aerodynamic_reference_meters(st.reference);d->set_nose_radius_meters(st.noseRadius);for(const auto& l:st.legs)put(l,d->add_legs_meters());
        d->set_leg_stiffness_newtons_per_meter(st.legStiffness);d->set_leg_damping_newton_seconds_per_meter(st.legDamping);d->set_friction(st.friction);
        auto engines=st.engines;if(engines.empty()&&st.thrust>0)engines.push_back({st.name,{st.nozzle,0,0},{1,0,0},st.thrust,st.exhaust,st.exitArea,st.minThrottle,st.gimbal,0}); // The shorthand, as the core reads it.
        for(const auto& e:engines){auto* x=d->add_engines();x->set_name(e.name);put(e.position,x->mutable_position_meters());put(e.direction,x->mutable_direction());x->set_vacuum_thrust_newtons(e.thrust);
            x->set_exhaust_velocity_meters_per_second(e.exhaust);x->set_exit_area_meters2(e.exitArea);x->set_min_throttle(e.minThrottle);x->set_gimbal_radians(e.gimbal);x->set_gimbal_rate_radians_per_second(e.gimbalRate);}
        const auto& ae=st.aero;auto* a=d->mutable_aerodynamics();a->set_reference_area_meters2(ae.area);a->set_reference_length_meters(ae.length);a->set_reference_span_meters(ae.span);
        put(ae.lift,a->mutable_lift());put(ae.drag,a->mutable_drag());put(ae.side,a->mutable_side());put(ae.roll,a->mutable_roll());put(ae.pitch,a->mutable_pitch());put(ae.yaw,a->mutable_yaw());
        a->set_roll_damping(ae.rollDamping);a->set_pitch_damping(ae.pitchDamping);a->set_yaw_damping(ae.yawDamping);
        for(const auto& f:st.surfaces){auto* x=d->add_surfaces();x->set_name(f.name);x->set_limit_radians(f.limit);x->set_rate_radians_per_second(f.rate);
            put(f.lift,x->mutable_lift());put(f.drag,x->mutable_drag());put(f.side,x->mutable_side());put(f.roll,x->mutable_roll());put(f.pitch,x->mutable_pitch());put(f.yaw,x->mutable_yaw());}
        for(const auto& t:st.rcs){auto* x=d->add_thrusters();x->set_name(t.name);put(t.position,x->mutable_position_meters());put(t.direction,x->mutable_direction());x->set_thrust_newtons(t.thrust);x->set_exhaust_velocity_meters_per_second(t.exhaust);}
    }
}
ph::Vehicle preset(const api::Vehicle kind,const std::optional<double> payloadKilograms){
    switch(kind){
    case api::GENERIC_LAUNCHER:{const double payload=payloadKilograms.value_or(5000.);
        if(!(payload>=0&&payload<=30000))throw std::invalid_argument("Payload must be 0-30000 kg");return ph::genericLauncher(payload);}
    case api::NESC_TWO_STAGE:return ph::nescTwoStage();
    case api::NESC_SPHERE:return ph::nescSphere();
    case api::GENERIC_LANDER:return ph::genericLander();
    case api::GENERIC_LUNAR_ASCENT:return ph::genericLunarAscent();
    case api::GENERIC_MARS_ASCENT:return ph::genericMarsAscent();
    case api::SATURN_V:return ph::saturnV();
    default:throw std::invalid_argument("Unknown vehicle");
    }
}
struct Prepared {ph::Vehicle vehicle;double utc=0,pitch=90,seconds=.001,coast=1;unsigned degree=20;ph::Center body=ph::Center::earth;};
// Air off Earth: the body's climatology.
const char* climatology(ph::Center c){return c==ph::Center::moon?"Vacuum (no lunar atmosphere)":"NASA Glenn Mars mean atmosphere";}
void checkSensors(const api::SensorSettings& q){ // Finite, non-negative values; positive periods; boresights not zero.
    auto ok=[](bool has,double v,bool positive){return !has||(std::isfinite(v)&&(positive?v>0:v>=0));};
    auto aim=[](bool has,const api::Vector3& b){return !has||(finite(b.x()+b.y()+b.z())&&(b.x()!=0||b.y()!=0||b.z()!=0));};
    const auto& g=q.gnss();const auto& r=q.radar_altimeter();const auto& t=q.star_tracker();
    if(!ok(g.has_horizontal_sigma_meters(),g.horizontal_sigma_meters(),false)||!ok(g.has_vertical_sigma_meters(),g.vertical_sigma_meters(),false)||!ok(g.has_velocity_sigma_meters_per_second(),g.velocity_sigma_meters_per_second(),false)
        ||!ok(g.has_drift_sigma_meters(),g.drift_sigma_meters(),false)||!ok(g.has_drift_correlation_seconds(),g.drift_correlation_seconds(),true)||!ok(g.has_period_seconds(),g.period_seconds(),true)
        ||!ok(g.has_max_height_meters(),g.max_height_meters(),false)||!ok(g.has_max_speed_meters_per_second(),g.max_speed_meters_per_second(),false))throw std::invalid_argument("Invalid GNSS settings");
    if(!aim(r.has_boresight(),r.boresight())||!ok(r.has_half_angle_radians(),r.half_angle_radians(),true)||(r.has_half_angle_radians()&&r.half_angle_radians()>=1.5707963267948966)
        ||!ok(r.has_max_range_meters(),r.max_range_meters(),true)||!ok(r.has_noise_floor_meters(),r.noise_floor_meters(),false)||!ok(r.has_noise_fraction(),r.noise_fraction(),false)||!ok(r.has_period_seconds(),r.period_seconds(),true))
        throw std::invalid_argument("Invalid radar altimeter settings");
    if(!aim(t.has_boresight(),t.boresight())||!ok(t.has_cross_sigma_radians(),t.cross_sigma_radians(),false)||!ok(t.has_roll_sigma_radians(),t.roll_sigma_radians(),false)||!ok(t.has_sun_exclusion_radians(),t.sun_exclusion_radians(),false)
        ||!ok(t.has_body_margin_radians(),t.body_margin_radians(),false)||!ok(t.has_max_rate_radians_per_second(),t.max_rate_radians_per_second(),true)||!ok(t.has_period_seconds(),t.period_seconds(),true))
        throw std::invalid_argument("Invalid star tracker settings");
}
Prepared prepare(const api::CreateFlightRequest& r,double utc){
    checkSensors(r.sensors());
    Prepared p;
    if(r.vehicle()==api::CUSTOM)p.vehicle=vehicleFrom(r.definition());
    else if(r.has_definition())throw std::invalid_argument("A definition flies only as CUSTOM");
    else p.vehicle=preset(r.vehicle(),r.has_payload_kilograms()?std::optional(r.payload_kilograms()):std::nullopt);
    if(!api::CreateFlightRequest::Body_IsValid(r.body()))throw std::invalid_argument("Unknown body");
    p.body=r.body()==api::CreateFlightRequest::MOON?ph::Center::moon:r.body()==api::CreateFlightRequest::MARS?ph::Center::mars:ph::Center::earth;const bool earth=p.body==ph::Center::earth;
    if(!earth&&r.weather()==api::CreateFlightRequest::LIVE_GFS)throw std::invalid_argument("Live weather exists only on Earth");
    const auto& site=r.site();p.pitch=r.has_pitch_degrees()?r.pitch_degrees():90.;p.seconds=r.has_step_seconds()?r.step_seconds():.001;p.coast=r.has_coast_step_seconds()?r.coast_step_seconds():1.;
    if(!(std::abs(site.latitude_degrees())<=90&&std::abs(site.longitude_degrees())<=360&&site.height_meters()>=(earth?-500:-1e4)&&site.height_meters()<=(earth?1e4:2.5e4)))throw std::invalid_argument("Invalid launch site");
    if(!finite(r.heading_degrees())||!(std::abs(p.pitch)<=90)||!(p.seconds>=1e-5&&p.seconds<=.1)||!(p.coast>=0&&p.coast<=10))throw std::invalid_argument("Invalid heading, pitch, step or coast step");
    if(!(utc>=time::Simulation::earliestUtc&&utc<=time::Simulation::latestUtc))throw std::invalid_argument("Instant outside 1900-2100");
    p.utc=utc;p.degree=r.has_gravity_degree()?r.gravity_degree():earth?20u:100u;const int maximum=earth?ph::egm2008Maximum:providers::gravityMaximum(p.body);
    if(p.degree==1||p.degree>static_cast<unsigned>(maximum))throw std::invalid_argument("Gravity degree must be 0 (point mass) or 2-"+std::to_string(maximum));
    return p;
}
}
struct FlightServices::Impl {
    struct Command {std::uint32_t kind;double a=0,b=0,c=0;};
    core::Bridge& bridge;AirSources sources;std::unique_ptr<wire::Server> data;std::mutex mutex;std::optional<ph::Flight> flight;std::optional<ph::Imu> imu;ph::Imu::Reading reading;std::string atmosphere;
    // Navigation sensors, each measuring at the end of the step that completes its period.
    std::optional<ph::Gnss> gnss;std::optional<ph::RadarAltimeter> radar;std::optional<ph::StarTracker> tracker;
    ph::Gnss::Reading gnssReading;ph::RadarAltimeter::Reading radarReading;ph::StarTracker::Reading trackerReading;double gnssLast=0,radarLast=0,trackerLast=0;
    void sense(){
        const double now=elapsed();const auto& s=flight->state();
        const bool due=(gnss&&now-gnssLast>=gnss->model().period-1e-9)||(radar&&now-radarLast>=radar->model().period-1e-9)||(tracker&&now-trackerLast>=tracker->model().period-1e-9);if(!due)return;
        const ph::Sample x=flight->sample();
        if(gnss&&now-gnssLast>=gnss->model().period-1e-9){gnssReading=gnss->measure(now,now-gnssLast,x.fixedPosition,x.fixedVelocity,ph::nedFromFixed(x.geodetic.latitude,x.geodetic.longitude),
            x.geodetic.height,ph::norm(x.nedVelocity),center==ph::Center::earth);gnssLast=now;}
        if(radar&&now-radarLast>=radar->model().period-1e-9){const ph::V3 b=radar->model().boresight;radarReading=radar->measure(now,x.geodetic.height-flight->environment().ground,ph::matrix(x.nedAttitude)*(b/ph::norm(b)));radarLast=now;}
        if(tracker&&now-trackerLast>=tracker->model().period-1e-9){const ph::V3 sun=flight->sunFromVehicle(),p=s.position;const double r=ph::norm(p);
            trackerReading=tracker->measure(now,s.attitude,sun/ph::norm(sun),p*(-1/r),std::asin(std::min(1.,flight->environment().planet.radius/r)),s.rate);trackerLast=now;}
    }
    ph::Tt step=ph::second/1000,start=0;std::uint64_t tick=0;int shown=0;std::vector<api::FlightEvent> events;
    std::chrono::steady_clock::time_point published{};
    // The keeper republishes a flight that is not stepping (paused, before real time) when the scene's
    // state of its central body changes, e.g. once Horizons data replaces ERFA's (Mars: ~1000 km).
    ph::V3 publishedOrigin{};double publishedUtc=-1e300;std::thread keeper;std::mutex keeperLock;std::condition_variable keeperWake;bool keeperStop=false;
    void keep(){
        std::unique_lock l(keeperLock);
        while(!keeperWake.wait_for(l,std::chrono::milliseconds(500),[this]{return keeperStop;})){
            l.unlock();
            {std::lock_guard g(mutex);if(flight&&!pacing&&center!=ph::Center::earth&&atInstant()&&ph::norm(origin(flight->state().time).position-publishedOrigin)>1)publish(true);}
            l.lock();
        }
    }
    // The scene shows the flight's instant. Elsewhere (a live or scrubbed clock) the slow drift of the
    // Horizons correction with the scene's time (~0.1 m/s for Mars) must not republish: that re-frames
    // the camera and holds the clock at the flight's instant.
    bool atInstant()const{const auto snap=bridge.snapshot();return snap&&std::abs(snap->utcSeconds-ph::utcFromTt(flight->state().time))<1;}
    void stopKeeper(){{std::lock_guard l(keeperLock);keeperStop=true;}keeperWake.notify_all();if(keeper.joinable())keeper.join();}
    Impl(core::Bridge& b,AirSources s):bridge(b),sources(std::move(s)){
        if(!sources.weather)sources.weather=[](double lat,double lon,double utc){auto d=providers::gfs().fetch(lat,lon,utc);return LiveAir{d.model,std::move(d.grib)};};
        if(!sources.upper)sources.upper=[](double utc){UpperAir a;try{a.model=providers::spaceWeather().msis(utc,&a.days);}catch(const std::exception&){a={};}return a;};
        if(!sources.gravity)sources.gravity=[](ph::Center c,int degree){return providers::gravityModels().load(c,degree);};
        if(!sources.relief)sources.relief=[](ph::Center c,double lat,double lon){DownloadProgress p;return providers::planetaryRelief().height(c,lat,lon,p);};
    }
    // Replay log: the creation and every committed step (u32 ticks, u32 count, then per command u32 kind and three doubles).
    api::FlightLog log;std::string record;
    // Real time: the pacing thread, its settings, delayed commands and samples.
    struct Realtime {double rate=1;std::uint32_t sensor=0,actuator=0,every=10;double timeout=0;int missing=0;
        std::deque<std::pair<std::uint64_t,std::vector<Command>>> queue;std::deque<std::pair<wire::Response,bool>> samples;std::uint32_t carry=0; // Samples: sent flag; carry: events of dropped samples.
        std::chrono::steady_clock::time_point command;bool policyApplied=false;std::uint64_t late=0,policies=0;double worst=0;};
    Realtime realtime;std::thread pacer;std::atomic_bool pacing{false};std::mutex control,sleeper;std::condition_variable wake; // control: start/stop of the pacer.
    void status(api::RealtimeStatus* o){o->set_running(pacing);o->set_tick(tick);o->set_late_ticks(realtime.late);o->set_worst_lag_seconds(realtime.worst);o->set_policy_events(realtime.policies);}
    void stopPacing(){std::lock_guard c(control);stopLocked();} // Without the flight mutex held.
    void stopLocked(){{std::lock_guard l(sleeper);pacing=false;}wake.notify_all();if(pacer.joinable())pacer.join();}
    void pace(){
        const auto begin=std::chrono::steady_clock::now();std::uint64_t n=0;
        while(pacing){
            const auto deadline=begin+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(static_cast<double>(++n)*ph::secondsSinceJ2000(step)/realtime.rate));
            {std::unique_lock l(sleeper);if(wake.wait_until(l,deadline,[&]{return !pacing.load();}))break;}
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
            wire::Response r;fill(r);r.reserved=rt.sensor;rt.samples.push_back({r,false});
            while(rt.samples.size()>rt.sensor+1){if(!rt.samples.front().second)rt.carry|=rt.samples.front().first.events;rt.samples.pop_front();}
            if(tick%rt.every==0&&rt.samples.size()==rt.sensor+1){auto& f=rt.samples.front();out=f.first;out->events|=rt.carry;rt.carry=0;f.second=true;}}
            if(out)(void)data->push(*out);
        }
    }
    // Records: u32 repeat, u32 ticks, u32 count, then per command u32 kind and three doubles;
    // identical consecutive steps only bump the repeat. Capped at 256 MiB (replay unavailable).
    std::size_t lastRecord=std::string::npos;bool truncated=false;
    void note(const std::vector<Command>& commands,std::uint32_t ticks){
        if(truncated)return;
        std::string r;auto put=[&](const void* p,std::size_t n){r.append(static_cast<const char*>(p),n);};
        const auto count=static_cast<std::uint32_t>(commands.size());put(&ticks,4);put(&count,4);
        for(const auto& c:commands){put(&c.kind,4);put(&c.a,8);put(&c.b,8);put(&c.c,8);}
        if(lastRecord!=std::string::npos&&record.compare(lastRecord+4,std::string::npos,r)==0){std::uint32_t n;std::memcpy(&n,record.data()+lastRecord,4);if(n<0xffffffffu){++n;std::memcpy(record.data()+lastRecord,&n,4);return;}}
        if(record.size()+r.size()+4>(256u<<20)){truncated=true;return;}
        lastRecord=record.size();const std::uint32_t one=1;record.append(reinterpret_cast<const char*>(&one),4);record+=r;
    }
    double elapsed()const{return ph::secondsSinceJ2000(flight->state().time-start);}
    // Geocentric ICRF state of the central body where the scene draws it: the scene's own
    // (Horizons) state carried to t by the ERFA motion since, else ERFA alone.
    ph::PositionVelocity origin(ph::Tt t)const{
        ph::PositionVelocity e=ph::geocentric(center,t);if(center==ph::Center::earth)return e;
        const char* id=center==ph::Center::moon?"moon":"mars";
        if(const auto snap=bridge.snapshot())for(const auto& o:snap->objects)if(o.id==id&&!o.samples.empty()&&(o.samples.front().positionMeters.x!=0||o.samples.front().positionMeters.y!=0||o.samples.front().positionMeters.z!=0)){ // Zero: no state yet.
            const auto& s=o.samples.front();const ph::V3 then=ph::geocentric(center,ph::ttFromUtc(s.utcSeconds)).position;
            e.position+=ph::V3{s.positionMeters.x,s.positionMeters.y,s.positionMeters.z}-then;break;}
        return e;
    }
    core::Sample scene()const{ // Cylinder axis (local y) along body x, centred on the stack.
        const auto& s=flight->state();const auto& st=flight->vehicle().stages[s.stage];
        const double f=st.propellant>0?s.propellant/st.propellant:0,cg=st.cgEmpty+(st.cgFull-st.cgEmpty)*f;
        const ph::Q q=s.attitude*ph::Q{std::sqrt(.5),0,0,-std::sqrt(.5)};const ph::PositionVelocity o=origin(s.time);
        const ph::V3 p=o.position+s.position+ph::matrix(s.attitude)*ph::V3{cg-st.length/2,0,0},v=o.velocity+s.velocity;
        return core::Sample{ph::utcFromTt(s.time),{p.x,p.y,p.z},{v.x,v.y,v.z},core::Quaternion{q.x,q.y,q.z,q.w},true};
    }
    core::Object object()const{
        const auto& v=flight->vehicle();const double length=v.stages[flight->state().stage].length;
        core::Object o;o.id=objectId;o.name=v.name;o.source="Albert flight physics";o.appearance={core::Shape::Cylinder,{v.diameter,length,v.diameter},0xe8e8e8};
        o.radiusMeters=length/2;o.defaultDistanceMeters=6*length;o.minDistanceMeters=length;o.samples.push_back(scene());return o;
    }
    void publish(bool force){
        const auto now=std::chrono::steady_clock::now();
        if(!force&&now-published<std::chrono::milliseconds(33))return;
        published=now;const core::Sample s=scene();const ph::V3 at=origin(flight->state().time).position;
        sound.store(center==ph::Center::earth?std::make_shared<const Sound>(heard()):nullptr); // Read by the frame loop without the lock.
        // The stack got shorter, or the same instant again (the keeper; appending would be refused as
        // out of order): replace the cylinder, keeping the camera on it.
        if(flight->state().stage!=shown||s.utcSeconds<=publishedUtc){
            const auto snap=bridge.snapshot();const bool followed=snap&&snap->camera.target==objectId;
            (void)bridge.submit(core::Remove{objectId});
            if(wait(bridge.submit(core::Create{object()})).code!=core::Code::Ok){shown=-1;return;} // Retried on the next publication.
            shown=flight->state().stage;if(followed)(void)bridge.submit(core::Focus{objectId,false});
        }else (void)bridge.submit(core::Append{objectId,{s}});
        publishedUtc=s.utcSeconds;publishedOrigin=at;
        (void)bridge.submit(core::SetTime{std::clamp(s.utcSeconds,time::Simulation::earliestUtc,time::Simulation::latestUtc),{},false,true});
        (void)bridge.submit(telemetry());
    }
    // The flight as a sound source (see FlightServices::sound); published with the scene, read without the mutex (the
    // frame loop must not wait on a step that waits on the frame loop).
    std::atomic<std::shared_ptr<const Sound>> sound;
    Sound heard()const{
        const ph::Sample x=flight->sample();const auto& st=flight->vehicle().stages[flight->state().stage];
        Sound s;s.utc=ph::utcFromTt(flight->state().time);s.planet=flight->environment().planet;s.air=flight->environment().air;auto& e=s.emission;
        e.time=s.utc;e.position=x.fixedPosition;e.velocity=x.fixedVelocity;
        const ph::V3 nose=flight->environment().rotation.at(flight->state().time).fixed*(ph::matrix(flight->state().attitude)*ph::V3{1,0,0});e.plume=nose*-1.;
        double exhaust=0,area=0,flow=0; // The engines on as one plume: their exit area, flow-weighted exhaust velocity.
        for(std::size_t i=0;i<st.engines.size();++i)if(flight->engines()[i].on){const auto& n=st.engines[i];const double f=flight->engines()[i].throttle*n.thrust/n.exhaust;exhaust+=f*n.exhaust;flow+=f;area+=n.exitArea;}
        e.noise={x.thrust,flow>0?exhaust/flow:0.,std::sqrt(4*area/3.14159265358979323846),.0035};e.on=flight->state().burning&&x.thrust>0;
        return s;
    }
    core::FlightTelemetry telemetry()const{
        const ph::Sample x=flight->sample();const auto& st=flight->state();
        return{true,st.burning,st.clamped,st.landed,flight->vehicle().name,atmosphere,models(),elapsed(),x.geodetic.height,ph::norm(x.nedVelocity),x.mach,x.dynamicPressure,x.mass,st.propellant,x.air.wind.x,x.air.wind.y,st.stage,static_cast<int>(flight->vehicle().stages.size())};
    }
    grpc::Status create(const api::CreateFlightRequest& r,const Prepared& p,std::shared_ptr<const physics::AirModel> air,std::string atmosphere,
        std::vector<std::string> grib,std::vector<ph::SpaceWeatherDay> days,providers::GravityModel gravity,const std::function<bool()>& cancelled,api::FlightSample* out);
    ph::Center center=ph::Center::earth;unsigned degree=0;api::PhysicsSettings physics; // The flight's models, resolved.
    std::mutex defaultsLock;api::PhysicsSettings defaults;                        // Albert's (the Física menu), for unset fields.
    std::string models()const{ // The models in words, for the app's Física page.
        std::string on,off;auto add=[&](bool x,const char* name){std::string& to=x?on:off;to+=to.empty()?"":", ";to+=name;};
        std::string g=degree==0?"gravidade de massa pontual":"gravidade "+std::to_string(degree)+"x"+std::to_string(degree);on=g;
        add(physics.third_bodies(),center==ph::Center::moon?"Terra e Sol":center==ph::Center::mars?"Sol":"Sol e Lua");add(physics.relativity(),"relatividade");
        add(physics.atmosphere(),"atmosfera");add(physics.wind(),"vento");
        if(physics.turbulence_meters_per_second()>0&&physics.atmosphere()){char t[64];std::snprintf(t,sizeof t,"turbulencia %.1f m/s",physics.turbulence_meters_per_second());on+=", ";on+=t;}add(physics.solar_radiation_pressure()&&flight->vehicle().sunArea>0,"radiacao solar");
        const auto& st=flight->vehicle().stages[static_cast<std::size_t>(flight->state().stage)];
        if(!st.engines.empty())on+=", "+std::to_string(st.engines.size())+(st.engines.size()==1?" motor":" motores");
        if(!st.rcs.empty())on+=", RCS ("+std::to_string(st.rcs.size())+")";if(!st.surfaces.empty())on+=", superficies ("+std::to_string(st.surfaces.size())+")";
        if(!st.legs.empty())on+=", pernas";
        return off.empty()?on:on+"; sem "+off;
    }
    std::uint32_t eventBits=0; // For the data plane, since its last response.
    grpc::Status run(const std::vector<Command>& commands,std::uint32_t ticks,const std::function<bool()>& cancelled){
        if(!flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
        if(ticks>600000)return{grpc::StatusCode::INVALID_ARGUMENT,"At most 600000 ticks per step"};
        if(commands.size()>64)return{grpc::StatusCode::INVALID_ARGUMENT,"At most 64 commands per step"};
        ph::Flight next=*flight; // Commands apply atomically.
        try{for(const auto& c:commands){const int e=static_cast<int>(c.kind>>16)-1; // The engine in the high bits: 0 every one.
            const std::uint32_t k=c.kind&0xffff;
            if(e>=0&&k!=wire::ignite&&k!=wire::cutoff&&k!=wire::throttle&&k!=wire::gimbal&&k!=wire::fail&&k!=wire::thruster&&k!=wire::surface)throw std::invalid_argument("This command takes no index");
            switch(c.kind&0xffff){
            case wire::ignite:next.ignite(e);break;case wire::cutoff:next.cutoff(e);break;case wire::separate:next.separate();break;case wire::release:next.release();break;
            case wire::throttle:next.throttle(c.a,e);break;case wire::gimbal:next.gimbal(c.a,c.b,e);break;case wire::torque:next.torque({c.a,c.b,c.c});break;
            case wire::fail:if(e<0)throw std::invalid_argument("Fail needs an engine");next.fail(e);break;
            case wire::thruster:next.thruster(c.a,e);break;case wire::surface:next.surface(c.a,e);break;
            default:throw std::invalid_argument("Unknown command");}}}
        catch(const std::invalid_argument& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}catch(const std::logic_error& e){return{grpc::StatusCode::FAILED_PRECONDITION,e.what()};}
        // Integrated on the copy and committed whole: a cancelled call leaves the flight untouched.
        bool changed=std::any_of(commands.begin(),commands.end(),[](const Command& c){return (c.kind&0xffff)<=wire::release||(c.kind&0xffff)==wire::fail;});
        const std::size_t pending=events.size();std::uint32_t bits=0;
        auto event=[&](api::FlightEvent::Kind kind,std::uint32_t bit){api::FlightEvent e;e.set_kind(kind);e.set_elapsed_seconds(ph::secondsSinceJ2000(next.state().time-start));e.set_stage(static_cast<unsigned>(next.state().stage));events.push_back(std::move(e));bits|=bit;changed=true;};
        const ph::Tt total=step*static_cast<ph::Tt>(ticks),chunk=step*4096;
        for(ph::Tt left=total;left>0;){
            if(cancelled()){events.resize(pending);return{grpc::StatusCode::CANCELLED,"Step cancelled; flight unchanged"};}
            const auto before=next.state();left-=next.advance(std::min(left,chunk));const auto& after=next.state();
            if((before.clamped&&!after.clamped)||(before.contact&&!after.contact))event(api::FlightEvent::LIFTOFF,wire::liftoff);
            if(before.burning&&!after.burning&&after.propellant<=0)event(api::FlightEvent::DEPLETED,wire::depleted);
            if((!before.landed&&after.landed)||(!before.contact&&after.contact))event(api::FlightEvent::TOUCHDOWN,wire::touchdown);
        }
        if(cancelled()){events.resize(pending);return{grpc::StatusCode::CANCELLED,"Step cancelled; flight unchanged"};}
        ph::Imu sensor=*imu;const auto inc=next.increments();
        if(inc.seconds>0)reading=sensor.measure(inc.seconds,inc.velocity/inc.seconds,inc.angle/inc.seconds,inc.velocity,inc.angle,next.sample().air.pressure);
        *flight=std::move(next);*imu=std::move(sensor);tick+=ticks;eventBits|=bits;note(commands,ticks);if(ticks)sense();
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
        o.stage=static_cast<std::uint32_t>(s.stage);o.flags=(s.burning?wire::burning:0u)|(s.clamped?wire::clamped:0u)|(s.landed?wire::landed:0u)|(s.contact?wire::contact:0u);o.events=eventBits;eventBits=0;o.hash=flight->hash();
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
        o->set_burning(s.burning);o->set_clamped(s.clamped);o->set_landed(s.landed);o->set_leg_contact(s.contact);o->set_heat_load_joules_per_meter2(s.heatLoad);o->set_heat_flux_watts_per_meter2(x.heatFlux);o->set_throttle(s.throttle);
        o->mutable_gimbal()->set_pitch_radians(s.gimbalPitch);o->mutable_gimbal()->set_yaw_radians(s.gimbalYaw);
        {const auto thrust=flight->engineThrust();const auto& es=v.stages[static_cast<std::size_t>(s.stage)].engines;
         for(std::size_t i=0;i<es.size();++i){const auto& e=flight->engines()[i];auto* x=o->add_engines();x->set_name(es[i].name);x->set_on(e.on&&s.burning);x->set_failed(e.failed);x->set_throttle(e.throttle);
            x->mutable_gimbal()->set_pitch_radians(e.pitch);x->mutable_gimbal()->set_yaw_radians(e.yaw);x->mutable_gimbal_command()->set_pitch_radians(e.targetPitch);x->mutable_gimbal_command()->set_yaw_radians(e.targetYaw);x->set_thrust_newtons(thrust[i]);}
         const auto& fs=v.stages[static_cast<std::size_t>(s.stage)].surfaces;
         for(std::size_t i=0;i<fs.size();++i){auto* x=o->add_surfaces();x->set_name(fs[i].name);x->set_deflection_radians(flight->surfaces()[i].deflection);x->set_command_radians(flight->surfaces()[i].target);}
         for(double u:flight->thrusters())o->add_thruster_duty(u);put(x.rcsForce,o->mutable_thruster_force_newtons());put(x.rcsMoment,o->mutable_thruster_moment_newton_meters());}
        for(auto& e:events)*o->add_events()=std::move(e);events.clear();
        *o->mutable_physics()=physics;
        if(gnss){auto* g=o->mutable_gnss();g->set_valid(gnssReading.valid);g->set_elapsed_seconds(gnssReading.time);put(gnssReading.position,g->mutable_position_ecef_meters());put(gnssReading.velocity,g->mutable_velocity_ecef_meters_per_second());}
        if(radar){auto* g=o->mutable_radar_altimeter();g->set_valid(radarReading.valid);g->set_elapsed_seconds(radarReading.time);g->set_range_meters(radarReading.range);}
        if(tracker){auto* g=o->mutable_star_tracker();g->set_valid(trackerReading.valid);g->set_elapsed_seconds(trackerReading.time);put(trackerReading.attitude,g->mutable_attitude());}
        o->set_state_hash(flight->hash());o->set_vehicle(v.name);o->set_step_seconds(ph::secondsSinceJ2000(step));o->set_atmosphere(atmosphere);put(x.air.wind,o->mutable_wind_ned_meters_per_second());o->set_data_port(data?data->port():0);
        put(reading.specificForce,o->mutable_measured_specific_force_meters_per_second2());put(reading.rate,o->mutable_measured_angular_rate_radians_per_second());
        put(reading.deltaVelocity,o->mutable_delta_velocity_meters_per_second());put(reading.deltaAngle,o->mutable_delta_angle_radians());o->set_measured_static_pressure_pascals(reading.pressure);
    }
};
FlightServices::FlightServices(core::Bridge& bridge,AirSources sources):impl_(std::make_unique<Impl>(bridge,std::move(sources))){
    impl_->data=std::make_unique<wire::Server>([this](const wire::Request& q,wire::Response& r){
        std::vector<Impl::Command> commands; // Fixed order: fail, cutoff, separate, release, ignite, throttle, gimbal, torque.
        const std::uint32_t engine=std::min<std::uint32_t>(q.engine,0xffff)<<16; // Out of range: refused by the flight.
        for(const std::uint32_t k:{wire::fail,wire::cutoff,wire::separate,wire::release,wire::ignite})if(q.commands&k)commands.push_back({k==wire::separate||k==wire::release?k:k|engine});
        if(q.commands&wire::throttle)commands.push_back({wire::throttle|engine,q.throttleValue});
        if(q.commands&wire::gimbal)commands.push_back({wire::gimbal|engine,q.gimbalPitch,q.gimbalYaw});
        if(q.commands&wire::torque)commands.push_back({wire::torque,q.torqueValue[0],q.torqueValue[1],q.torqueValue[2]});
        std::lock_guard lock(impl_->mutex);
        if(!impl_->flight){r=wire::Response{};r.status=wire::noFlight;return true;}
        if(impl_->pacing){auto& rt=impl_->realtime;rt.queue.push_back({impl_->tick+rt.actuator,std::move(commands)});rt.command=std::chrono::steady_clock::now();rt.policyApplied=false;return false;}
        const auto s=impl_->run(commands,q.ticks,[]{return false;});impl_->fill(r);
        r.status=s.ok()?wire::ok:s.error_code()==grpc::StatusCode::INVALID_ARGUMENT?wire::invalid:wire::precondition;
        return true;
    });
    impl_->keeper=std::thread([i=impl_.get()]{i->keep();}); // After the data plane, whose constructor may throw.
}
FlightServices::~FlightServices(){impl_->stopPacing();impl_->stopKeeper();impl_->data.reset();}
grpc::Status FlightServices::Impl::create(const api::CreateFlightRequest& r,const Prepared& p,std::shared_ptr<const physics::AirModel> air,std::string atmosphere,
    std::vector<std::string> grib,std::vector<ph::SpaceWeatherDay> days,providers::GravityModel gravity,const std::function<bool()>& cancelled,api::FlightSample* out){
    const auto& site=r.site();const ph::Tt t=ph::ttFromUtc(p.utc),stepNs=static_cast<ph::Tt>(std::llround(p.seconds*1e9));
    ph::Environment env=ph::environment(p.body,gravity.gravity,static_cast<int>(p.degree));env.ground=site.height_meters();
    const auto& m=r.physics();auto on=[](bool has,bool value){return !has||value;}; // Logs from before the switches: all on.
    env.sunMoon=on(m.has_third_bodies(),m.third_bodies());env.relativity=on(m.has_relativity(),m.relativity());env.atmosphere=on(m.has_atmosphere(),m.atmosphere());
    env.wind=on(m.has_wind(),m.wind());env.solarPressure=on(m.has_solar_radiation_pressure(),m.solar_radiation_pressure());
    env.turbulence=m.turbulence_meters_per_second();env.turbulenceSeed=m.turbulence_seed();
    if(!(env.turbulence>=0&&env.turbulence<=50))return{grpc::StatusCode::INVALID_ARGUMENT,"Turbulence must be 0-50 m/s"};
    if(p.body==ph::Center::earth){env.air=air;env.planet.degree=static_cast<int>(p.degree);env.rotation=ph::Rotation::iau(ph::secondsSinceJ2000(t)-(p.utc-946728000.));}
    std::optional<ph::Flight> made;
    try{
        if(!r.has_start())made.emplace(p.vehicle,env,ph::Flight::pad(env,p.vehicle,t,{site.latitude_degrees()*deg,site.longitude_degrees()*deg,site.height_meters()},r.heading_degrees()*deg,p.pitch*deg));
        else{ // In flight: from the local horizon or the inertial frame, at the given stage and propellant.
            const auto& w=r.start();if(w.stage()>=p.vehicle.stages.size())throw std::invalid_argument("No such stage to start in");
            const double full=p.vehicle.stages[w.stage()].propellant,left=w.has_propellant_kilograms()?w.propellant_kilograms():full;
            if(!(left>=0&&left<=full))throw std::invalid_argument("Propellant must be 0 to the stage's");
            ph::State s;
            if(w.has_local()){const auto& l=w.local();const auto& g=l.position();
                if(!(std::abs(g.latitude_degrees())<=90)||!finite(g.longitude_degrees())||!(g.height_meters()>-1e4&&g.height_meters()<1e9)||!finite(l.heading_degrees()+l.pitch_degrees()+l.roll_degrees()))throw std::invalid_argument("Invalid local start");
                s=ph::Flight::free(env,p.vehicle,t,{g.latitude_degrees()*deg,g.longitude_degrees()*deg,g.height_meters()},get(l.velocity_ned_meters_per_second()),{l.heading_degrees()*deg,l.pitch_degrees()*deg,l.roll_degrees()*deg},get(l.angular_rate_radians_per_second()));}
            else if(w.has_inertial()){const auto& i=w.inertial();const auto& q=i.attitude();const double n=std::sqrt(q.w()*q.w()+q.x()*q.x()+q.y()*q.y()+q.z()*q.z());
                s.time=t;s.position=get(i.position_meters());s.velocity=get(i.velocity_meters_per_second());s.rate=get(i.angular_rate_radians_per_second());
                if(!(n>0)||!finite(n)||!finite(s.position.x+s.position.y+s.position.z+s.velocity.x+s.velocity.y+s.velocity.z+s.rate.x+s.rate.y+s.rate.z)||!(ph::norm(s.position)>1))throw std::invalid_argument("Invalid inertial start");
                s.attitude={q.w()/n,q.x()/n,q.y()/n,q.z()/n};}
            else throw std::invalid_argument("A start needs a local or inertial state");
            for(const double x:{s.velocity.x,s.velocity.y,s.velocity.z})if(!finite(x))throw std::invalid_argument("Invalid start velocity");
            s.stage=static_cast<int>(w.stage());s.propellant=left;made.emplace(p.vehicle,env,s);
        }
    }
    catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    made->steps(stepNs,static_cast<ph::Tt>(std::llround(p.coast*1e9)));
    if(cancelled())return{grpc::StatusCode::CANCELLED,"Flight creation cancelled; current flight kept"};
    stopPacing();std::lock_guard lock(mutex);
    flight=std::move(made);step=stepNs;start=t;tick=0;events.clear();eventBits=0;this->atmosphere=atmosphere;center=p.body;degree=p.degree;
    physics.set_third_bodies(env.sunMoon);physics.set_relativity(env.relativity);physics.set_atmosphere(env.atmosphere);physics.set_wind(env.wind);physics.set_solar_radiation_pressure(env.solarPressure);
    physics.set_turbulence_meters_per_second(env.turbulence);physics.set_turbulence_seed(env.turbulenceSeed);
    { // The IMU starts with one sample at the pad.
        const auto g=r.imu().grade();imu.emplace(ph::imuModel(g==api::ImuSettings::TACTICAL?ph::ImuGrade::tactical:g==api::ImuSettings::INDUSTRIAL?ph::ImuGrade::industrial:ph::ImuGrade::ideal),r.imu().seed());
        const ph::Sample x=flight->sample();const double dt=p.seconds;
        reading=imu->measure(dt,x.specificForce,flight->state().rate,x.specificForce*dt,flight->state().rate*dt,x.air.pressure);
    }
    { // Navigation sensors, from the request (seeded apart from the IMU), measuring once at the pad.
        const auto& q=r.sensors();const std::uint64_t seed=q.seed();gnss.reset();radar.reset();tracker.reset();gnssLast=radarLast=trackerLast=-1e300;
        auto v=[](bool has,double value,double fallback){return has?value:fallback;};
        if(q.gnss().enabled()){const auto& g=q.gnss();ph::GnssModel m;m.horizontal=v(g.has_horizontal_sigma_meters(),g.horizontal_sigma_meters(),m.horizontal);m.vertical=v(g.has_vertical_sigma_meters(),g.vertical_sigma_meters(),m.vertical);
            m.velocity=v(g.has_velocity_sigma_meters_per_second(),g.velocity_sigma_meters_per_second(),m.velocity);m.drift=v(g.has_drift_sigma_meters(),g.drift_sigma_meters(),m.drift);
            m.correlation=v(g.has_drift_correlation_seconds(),g.drift_correlation_seconds(),m.correlation);m.period=v(g.has_period_seconds(),g.period_seconds(),m.period);
            m.maxHeight=v(g.has_max_height_meters(),g.max_height_meters(),m.maxHeight);m.maxSpeed=v(g.has_max_speed_meters_per_second(),g.max_speed_meters_per_second(),m.maxSpeed);gnss.emplace(m,seed^0x676e7373ull);}
        if(q.radar_altimeter().enabled()){const auto& g=q.radar_altimeter();ph::RadarModel m;if(g.has_boresight())m.boresight=get(g.boresight());m.halfAngle=v(g.has_half_angle_radians(),g.half_angle_radians(),m.halfAngle);
            m.maxRange=v(g.has_max_range_meters(),g.max_range_meters(),m.maxRange);m.floor=v(g.has_noise_floor_meters(),g.noise_floor_meters(),m.floor);m.fraction=v(g.has_noise_fraction(),g.noise_fraction(),m.fraction);
            m.period=v(g.has_period_seconds(),g.period_seconds(),m.period);radar.emplace(m,seed^0x7261646172ull);}
        if(q.star_tracker().enabled()){const auto& g=q.star_tracker();ph::StarTrackerModel m;if(g.has_boresight())m.boresight=get(g.boresight());m.cross=v(g.has_cross_sigma_radians(),g.cross_sigma_radians(),m.cross);
            m.roll=v(g.has_roll_sigma_radians(),g.roll_sigma_radians(),m.roll);m.sunExclusion=v(g.has_sun_exclusion_radians(),g.sun_exclusion_radians(),m.sunExclusion);
            m.bodyMargin=v(g.has_body_margin_radians(),g.body_margin_radians(),m.bodyMargin);m.maxRate=v(g.has_max_rate_radians_per_second(),g.max_rate_radians_per_second(),m.maxRate);
            m.period=v(g.has_period_seconds(),g.period_seconds(),m.period);tracker.emplace(m,seed^0x73746172ull);}
        sense();
    }
    log=api::FlightLog{};record.clear();lastRecord=std::string::npos;truncated=false;log.set_engine("Albert Engine "+std::string(ALBERT_VERSION));*log.mutable_request()=r;log.mutable_request()->set_utc_unix_seconds(p.utc);log.set_atmosphere(atmosphere);
    for(auto& b:grib)log.add_gfs(std::move(b));
    log.set_gravity(std::move(gravity.table));
    for(const auto& d:days){auto* x=log.add_space_weather();x->set_day(d.day);x->set_f107(d.f107);x->set_f107a(d.f107a);x->set_ap_average(d.apAverage);x->set_type(d.type);if(d.hasAp)for(double a:d.ap)x->add_ap(a);}
    shown=0;(void)wait(bridge.submit(core::Remove{objectId}));
    if(auto s=failed(wait(bridge.submit(core::Create{object()})));!s.ok())return s;
    publishedOrigin=origin(flight->state().time).position;publishedUtc=ph::utcFromTt(flight->state().time); // The keeper's reference.
    if(auto s=failed(wait(bridge.submit(core::SetTime{ph::utcFromTt(t),0.,false})));!s.ok())return s;
    if(r.focus_camera())(void)wait(bridge.submit(core::Focus{objectId,true}));
    (void)bridge.submit(telemetry()); // The app's flight panel and Fisica page, before the first step.
    published=std::chrono::steady_clock::now();fill(out);return grpc::Status::OK;
}
grpc::Status FlightServices::CreateFlight(grpc::ServerContext* context,const api::CreateFlightRequest* request,api::FlightSample* out){
    api::CreateFlightRequest resolved=*request;const api::CreateFlightRequest* r=&resolved;
    { // Unset physics switches from Albert's defaults, recorded: the log replays without them.
        std::lock_guard l(impl_->defaultsLock);const auto& d=impl_->defaults;auto* m=resolved.mutable_physics();auto pick=[](bool has,bool value){return !has||value;};
        if(!m->has_third_bodies())m->set_third_bodies(pick(d.has_third_bodies(),d.third_bodies()));if(!m->has_relativity())m->set_relativity(pick(d.has_relativity(),d.relativity()));
        if(!m->has_atmosphere())m->set_atmosphere(pick(d.has_atmosphere(),d.atmosphere()));if(!m->has_wind())m->set_wind(pick(d.has_wind(),d.wind()));
        if(!m->has_solar_radiation_pressure())m->set_solar_radiation_pressure(pick(d.has_solar_radiation_pressure(),d.solar_radiation_pressure()));
        if(!m->has_turbulence_meters_per_second())m->set_turbulence_meters_per_second(d.turbulence_meters_per_second());
    }
    if(r->terrain_height()){ // Recorded as an explicit height: replay needs no terrain data.
        const auto& site=r->site();
        if(!(std::abs(site.latitude_degrees())<=90)||!finite(site.longitude_degrees())||!api::CreateFlightRequest::Body_IsValid(r->body()))return{grpc::StatusCode::INVALID_ARGUMENT,"Invalid launch site"};
        try{DownloadProgress progress;const double lon=site.longitude_degrees()-360.*std::floor((site.longitude_degrees()+180.)/360.);double h;
            if(r->body()==api::CreateFlightRequest::EARTH)h=providers::copernicusDem().height(site.latitude_degrees(),lon,progress)+ph::geoidHeight(site.latitude_degrees()*deg,lon*deg);
            else h=impl_->sources.relief(r->body()==api::CreateFlightRequest::MOON?ph::Center::moon:ph::Center::mars,site.latitude_degrees(),lon);
            resolved.mutable_site()->set_height_meters(h);resolved.set_terrain_height(false);}
        catch(const std::invalid_argument& e){return{grpc::StatusCode::INVALID_ARGUMENT,std::string("Terrain: ")+e.what()};}
        catch(const std::exception& e){return{grpc::StatusCode::UNAVAILABLE,std::string("Terrain unavailable: ")+e.what()};}
    }
    Prepared p;
    const auto shown=impl_->bridge.snapshot();
    try{p=prepare(*r,r->has_utc_unix_seconds()?r->utc_unix_seconds():shown?shown->simulation.utcSeconds:time::simulation().read().utcSeconds);}
    catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
    std::shared_ptr<const physics::AirModel> air;std::string atmosphere="U.S. Standard Atmosphere 1976";std::vector<std::string> grib;std::vector<ph::SpaceWeatherDay> days;providers::GravityModel gravity;
    if(p.body!=ph::Center::earth){atmosphere=climatology(p.body);
        // A point mass (degree 0) takes the field's GM.
        try{gravity=impl_->sources.gravity(p.body,static_cast<int>(std::max(p.degree,2u)));}catch(const std::exception& e){return{grpc::StatusCode::UNAVAILABLE,std::string("Gravity field unavailable: ")+e.what()};}
    }else if(r->weather()!=api::CreateFlightRequest::STANDARD_ATMOSPHERE){ // Weather only while Albert follows the real clock.
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
    return impl_->create(*r,p,std::move(air),std::move(atmosphere),std::move(grib),std::move(days),std::move(gravity),[&]{return context->IsCancelled();},out);
}
grpc::Status FlightServices::GetVehicle(grpc::ServerContext*,const api::VehicleRequest* r,api::VehicleDefinition* out){
    try{put(preset(r->vehicle(),r->has_payload_kilograms()?std::optional(r->payload_kilograms()):std::nullopt),out);return grpc::Status::OK;}
    catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
}
void FlightServices::physicsDefaults(const api::PhysicsSettings& d){std::lock_guard l(impl_->defaultsLock);impl_->defaults=d;}
std::optional<FlightServices::Sound> FlightServices::sound()const{const auto s=impl_->sound.load();return s?std::optional<Sound>(*s):std::nullopt;}
grpc::Status FlightServices::ExportFlight(grpc::ServerContext*,const api::Empty*,grpc::ServerWriter<api::FlightLogChunk>* writer){
    std::string bytes;
    {std::lock_guard lock(impl_->mutex);if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
        if(impl_->truncated)return{grpc::StatusCode::FAILED_PRECONDITION,"Flight log exceeded 256 MiB; replay unavailable"};
        api::FlightLog log=impl_->log;log.set_steps(impl_->record);log.set_state_hash(impl_->flight->hash());log.set_tick(impl_->tick);bytes=log.SerializeAsString();}
    for(std::size_t at=0;at<bytes.size();at+=1u<<19){api::FlightLogChunk c;c.set_data(bytes.substr(at,1u<<19));if(!writer->Write(c))return{grpc::StatusCode::CANCELLED,"Client stopped reading"};} // 512 KiB: below the 1 MiB message limit.
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
    providers::GravityModel gravity;
    try{
        if(p.body!=ph::Center::earth){ // Bounded before any copy: a forged log must not inflate memory.
            const int loaded=static_cast<int>(std::max(p.degree,2u));
            if(log.gravity().size()>providers::gravityTableBytes(loaded))throw std::runtime_error("Gravity table longer than its degree allows");
            gravity=providers::gravityModel(p.body,log.gravity(),loaded);}
        if(!days.empty())air=providers::msisFrom(days,p.utc);
        if(!grib.empty()){std::vector<std::vector<providers::GribField>> hours;for(const auto& b:grib)hours.push_back(providers::decodeGrib2(b));
            const auto grid=std::make_shared<const physics::Weather>(providers::gfsGrid(hours,log.atmosphere().substr(0,log.atmosphere().find(" + "))));air=grid->above(air);}
    }catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,std::string("Flight log air or gravity data: ")+e.what()};}
    const std::string expected=p.body!=ph::Center::earth?climatology(p.body):air?air->source():"U.S. Standard Atmosphere 1976";
    if(log.atmosphere().rfind(expected,0)!=0)return{grpc::StatusCode::FAILED_PRECONDITION,"The log does not carry the air data it used ("+log.atmosphere()+")"};
    api::FlightSample created;
    if(auto s=impl_->create(r,p,std::move(air),log.atmosphere(),std::move(grib),std::move(days),std::move(gravity),[&]{return context->IsCancelled();},&created);!s.ok())return s;
    const std::string& steps=log.steps();
    for(std::size_t at=0;at<steps.size();){
        auto get=[&](void* d,std::size_t n){if(at+n>steps.size())throw std::invalid_argument("Truncated flight log");std::memcpy(d,steps.data()+at,n);at+=n;};
        std::uint32_t repeat=0,ticks=0,count=0;std::vector<Impl::Command> commands;
        try{get(&repeat,4);get(&ticks,4);get(&count,4);if(count>64||repeat==0)throw std::invalid_argument("Malformed flight log step");commands.resize(count);for(auto& k:commands){get(&k.kind,4);get(&k.a,8);get(&k.b,8);get(&k.c,8);}}
        catch(const std::exception& e){return{grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
        for(std::uint32_t i=0;i<repeat;++i){std::lock_guard lock(impl_->mutex);
            if(auto s=impl_->run(commands,ticks,[&]{return context->IsCancelled();});!s.ok())return s;}
    }
    std::lock_guard lock(impl_->mutex);impl_->fill(out);
    if(out->state_hash()!=log.state_hash()||out->tick()!=log.tick())return{grpc::StatusCode::DATA_LOSS,"Replay diverged from the recorded state"};
    return grpc::Status::OK;
}
grpc::Status FlightServices::Step(grpc::ServerContext* context,const api::StepRequest* r,api::FlightSample* out){
    std::vector<Impl::Command> commands;
    for(const auto& c:r->commands()){
        if(c.index()>0xfffe)return{grpc::StatusCode::INVALID_ARGUMENT,"No such engine, thruster or surface"};
        const std::uint32_t e=c.index()<<16; // In the kind's high bits (the replay log keeps the record format).
        switch(c.command_case()){
        case api::FlightCommand::kIgnite:if(c.ignite())commands.push_back({wire::ignite|e});break;
        case api::FlightCommand::kCutoff:if(c.cutoff())commands.push_back({wire::cutoff|e});break;
        case api::FlightCommand::kSeparate:if(c.separate())commands.push_back({wire::separate|e});break;
        case api::FlightCommand::kRelease:if(c.release())commands.push_back({wire::release|e});break;
        case api::FlightCommand::kThrottle:commands.push_back({wire::throttle|e,c.throttle()});break;
        case api::FlightCommand::kGimbal:commands.push_back({wire::gimbal|e,c.gimbal().pitch_radians(),c.gimbal().yaw_radians()});break;
        case api::FlightCommand::kTorqueNewtonMeters:commands.push_back({wire::torque|e,c.torque_newton_meters().x(),c.torque_newton_meters().y(),c.torque_newton_meters().z()});break;
        case api::FlightCommand::kFail:if(c.fail())commands.push_back({wire::fail|e});break;
        case api::FlightCommand::kThrusterDuty:commands.push_back({wire::thruster|e,c.thruster_duty()});break;
        case api::FlightCommand::kSurfaceRadians:commands.push_back({wire::surface|e,c.surface_radians()});break;
        default:return{grpc::StatusCode::INVALID_ARGUMENT,"Empty command"};
        }
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
    impl_->flight.reset();impl_->sound.store(nullptr);(void)impl_->bridge.submit(core::FlightTelemetry{});const auto reply=wait(impl_->bridge.submit(core::Remove{objectId}));out->set_revision(reply.revision);
    return reply.code==core::Code::Ok||reply.code==core::Code::NotFound?grpc::Status::OK:grpc::Status{grpc::StatusCode::UNAVAILABLE,reply.message};
}
grpc::Status FlightServices::StartRealtime(grpc::ServerContext*,const api::RealtimeRequest* r,api::RealtimeStatus* out){
    const double rate=r->rate()==0?1.:r->rate();
    if(!(rate>0&&rate<=100)||!(r->command_timeout_seconds()>=0)||r->sensor_delay_ticks()>100000||r->actuator_delay_ticks()>100000)return{grpc::StatusCode::INVALID_ARGUMENT,"Invalid real-time settings"};
    std::lock_guard control(impl_->control);impl_->stopLocked();std::lock_guard lock(impl_->mutex);
    if(!impl_->flight)return{grpc::StatusCode::FAILED_PRECONDITION,"No flight"};
    auto& rt=impl_->realtime;rt=Impl::Realtime{};rt.rate=rate;rt.sensor=r->sensor_delay_ticks();rt.actuator=r->actuator_delay_ticks();rt.every=r->sample_every_ticks()?r->sample_every_ticks():10;
    rt.timeout=r->command_timeout_seconds();rt.missing=r->missing();rt.command=std::chrono::steady_clock::now();
    impl_->pacing=true;impl_->pacer=std::thread([this]{impl_->pace();});impl_->status(out);return grpc::Status::OK;
}
grpc::Status FlightServices::StopRealtime(grpc::ServerContext*,const api::Empty*,api::RealtimeStatus* out){
    impl_->stopPacing();std::lock_guard lock(impl_->mutex);impl_->status(out);return grpc::Status::OK;
}
}
