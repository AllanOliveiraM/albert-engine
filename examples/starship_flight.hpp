#pragma once
// A Starship-class mission over the flight API, in lockstep:
// - the stack and then the ship by the generic ascent program (gravity_turn.hpp);
// - the booster back to the tower by BoosterReturn (starship.hpp);
// - about a day later, the ship home to the same tower by ShipReturn (ship_return.hpp).
#include "flight_api.hpp"
#include "ship_return.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
namespace albert::example::starship {
// The tower at Starbase: its base, the arms' height over it, the bars' radius; launches toward `heading` (degrees from north).
constexpr double towerLatitude=25.9969,towerLongitude=-97.1573,towerGround=5,armHeight=100,armRadius=.25,armLip=.2,heading=93;
inline Vec towerSite(){ // WGS 84 (ECEF, m).
    const double a=6378137,e2=6.69437999014e-3,phi=towerLatitude*deg,lam=towerLongitude*deg,n=a/std::sqrt(1-e2*std::sin(phi)*std::sin(phi));
    return{(n+towerGround)*std::cos(phi)*std::cos(lam),(n+towerGround)*std::cos(phi)*std::sin(lam),(n*(1-e2)+towerGround)*std::sin(phi)};
}
inline double pinRest(){return std::sqrt(armRadius*armRadius-armLip*armLip);} // The pins over the bars' axes in the groove (m).
// The arms hinge on the carriage at the tower's east face (east of the tower's centre at towerX, its half width half) and swing
// at 8°/s, open 30° apart from the closed catch (the north one to the north) while a vehicle goes up or comes down past them.
constexpr double towerX=-22,half=4.75,armPivot=towerX+half+1.5,armSwing=8*deg,armOpen=30*deg;
// Starbase around the tower as it looks (drawn only; its places and sizes approximate, from photographs): the tower, a 9.5 m
// lattice 146 m tall west of the mount, its carriage and catch arms (the rails above are the structures), the ship's quick-
// disconnect arm swung back, the launch mount (a ring the stack stands on, its clamps), the concrete pad, the tank farm
// (upright and lying tanks) and its propellant line.
inline void addStarbase(api::CreateFlightRequest& create){
    using Shape=api::SceneryPart;constexpr std::uint32_t lattice=0x6b6e71,dark=0x46484b,concrete=0x9d9a94,white=0xe4e4e2,tankSteel=0xb9bdc0;
    auto part=[&](const char* name,Shape::Shape shape,Vec at,Vec size,std::uint32_t colour,double heading=0,double tilt=0,double inner=0){auto* x=create.add_scenery();
        x->set_name(name);x->set_shape(shape);put(x->mutable_centre_enu_meters(),at);put(x->mutable_size_meters(),size);x->set_color(colour);x->set_heading_degrees(heading);
        x->set_tilt_degrees(tilt);x->set_inner_diameter_meters(inner);};
    constexpr double beam=3.2,depth=4;
    part("pad",Shape::BOX,{-25,-30,-4},{170,170,4},concrete);
    part("tower",Shape::TRUSS,{towerX,0,69},{2*half,2*half,146},lattice);
    part("tower crown",Shape::BOX,{towerX,0,143},{2*half+1,2*half+1,3},dark);
    part("lightning mast",Shape::CYLINDER,{towerX,0,152},{.6,.6,15},dark);
    part("carriage",Shape::BOX,{towerX,0,armHeight-armRadius-depth/2},{2*half+3,2*half+3,depth+3},dark);
    for(const double side:{-1.,1.}){const double y=side*(pinOut+.8);const double from=armPivot,to=13;const unsigned on=side>0?3:1; // The catch arms, under their rails.
        part("catch arm",Shape::TRUSS,{(from+to)/2,y,armHeight-armRadius-depth/2},{beam,depth,to-from},lattice,90,90);create.mutable_scenery(create.scenery_size()-1)->set_structure(on);
        part("arm deck",Shape::BOX,{(from+to)/2,y,armHeight-armRadius-.15},{to-from,beam,.3},dark);create.mutable_scenery(create.scenery_size()-1)->set_structure(on);}
    part("ship arm",Shape::TRUSS,{towerX+half+5,half+5,78},{2.6,2.6,13},lattice,45,90);
    part("mount",Shape::CYLINDER,{0,0,-1.5},{17,17,3},concrete,0,0,9.6);
    for(int i=0;i<20;++i){const double a=i*18*deg; // The hold-down clamps on the ring's lip.
        part("clamp",Shape::BOX,{5.1*std::sin(a),5.1*std::cos(a),.25},{.8,1.2,.5},dark,i*18.);}
    for(int i=0;i<8;++i)part("tank",Shape::CYLINDER,{-100.+12*(i%4),-62.-13*(i/4),13},{9,9,30},i%2?white:tankSteel); // Upright: oxygen, methane, nitrogen.
    for(int i=0;i<4;++i)part("tank",Shape::CYLINDER,{-95.+12*i,-100,1},{4,4,24},white,0,90);  // Lying, north-south.
    {const Vec a{-60,-60,-1},b{0,-9,-1};const Vec d=b+a*-1.;part("propellant line",Shape::CYLINDER,(a+b)*.5,{1.2,1.2,norm(d)},tankSteel,std::atan2(d.x,d.y)/deg,90);}
}
// Each arm a rail of two steel bars 0.4 m apart (0.25 m round), east-west under a pin, shock-absorbed (ζ ~0.8 under the booster):
// the pin rests in the groove between them, the inner bar ~0.5 m off the hull; under it a beam (1 m round, ~1 m off the hull),
// both swinging with the arm. The
// structures: 1-2 the south rail, 3-4 the north one, 5 the south beam, 6 the north one (hinged, starting open), 7 the tower's
// column (fixed, round to its lattice's corners); the beams and the column drawn by the scenery.
inline void addTower(api::CreateFlightRequest& create){
    create.mutable_site()->set_latitude_degrees(towerLatitude);create.mutable_site()->set_longitude_degrees(towerLongitude);create.mutable_site()->set_height_meters(towerGround);
    auto add=[&](const char* name,Vec from,Vec to,double round,double side,bool drawn){auto* a=create.add_structures();a->set_name(name);put(a->mutable_start_enu_meters(),from);
        put(a->mutable_end_enu_meters(),to);a->set_radius_meters(round);a->set_stiffness_newtons_per_meter(1e8);a->set_damping_newton_seconds_per_meter(6e6);a->set_friction(.8);
        if(!drawn)a->set_drawn(false);if(side==0)return;
        put(a->mutable_hinge_enu_meters(),armPivot,side*(pinOut+.8),armHeight);a->set_swing_rate_radians_per_second(armSwing);
        a->set_min_radians(side>0?0:-armOpen);a->set_max_radians(side>0?armOpen:0);a->set_start_radians(side*armOpen);};
    for(const double side:{-1.,1.})for(const double lip:{-armLip,armLip})add("arm rail",{-10,side*pinOut+lip,armHeight},{10,side*pinOut+lip,armHeight},armRadius,side,true);
    for(const double side:{-1.,1.})add("arm beam",{armPivot,side*(pinOut+1),armHeight-armRadius-1.1},{13,side*(pinOut+1),armHeight-armRadius-1.1},1,side,false);
    add("tower",{towerX,0,-5},{towerX,0,146},half*std::sqrt(2.),0,false);
    addStarbase(create);
}
// What the guidance reads from a sample: ECEF state, body rates and the attitude as ECEF to body (Bᵀ N, N the geodetic NED).
inline State state(const api::FlightSample& s){
    State x;x.position=vec(s.position_ecef_meters());x.velocity=vec(s.velocity_ecef_meters_per_second());x.rate=vec(s.angular_rate_radians_per_second());
    x.contact=vec(s.contact_force_newtons());x.mass=s.mass_kilograms();x.propellant=s.propellant_kilograms();x.thrust=s.thrust_newtons();x.height=s.geodetic().height_meters();x.q=s.dynamic_pressure_pascals();
    x.specific=vec(s.specific_force_meters_per_second2());for(const auto& f:s.surfaces())x.surfaces.push_back(f.deflection_radians());
    for(const auto& e:s.engines())x.failed.push_back(e.failed());
    const double la=s.geodetic().latitude_degrees()*deg,lo=s.geodetic().longitude_degrees()*deg;
    const Mat b=bodyToNed(s.attitude_ned().w(),s.attitude_ned().x(),s.attitude_ned().y(),s.attitude_ned().z());
    const Mat n{{-std::sin(la)*std::cos(lo),-std::sin(la)*std::sin(lo),std::cos(la)},{-std::sin(lo),std::cos(lo),0},{-std::cos(la)*std::cos(lo),-std::cos(la)*std::sin(lo),-std::sin(la)}};
    x.toBody={n.r0*b.r0.x+n.r1*b.r1.x+n.r2*b.r2.x,n.r0*b.r0.y+n.r1*b.r1.y+n.r2*b.r2.y,n.r0*b.r0.z+n.r1*b.r1.z+n.r2*b.r2.z};
    return x;
}
// The catch arms open (a vehicle going up or coming down past them) or closed (catching it), by a step's commands.
inline void arms(api::StepRequest& q,bool open){ // Structures 1, 2 and 5 the south arm's, 3, 4 and 6 the north one's.
    for(unsigned i=1;i<=6;++i){auto* k=q.add_commands();k->set_index(i);k->set_structure_radians(open?(i<=2||i==5?-armOpen:armOpen):0.);}
}
inline bool armsClosed(const api::FlightSample& s){return std::all_of(s.structure_radians().begin(),s.structure_radians().end(),[](double a){return std::abs(a)<.2*deg;});}
inline void apply(api::StepRequest& q,const Command& c){ // A guidance command as flight commands (indices from 1).
    for(int i:c.ignite){auto* k=q.add_commands();k->set_ignite(true);k->set_index(static_cast<unsigned>(i));}
    for(int i:c.cutoff){auto* k=q.add_commands();k->set_cutoff(true);k->set_index(static_cast<unsigned>(i));}
    if(c.throttle>=0)q.add_commands()->set_throttle(c.throttle);
    auto* g=q.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw);
    for(const auto& e:c.gimbals){auto* k=q.add_commands();k->set_index(static_cast<unsigned>(e.engine));k->mutable_gimbal()->set_pitch_radians(e.pitch);k->mutable_gimbal()->set_yaw_radians(e.yaw);}
    for(std::size_t i=0;i<c.surfaces.size();++i){auto* k=q.add_commands();k->set_surface_radians(c.surfaces[i]);k->set_index(static_cast<unsigned>(i+1));}
    if(c.torqueOn){auto* t=q.add_commands()->mutable_torque_newton_meters();t->set_x(c.torque.x);t->set_y(c.torque.y);t->set_z(c.torque.z);}
}
// Where a vehicle is over the ground from the tower's base: east and north (m).
inline std::pair<double,double> aside(const api::FlightSample& s){
    const double la=towerLatitude*deg,lo=towerLongitude*deg;const Vec d=vec(s.position_ecef_meters())+towerSite()*-1.;
    return{dot(d,Vec{-std::sin(lo),std::cos(lo),0}),dot(d,Vec{-std::sin(la)*std::cos(lo),-std::sin(la)*std::sin(lo),std::cos(la)})};
}
// How far a vehicle (upright, `length` m, 4.5 m round) keeps from the tower's lattice while its lowest end is below the
// tower's top: its axis to the tower's centre less both half-widths (m; huge when above it).
inline double clearance(const api::FlightSample& s,double length){
    if(s.geodetic().height_meters()-towerGround-length/2>150)return 1e9;
    const auto [east,north]=aside(s);return std::hypot(east-towerX,north)-radius-half*std::sqrt(2.);
}
inline double offAxis(const api::FlightSample& s,Vec up){const Vec rel=vec(s.position_ecef_meters())+towerSite()*-1.;return norm(rel+up*-dot(rel,up));}
inline double speedOf(const api::FlightSample& s){const auto& v=s.velocity_ned_meters_per_second();return std::hypot(v.x(),v.y(),v.z());}
// Real time with a warp: each step's simulated seconds take 1/rate of them on the wall clock (no waiting when fast).
struct Pacer {
    bool fast=false;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();double wall=0,last=0;
    void operator()(double sim,double rate){if(fast){last=sim;return;}wall+=(sim-last)/rate;last=sim;
        std::this_thread::sleep_until(start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(wall)));}
};
struct Home {double deorbit=0,entry=0,maxTile=0,caught=0,offAxis=0,speed=0,propellant=0,towerClearance=1e9;int overheats=0;}; // towerClearance: see clearance().
// The ship home from orbit to a catch, then 30 s on the arms; warp: the orbit's time compression in real time. Throws on a
// failure.
// Why a vehicle broke up, if it did (the flight's Breakup), in words; null while whole.
inline const char* broken(const api::FlightSample& s){
    if(!s.has_breakup())return nullptr;
    switch(s.breakup().cause()){case api::Breakup::GROUND:return "bateu no chão";case api::Breakup::STRUCTURE:return "bateu numa estrutura";case api::Breakup::HEAT:return "superaqueceu";default:return nullptr;}
}
// A mission event for the broadcast's timeline: done now (the step's start), or planned at a UTC instant.
inline void mark(api::StepRequest& q,const char* name,double plannedUtc=-1){auto* m=q.add_milestones();m->set_name(name);if(plannedUtc>=0){m->set_utc_unix_seconds(plannedUtc);m->set_planned(true);}}
// An engine failure to inject (as the engines' own sensors would report it): on "stack" (before staging), "booster" or
// "ship", at its mission time (s) or as its guidance enters a phase (BoosterReturn's or ShipReturn's, by name), the engine's
// number (1-based). Written "flight@time:engine" or "flight@phase:engine", several joined by commas.
struct Fault {std::string flight,phase;double at=-1;int engine=0;bool done=false;};
inline std::vector<Fault> faults(const std::string& spec){
    std::vector<Fault> out;std::size_t from=0;
    while(from<spec.size()){const std::size_t end=std::min(spec.find(',',from),spec.size());const std::string x=spec.substr(from,end-from);from=end+1;
        const std::size_t a=x.find('@'),b=x.rfind(':');if(a==std::string::npos||b==std::string::npos||b<a)throw std::invalid_argument("A failure is flight@time-or-phase:engine: "+x);
        Fault f;f.flight=x.substr(0,a);const std::string when=x.substr(a+1,b-a-1);f.engine=std::stoi(x.substr(b+1));
        if(f.flight!="stack"&&f.flight!="booster"&&f.flight!="ship")throw std::invalid_argument("A failure's flight is stack, booster or ship: "+x);
        if(!when.empty()&&(std::isdigit(static_cast<unsigned char>(when[0]))||when[0]=='.'))f.at=std::stod(when);else f.phase=when;out.push_back(f);}
    return out;
}
// The failures due now on that flight (its time, its phase's name) as fail commands.
inline void inject(std::vector<Fault>& faults,const char* flight,double t,const char* phase,api::StepRequest& q){
    for(auto& f:faults)if(!f.done&&f.flight==flight&&((f.at>=0&&t>=f.at)||(f.at<0&&phase&&f.phase==phase))){
        auto* k=q.add_commands();k->set_fail(true);k->set_index(static_cast<unsigned>(f.engine));f.done=true;
        std::printf("%9.1f s  FALHA: motor %d (%s, %s)\n",t,f.engine,flight,f.at>=0?"no instante":phase);}
}
inline Home flyHome(api::FlightService::Stub& flight,api::FlightSample& ship,Pacer& pace,double warp,std::vector<Fault> faults={}){
    ShipReturn back(towerSite(),towerLatitude*deg,towerLongitude*deg,armHeight,pinRest());Home out;double report=0,caughtAt=-1;
    const char* names[]{"em órbita","orientação retrógrada","queima de saída de órbita","planeio até o ar","reentrada","giro","queima de pouso","pairando","descendo","capturada"};auto last=back.phase();
    bool open=false; // The arms: opened for the way down past them, closed over the hover.
    std::vector<std::string> marks; // Events seen since the last step, for the timeline.
    for(;;){
        const double t=ship.elapsed_seconds();back.armsClosed=armsClosed(ship);const Command c=back.step(t,state(ship));
        api::StepRequest q;q.set_ticks(static_cast<unsigned>(std::clamp(std::llround(back.nextStep()*1000),1LL,600000LL)));apply(q,c);
        {static const char* phases[]{"orbit","orient","deorbit","glide","entry","flip","landing","hover","descend","caught"};inject(faults,"ship",t,phases[static_cast<int>(back.phase())],q);}
        for(const auto& m:marks)mark(q,m.c_str());marks.clear();
        {const bool shut=back.closeArms()||back.phase()==ShipReturn::Phase::descend||back.phase()==ShipReturn::Phase::caught; // As its guidance asks.
            if(shut==open){arms(q,!shut);open=!shut;}}
        call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,q,&ship);});
        for(const auto& e:ship.events())if(e.kind()==api::FlightEvent::OVERHEAT&&e.index()<static_cast<unsigned>(ship.thermal_nodes_size())){++out.overheats;
            std::printf("%9.1f s  nave: %s acima do limite\n",ship.elapsed_seconds(),ship.thermal_nodes(static_cast<int>(e.index())).name().c_str());}
        for(const auto& n:ship.thermal_nodes())out.maxTile=std::max(out.maxTile,n.temperature_kelvin());
        out.towerClearance=std::min(out.towerClearance,clearance(ship,shipLength));
        if(back.phase()!=last){last=back.phase();const double now=ship.elapsed_seconds();
            if(last==ShipReturn::Phase::deorbit)out.deorbit=now;else if(last==ShipReturn::Phase::entry)out.entry=now;else if(last==ShipReturn::Phase::caught)caughtAt=out.caught=now;
            static const char* events[]{nullptr,nullptr,"SAÍDA DE ÓRBITA",nullptr,"REENTRADA","GIRO","QUEIMA DE POUSO DA NAVE",nullptr,nullptr,"CAPTURA DA NAVE"};
            if(const char* e=events[static_cast<int>(last)])marks.push_back(e);
            std::printf("%9.1f s  nave: %s (h %.2f km, v %.0f m/s, %.0f t de propelante, pouso previsto %.1f km além e %.1f km à direita da torre)\n",now,names[static_cast<int>(last)],
                ship.geodetic().height_meters()/1e3,speedOf(ship),ship.propellant_kilograms()/1e3,back.along()/1e3,back.beside()/1e3);}
        const double now=ship.elapsed_seconds();
        if(now>=report){const bool orbiting=last==ShipReturn::Phase::orbit;report=orbiting?std::floor(now/3600)*3600+3600:std::floor(now/30)*30+30;
            std::printf("%9.1f s  nave: h %7.1f km  v %5.0f m/s  q %6.0f Pa  inclinação lateral %4.0f°  pouso previsto %.1f km além, %.1f à direita  ladrilho mais quente %4.0f K gás %.0f kg\n",now,
                ship.geodetic().height_meters()/1e3,speedOf(ship),ship.dynamic_pressure_pascals(),back.bank()/deg,back.along()/1e3,back.beside()/1e3,out.maxTile,ship.thruster_gas_kilograms());}
        if(const char* why=broken(ship))throw std::runtime_error(std::string("A nave se desfez: ")+why+" ("+std::to_string(static_cast<int>(ship.breakup().speed_meters_per_second()))+" m/s)");
        if(ship.landed()||ship.geodetic().height_meters()<towerGround)throw std::runtime_error("A nave atingiu o solo");
        if(caughtAt>=0&&now-caughtAt>=30){out.offAxis=offAxis(ship,back.up());out.speed=speedOf(ship);out.propellant=ship.propellant_kilograms();
            std::printf("Nave: a menor folga da torre, abaixo do topo dela, %.1f m.\n",out.towerClearance);
            std::printf("Nave capturada aos %.1f s; 30 s depois, nos braços: %.2f m do eixo da torre, %.3f m/s, %.0f t de propelante; ladrilho mais quente %.0f K, %d alertas de calor.\n",
                out.caught,out.offAxis,out.speed,out.propellant/1e3,out.maxTile,out.overheats);return out;}
        if(now>4*86400)throw std::runtime_error("A nave não voltou em 4 dias");
        const auto p=back.phase();pace(now,p==ShipReturn::Phase::orbit?warp:p==ShipReturn::Phase::glide&&ship.geodetic().height_meters()>140e3?std::min(warp,60.):p==ShipReturn::Phase::orient?5:1);
    }
}
// The flight from Starbase:
// - the stack lifts off on 33 engines and cuts off with 570 t left in the booster;
// - the booster flies on as its own flight ("booster", spawned) to a catch on the tower's arms, rests there 30 s and is
//   taken off (its flight ended);
// - the ship goes on to orbit and, unless orbitOnly, home to the same arms (flyHome).
// Throws when any part fails. onSpawn: called once the booster is a flight (scene id "flight.booster").
// boosterDown: the catch given up (gaveUp: why), the booster down at sea or on the ground boosterAway m from the tower's axis.
struct Mission {double meco=0,mecoHeight=0,seco=0,perigee=0,apoapsis=0,maxQ=0,maxQAt=0,caught=0,offAxis=0,speed=0,propellant=0,towerClearance=1e9,boosterAway=0;
    bool boosterLost=false,boosterDown=false;std::string gaveUp;Home ship;};
inline Mission mission(api::FlightService::Stub& flight,bool fast,double warp=1000,bool orbitOnly=false,const std::function<void()>& onSpawn={},std::vector<Fault> faults={},double utc=0){
    Mission out;Pacer pace;pace.fast=fast;
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=stack();create.set_focus_camera(true);create.set_heading_degrees(heading);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);if(utc>0)create.set_utc_unix_seconds(utc); // Unset: the scene's instant.
    api::FlightSample ship,boost;call([&](grpc::ClientContext& c){return flight.CreateFlight(&c,create,&ship);});
    std::printf("%s de Starbase, %s\n",ship.vehicle().c_str(),fast?"o mais rápido possível":"tempo real");
    BoosterReturn back(towerSite(),towerLatitude*deg,towerLongitude*deg,armHeight,pinRest());GravityTurn ascent(heading*deg,.13); // A small kick: lofted, so the booster's way back stays short.
    bool spawned=false,orbit=false,bucket=false,coasting=false,shut=false,mirror=false,first=true,peaked=false;double report=0,maxQ=0,maxQAt=0,spawnAt=0,caughtAt=0,lostAt=-1;
    std::vector<std::string> marks; // Events seen since the last step, for the timeline.
    const bool trace=std::getenv("ALBERT_TRACE")!=nullptr;double traced=0;
    const char* labels[]{"espera","giro","boostback","costa","queima de pouso","aproximação final","pairando","descendo","capturado","desvio","no chão"};auto last=back.phase();
    static const char* names[]{"wait","flip","boostback","coast","landing","final","hover","descend","caught","divert","down"}; // For the failures.
    for(;;){
        const double t=ship.elapsed_seconds();
        api::StepRequest step;step.set_ticks(20);inject(faults,spawned?"ship":"stack",t,nullptr,step);
        if(first){const double t0=ship.utc_unix_seconds();first=false;mark(step,"DECOLAGEM"); // The public profile's times, as planned.
            for(const auto& [name,at]:std::initializer_list<std::pair<const char*,double>>{{"MAX-Q",62},{"MECO",153},{"SEPARAÇÃO A QUENTE",160},{"BOOSTBACK",168},
                {"QUEIMA DE POUSO DO BOOSTER",392},{"CAPTURA DO BOOSTER",417},{"SECO",510}})mark(step,name,t0+at);} // Flights 7 and 8 (2025), roughly.
        for(const auto& m:marks)mark(step,m.c_str());marks.clear();
        if(!spawned&&ship.stage()==0&&!ship.clamped()&&ship.propellant_kilograms()<=reserve){ // MECO and hot staging: the booster becomes its own flight.
            api::StepRequest stage;stage.add_commands()->set_cutoff(true);auto* sp=stage.add_commands()->mutable_spawn();sp->set_flight_id("booster");*sp->mutable_definition()=superHeavy();
            sp->set_nose_offset_meters(shipLength);call([&](grpc::ClientContext& c){return flight.Step(&c,stage,&ship);});
            {api::FlightRequest q;q.set_flight_id("booster");call([&](grpc::ClientContext& c){return flight.GetFlight(&c,q,&boost);});}
            std::printf("%7.2f s  MECO e separação a quente: h %.1f km, v %.0f m/s; booster com %.0f t\n",t,ship.geodetic().height_meters()/1e3,speedOf(ship),boost.propellant_kilograms()/1e3);
            spawned=true;spawnAt=t;if(onSpawn)onSpawn();mark(step,"MECO");mark(step,"SEPARAÇÃO A QUENTE");out.meco=t;out.mecoHeight=ship.geodetic().height_meters();back.start(boost.elapsed_seconds());step.add_commands()->set_ignite(true);ascent.upper(reading(ship));
        }
        if(!spawned&&!bucket&&t>=40){step.add_commands()->set_throttle(.8);bucket=true;} // Through max-Q to cutoff at 80%.
        if(mirror){arms(step,!shut);mirror=false;}
        if(orbit&&!coasting){auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(0);q->set_y(0);q->set_z(0);step.add_commands()->mutable_gimbal();coasting=true;}
        if(!orbit){const Commands c=ascent.step(reading(ship));
            if(c.ignite&&!spawned)step.add_commands()->set_ignite(true);if(c.cutoff)step.add_commands()->set_cutoff(true);
            auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(c.torque.x);q->set_y(c.torque.y);q->set_z(c.torque.z);
            auto* g=step.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw); // The Raptors steer the stack.
            if(ascent.phase()==GravityTurn::Phase::orbit){orbit=true;out.seco=t;marks.push_back("SECO");out.perigee=GravityTurn::perigee(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()));
                out.apoapsis=GravityTurn::apoapsis(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()));std::printf("%7.2f s  SECO: perigeu %.0f km, apogeu %.0f km, girando a %.2f°/s\n",t,out.perigee/1e3,out.apoapsis/1e3,norm(vec(ship.angular_rate_radians_per_second()))/deg);}}
        call([&](grpc::ClientContext& c){return flight.Step(&c,step,&ship);});
        if(const char* why=broken(ship))throw std::runtime_error(std::string(spawned?"A nave se desfez: ":"A pilha se desfez: ")+why);
        if(ship.dynamic_pressure_pascals()>maxQ){maxQ=ship.dynamic_pressure_pascals();maxQAt=ship.elapsed_seconds();}
        else if(!peaked&&maxQ>1e4&&ship.dynamic_pressure_pascals()<.97*maxQ){peaked=true;marks.push_back("MAX-Q");}
        if(spawned&&lostAt>=0){if(!out.boosterLost&&!out.boosterDown){ // Broken up: what is left falls, uncommanded, until it lies on the ground, then its flight ends.
            api::StepRequest b;b.set_flight_id("booster");b.set_ticks(20);call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,b,&boost);});
            if(boost.landed()&&!out.boosterLost){api::FlightRequest q;q.set_flight_id("booster");api::Applied a;call([&](grpc::ClientContext& c){return flight.EndFlight(&c,q,&a);});out.boosterLost=true;}}
        }
        else if(spawned){ // Caught, it stays a flight: resting on the arms.
            back.armsClosed=armsClosed(boost);const Command c=back.step(boost.elapsed_seconds(),state(boost));api::StepRequest b;b.set_flight_id("booster");b.set_ticks(20);apply(b,c); // The booster's own clock: 0 at its spawn.
            inject(faults,"booster",spawnAt+boost.elapsed_seconds(),names[static_cast<int>(back.phase())],b);
            if(back.closeArms()!=shut){shut=back.closeArms();arms(b,!shut);mirror=true;} // As its guidance asks; the site shows the ship's flight's arms: those too.
            if(back.gaveUp()&&out.gaveUp.empty()){out.gaveUp=back.gaveUp();marks.push_back("BOOSTER ABORTA A CAPTURA");const auto [east,north]=aside(boost);
                std::printf("%7.2f s  booster: captura cancelada (%s); desviando para longe da torre (h %.2f km, %.0f m a leste e %.0f m ao norte da torre)\n",spawnAt+boost.elapsed_seconds(),
                    back.gaveUp(),boost.geodetic().height_meters()/1e3,east,north);}
            try{call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,b,&boost);});}
            catch(const std::exception& e){ // A command refused (an engine with nothing to burn): the step without them.
                std::printf("%7.2f s  booster: comando recusado (%s); segue sem ele\n",spawnAt+boost.elapsed_seconds(),e.what());
                api::StepRequest bare;bare.set_flight_id("booster");bare.set_ticks(20);call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,bare,&boost);});}
            if(back.phase()!=last){last=back.phase();if(last==BoosterReturn::Phase::caught)caughtAt=spawnAt+boost.elapsed_seconds();
                if(last==BoosterReturn::Phase::boostback)marks.push_back("BOOSTBACK");else if(last==BoosterReturn::Phase::landing)marks.push_back("QUEIMA DE POUSO DO BOOSTER");
                else if(last==BoosterReturn::Phase::caught)marks.push_back("CAPTURA DO BOOSTER");
                const auto [east,north]=aside(boost);
                std::printf("%7.2f s  booster: %s (h %.2f km, v %.0f m/s, %.0f t de propelante, %.0f m a leste e %.0f m ao norte da torre, pouso previsto a %.0f m do ponto sobre o mar)\n",
                    spawnAt+boost.elapsed_seconds(),labels[static_cast<int>(last)],boost.geodetic().height_meters()/1e3,speedOf(boost),boost.propellant_kilograms()/1e3,east,north,back.predicted());}
            out.towerClearance=std::min(out.towerClearance,clearance(boost,boosterLength));
            if(trace&&back.phase()>=BoosterReturn::Phase::landing){const double now=spawnAt+boost.elapsed_seconds();if(now>=traced){traced=now+.5; // ALBERT_TRACE: its way down, twice a second.
                const auto [e,n]=aside(boost);const State x=state(boost);const Vec u=back.up(),h=x.velocity+u*-dot(x.velocity,u);int lit=0;for(const auto& en:boost.engines())lit+=en.on();
                std::printf("TRACE %.1f %s e %.1f n %.1f h %.1f ve %.2f vn %.2f vz %.2f lit %d prop %.0f arms %.2f contact %.0f\n",now,labels[static_cast<int>(back.phase())],e,n,boost.geodetic().height_meters(),
                    dot(h,Vec{-std::sin(towerLongitude*deg),std::cos(towerLongitude*deg),0}),dot(h,cross(u,Vec{-std::sin(towerLongitude*deg),std::cos(towerLongitude*deg),0})),dot(x.velocity,u),lit,
                    boost.propellant_kilograms()/1e3,boost.structure_radians_size()?boost.structure_radians(0)/deg:0.,norm(x.contact));}}
            if(const char* why=broken(boost)){lostAt=spawnAt+boost.elapsed_seconds();marks.push_back("BOOSTER PERDIDO");
                std::printf("%7.2f s  booster: DESTRUÍDO, %s (%.0f m/s)\n",lostAt,why,boost.breakup().speed_meters_per_second());}
            else if(back.gaveUp()&&(back.phase()==BoosterReturn::Phase::down||boost.landed())){ // Down away from the tower: its flight ended.
                if(!out.boosterDown){out.boosterDown=true;out.boosterAway=offAxis(boost,back.up());const auto [east,north]=aside(boost);
                    std::printf("%7.2f s  booster: no chão a %.0f m da torre (%.0f m a leste, %.0f m ao norte), %.1f m/s, %.0f t de propelante\n",spawnAt+boost.elapsed_seconds(),
                        out.boosterAway,east,north,speedOf(boost),boost.propellant_kilograms()/1e3);
                    api::FlightRequest q;q.set_flight_id("booster");api::Applied a;call([&](grpc::ClientContext& c){return flight.EndFlight(&c,q,&a);});lostAt=spawnAt+boost.elapsed_seconds();}}
            else if(boost.landed()||boost.geodetic().height_meters()<towerGround)throw std::runtime_error("O booster atingiu o solo");
        }
        if(ship.elapsed_seconds()>=report){report=std::floor(ship.elapsed_seconds()/30)*30+30;
            std::printf("%7.2f s  nave: h %6.1f km  v %6.0f m/s  q %6.0f Pa  %7.0f t%s\n",ship.elapsed_seconds(),ship.geodetic().height_meters()/1e3,std::hypot(ship.velocity_meters_per_second().x(),ship.velocity_meters_per_second().y(),ship.velocity_meters_per_second().z()),
                ship.dynamic_pressure_pascals(),ship.mass_kilograms()/1e3,spawned?(std::string("; booster h ")+std::to_string(static_cast<int>(boost.geodetic().height_meters()))+" m").c_str():"");}
        if(orbit&&(out.boosterLost||out.boosterDown)){std::printf(out.boosterDown?"Booster no mar; a nave segue.\n":"Booster perdido; a nave segue.\n");if(orbitOnly)return out;out.ship=flyHome(flight,ship,pace,warp,faults);return out;}
        if(orbit&&back.phase()==BoosterReturn::Phase::caught&&t-caughtAt>=30){ // 30 s on the arms.
            out.maxQ=maxQ;out.maxQAt=maxQAt;out.caught=caughtAt;out.offAxis=offAxis(boost,back.up());out.speed=speedOf(boost);out.propellant=boost.propellant_kilograms();
            std::printf("Booster: a menor folga da torre, abaixo do topo dela, %.1f m.\n",out.towerClearance);
            std::printf("Max-Q %.0f Pa aos %.0f s. Booster capturado aos %.1f s; %.0f s depois, nos braços: %.2f m do eixo da torre, %.3f m/s, %.0f t de propelante restante.\n",maxQ,maxQAt,caughtAt,
                t-caughtAt,out.offAxis,out.speed,out.propellant/1e3);
            if(orbitOnly)return out;
            {api::FlightRequest q;q.set_flight_id("booster");api::Applied a;call([&](grpc::ClientContext& c){return flight.EndFlight(&c,q,&a);});} // Taken off the arms.
            out.ship=flyHome(flight,ship,pace,warp,faults);return out;}
        if(ship.elapsed_seconds()>1500)throw std::runtime_error("Missão não concluída em 25 minutos");
        pace(ship.elapsed_seconds(),1);
    }
}
// The stack on the mount at Starbase, not launched, the camera going once around it in `seconds` (distance: times the
// default, which takes in the tower).
inline void onPad(api::FlightService::Stub& flight,api::CameraService::Stub& camera,double seconds=60,double distance=.5){
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=stack();create.set_focus_camera(true);create.set_heading_degrees(heading);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);api::FlightSample s;call([&](grpc::ClientContext& c){return flight.CreateFlight(&c,create,&s);});
    std::printf("%s na plataforma de Starbase\n",s.vehicle().c_str());std::this_thread::sleep_for(std::chrono::seconds(4)); // The camera's arrival.
    {api::OrbitRequest o;o.set_distance_scale(distance);api::Applied a;call([&](grpc::ClientContext& c){return camera.Orbit(&c,o,&a);});}
    const auto start=std::chrono::steady_clock::now();double turned=0;
    for(double t=0;t<seconds;t=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()){
        api::OrbitRequest o;o.set_azimuth_radians(2*pi*t/seconds-turned);api::Applied a;call([&](grpc::ClientContext& c){return camera.Orbit(&c,o,&a);});turned=2*pi*t/seconds;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));}
}
// The ship alone, started in a 205 km circular orbit at the inclination a launch from Starbase toward `heading` gives, `ahead`
// degrees of orbit before the point over the tower: its way home without the day in orbit (its propellant: what the mission
// leaves it).
inline Home homecoming(api::FlightService::Stub& flight,bool fast,double warp=1000,double ahead=560,std::vector<Fault> faults={}){
    const double inc=std::acos(std::cos(towerLatitude*deg)*std::sin(heading*deg)),us=std::asin(std::min(std::sin(towerLatitude*deg)/std::sin(inc),1.)),u0=us-ahead*deg;
    const double r=6378137+205e3,speed=std::sqrt(earth.mu/r),period=2*pi*r/speed,turn=7.292115e-5*ahead/360*period;
    auto along=[&](double u){return std::atan2(std::cos(inc)*std::sin(u),std::cos(u));};double gone=along(us)-along(u0);while(gone<=0)gone+=2*pi;while(gone<ahead*deg-pi)gone+=2*pi;
    const double la=std::asin(std::sin(inc)*std::sin(u0)),lo=towerLongitude*deg+turn-gone,az=std::atan2(std::cos(inc),std::sin(inc)*std::cos(u0));
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=shipAlone();create.set_focus_camera(true);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);
    auto* w=create.mutable_start();w->set_stage(0);w->set_propellant_kilograms(110e3);auto* l=w->mutable_local();
    l->mutable_position()->set_latitude_degrees(la/deg);l->mutable_position()->set_longitude_degrees(std::remainder(lo,2*pi)/deg);l->mutable_position()->set_height_meters(205e3);
    put(l->mutable_velocity_ned_meters_per_second(),speed*std::cos(az),speed*std::sin(az)-7.292115e-5*r*std::cos(la),0);l->set_heading_degrees(az/deg);
    api::FlightSample ship;call([&](grpc::ClientContext& c){return flight.CreateFlight(&c,create,&ship);});
    std::printf("%s em órbita, %.0f° antes de passar sobre Starbase, %s\n",ship.vehicle().c_str(),ahead,fast?"o mais rápido possível":"tempo real");
    Pacer pace;pace.fast=fast;pace.last=ship.elapsed_seconds();return flyHome(flight,ship,pace,warp,std::move(faults));
}
}
