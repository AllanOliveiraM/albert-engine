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
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
namespace albert::example::starship {
// The tower at Starbase: its base, the arms' height over it, the bars' radius; launches toward `heading` (degrees from north).
constexpr double towerLatitude=25.9969,towerLongitude=-97.1573,towerGround=5,armHeight=100,armRadius=.8,heading=93;
inline Vec towerSite(){ // WGS 84 (ECEF, m).
    const double a=6378137,e2=6.69437999014e-3,phi=towerLatitude*deg,lam=towerLongitude*deg,n=a/std::sqrt(1-e2*std::sin(phi)*std::sin(phi));
    return{(n+towerGround)*std::cos(phi)*std::cos(lam),(n+towerGround)*std::cos(phi)*std::sin(lam),(n*(1-e2)+towerGround)*std::sin(phi)};
}
inline double pinRest(){return std::sqrt(armRadius*armRadius-.36);} // The pins over the bars' axes in the groove (m).
// The arms hinge on the carriage at the tower's east face (east of the tower's centre at towerX, its half width half) and swing
// at 8°/s, open 30° apart from the closed catch (the north one to the north) while a vehicle goes up or comes down past them.
constexpr double towerX=-22,half=4.75,armPivot=towerX+half+1.5,armSwing=8*deg,armOpen=30*deg;
// Each arm a rail of two steel bars 1.2 m apart, east-west under a pin, shock-absorbed (ζ ~0.8 under the booster): the pin rests
// in the groove between them. Structures 1-2: the south arm's, 3-4: the north one's; they start open.
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
inline void addTower(api::CreateFlightRequest& create){
    create.mutable_site()->set_latitude_degrees(towerLatitude);create.mutable_site()->set_longitude_degrees(towerLongitude);create.mutable_site()->set_height_meters(towerGround);
    for(const double north:{-pinOut,pinOut})for(const double lip:{-.6,.6}){auto* a=create.add_structures();a->set_name("arm");a->mutable_start_enu_meters()->set_x(-10);
        a->mutable_start_enu_meters()->set_y(north+lip);a->mutable_start_enu_meters()->set_z(armHeight);a->mutable_end_enu_meters()->set_x(10);a->mutable_end_enu_meters()->set_y(north+lip);
        a->mutable_end_enu_meters()->set_z(armHeight);a->set_radius_meters(armRadius);a->set_stiffness_newtons_per_meter(1e8);a->set_damping_newton_seconds_per_meter(6e6);a->set_friction(.8);
        put(a->mutable_hinge_enu_meters(),armPivot,north>0?pinOut+.8:-pinOut-.8,armHeight);a->set_swing_rate_radians_per_second(armSwing);
        a->set_min_radians(north>0?0:-armOpen);a->set_max_radians(north>0?armOpen:0);a->set_start_radians(north>0?armOpen:-armOpen);}
    addStarbase(create);
}
// What the guidance reads from a sample: ECEF state, body rates and the attitude as ECEF to body (Bᵀ N, N the geodetic NED).
inline State state(const api::FlightSample& s){
    State x;x.position=vec(s.position_ecef_meters());x.velocity=vec(s.velocity_ecef_meters_per_second());x.rate=vec(s.angular_rate_radians_per_second());
    x.contact=vec(s.contact_force_newtons());x.mass=s.mass_kilograms();x.propellant=s.propellant_kilograms();x.thrust=s.thrust_newtons();x.height=s.geodetic().height_meters();x.q=s.dynamic_pressure_pascals();
    x.specific=vec(s.specific_force_meters_per_second2());for(const auto& f:s.surfaces())x.surfaces.push_back(f.deflection_radians());
    const double la=s.geodetic().latitude_degrees()*deg,lo=s.geodetic().longitude_degrees()*deg;
    const Mat b=bodyToNed(s.attitude_ned().w(),s.attitude_ned().x(),s.attitude_ned().y(),s.attitude_ned().z());
    const Mat n{{-std::sin(la)*std::cos(lo),-std::sin(la)*std::sin(lo),std::cos(la)},{-std::sin(lo),std::cos(lo),0},{-std::cos(la)*std::cos(lo),-std::cos(la)*std::sin(lo),-std::sin(la)}};
    x.toBody={n.r0*b.r0.x+n.r1*b.r1.x+n.r2*b.r2.x,n.r0*b.r0.y+n.r1*b.r1.y+n.r2*b.r2.y,n.r0*b.r0.z+n.r1*b.r1.z+n.r2*b.r2.z};
    return x;
}
// The catch arms open (a vehicle going up or coming down past them) or closed (catching it), by a step's commands.
inline void arms(api::StepRequest& q,bool open){
    for(unsigned i=1;i<=4;++i){auto* k=q.add_commands();k->set_index(i);k->set_structure_radians(open?(i<=2?-armOpen:armOpen):0.);}
}
inline bool armsClosed(const api::FlightSample& s){return std::all_of(s.structure_radians().begin(),s.structure_radians().end(),[](double a){return std::abs(a)<.2*deg;});}
inline void apply(api::StepRequest& q,const Command& c){ // A guidance command as flight commands (indices from 1).
    for(int i:c.ignite){auto* k=q.add_commands();k->set_ignite(true);k->set_index(static_cast<unsigned>(i));}
    for(int i:c.cutoff){auto* k=q.add_commands();k->set_cutoff(true);k->set_index(static_cast<unsigned>(i));}
    if(c.throttle>=0)q.add_commands()->set_throttle(c.throttle);
    auto* g=q.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw);
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
inline Home flyHome(api::FlightService::Stub& flight,api::FlightSample& ship,Pacer& pace,double warp){
    ShipReturn back(towerSite(),towerLatitude*deg,towerLongitude*deg,armHeight,pinRest());Home out;double report=0,caughtAt=-1;
    const char* names[]{"em órbita","orientação retrógrada","queima de saída de órbita","planeio até o ar","reentrada","giro","queima de pouso","pairando","descendo","capturada"};auto last=back.phase();
    bool open=false; // The arms: opened for the way down past them, closed over the hover.
    for(;;){
        const double t=ship.elapsed_seconds();back.armsClosed=armsClosed(ship);const Command c=back.step(t,state(ship));
        api::StepRequest q;q.set_ticks(static_cast<unsigned>(std::clamp(std::llround(back.nextStep()*1000),1LL,600000LL)));apply(q,c);
        if(!open&&back.phase()!=ShipReturn::Phase::hover&&back.phase()!=ShipReturn::Phase::descend&&back.phase()!=ShipReturn::Phase::caught){arms(q,true);open=true;}
        if(open&&back.phase()==ShipReturn::Phase::hover){arms(q,false);open=false;}
        {grpc::ClientContext ctx;check(flight.Step(&ctx,q,&ship));}
        for(const auto& e:ship.events())if(e.kind()==api::FlightEvent::OVERHEAT&&e.index()<static_cast<unsigned>(ship.thermal_nodes_size())){++out.overheats;
            std::printf("%9.1f s  nave: %s acima do limite\n",ship.elapsed_seconds(),ship.thermal_nodes(static_cast<int>(e.index())).name().c_str());}
        for(const auto& n:ship.thermal_nodes())out.maxTile=std::max(out.maxTile,n.temperature_kelvin());
        out.towerClearance=std::min(out.towerClearance,clearance(ship,shipLength));
        if(back.phase()!=last){last=back.phase();const double now=ship.elapsed_seconds();
            if(last==ShipReturn::Phase::deorbit)out.deorbit=now;else if(last==ShipReturn::Phase::entry)out.entry=now;else if(last==ShipReturn::Phase::caught)caughtAt=out.caught=now;
            std::printf("%9.1f s  nave: %s (h %.2f km, v %.0f m/s, %.0f t de propelante, pouso previsto %.1f km além e %.1f km à direita da torre)\n",now,names[static_cast<int>(last)],
                ship.geodetic().height_meters()/1e3,speedOf(ship),ship.propellant_kilograms()/1e3,back.along()/1e3,back.beside()/1e3);}
        const double now=ship.elapsed_seconds();
        if(now>=report){const bool orbiting=last==ShipReturn::Phase::orbit;report=orbiting?std::floor(now/3600)*3600+3600:std::floor(now/30)*30+30;
            std::printf("%9.1f s  nave: h %7.1f km  v %5.0f m/s  q %6.0f Pa  inclinação lateral %4.0f°  pouso previsto %.1f km além, %.1f à direita  ladrilho mais quente %4.0f K\n",now,
                ship.geodetic().height_meters()/1e3,speedOf(ship),ship.dynamic_pressure_pascals(),back.bank()/deg,back.along()/1e3,back.beside()/1e3,out.maxTile);}
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
// - the stack lifts off on 33 engines and cuts off with 450 t left in the booster;
// - the booster flies on as its own flight ("booster", spawned) to a catch on the tower's arms, rests there 30 s and is
//   taken off (its flight ended);
// - the ship goes on to orbit and, unless orbitOnly, home to the same arms (flyHome).
// Throws when any part fails. onSpawn: called once the booster is a flight (scene id "flight.booster").
struct Mission {double meco=0,mecoHeight=0,seco=0,perigee=0,apoapsis=0,maxQ=0,maxQAt=0,caught=0,offAxis=0,speed=0,propellant=0,towerClearance=1e9;Home ship;};
inline Mission mission(api::FlightService::Stub& flight,bool fast,double warp=1000,bool orbitOnly=false,const std::function<void()>& onSpawn={}){
    Mission out;Pacer pace;pace.fast=fast;
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=stack();create.set_focus_camera(true);create.set_heading_degrees(heading);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);
    api::FlightSample ship,boost;{grpc::ClientContext c;check(flight.CreateFlight(&c,create,&ship));}
    std::printf("%s de Starbase, %s\n",ship.vehicle().c_str(),fast?"o mais rápido possível":"tempo real");
    BoosterReturn back(towerSite(),towerLatitude*deg,towerLongitude*deg,armHeight,pinRest());GravityTurn ascent(heading*deg,.07); // A small kick: lofted, so the booster's way back stays short.
    bool spawned=false,orbit=false,bucket=false,coasting=false,closing=false,close=false;double report=0,maxQ=0,maxQAt=0,spawnAt=0,caughtAt=0;
    const char* names[]{"espera","giro","boostback","costa","queima de pouso","pairando","descendo","capturado"};auto last=back.phase();
    for(;;){
        const double t=ship.elapsed_seconds();
        api::StepRequest step;step.set_ticks(20);
        if(!spawned&&ship.stage()==0&&!ship.clamped()&&ship.propellant_kilograms()<=reserve){ // MECO and hot staging: the booster becomes its own flight.
            api::StepRequest stage;stage.add_commands()->set_cutoff(true);auto* sp=stage.add_commands()->mutable_spawn();sp->set_flight_id("booster");*sp->mutable_definition()=superHeavy();
            sp->set_nose_offset_meters(shipLength);{grpc::ClientContext c;check(flight.Step(&c,stage,&ship));}
            {grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("booster");check(flight.GetFlight(&c,q,&boost));}
            std::printf("%7.2f s  MECO e separação a quente: h %.1f km, v %.0f m/s; booster com %.0f t\n",t,ship.geodetic().height_meters()/1e3,speedOf(ship),boost.propellant_kilograms()/1e3);
            spawned=true;spawnAt=t;if(onSpawn)onSpawn();out.meco=t;out.mecoHeight=ship.geodetic().height_meters();back.start(boost.elapsed_seconds());step.add_commands()->set_ignite(true);ascent.upper(reading(ship));
        }
        if(!spawned&&!bucket&&t>=40){step.add_commands()->set_throttle(.8);bucket=true;} // Through max-Q to cutoff at 80%.
        if(close){arms(step,false);close=false;}
        if(orbit&&!coasting){auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(0);q->set_y(0);q->set_z(0);step.add_commands()->mutable_gimbal();coasting=true;}
        if(!orbit){const Commands c=ascent.step(reading(ship));
            if(c.ignite&&!spawned)step.add_commands()->set_ignite(true);if(c.cutoff)step.add_commands()->set_cutoff(true);
            auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(c.torque.x);q->set_y(c.torque.y);q->set_z(c.torque.z);
            auto* g=step.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw); // The Raptors steer the stack.
            if(ascent.phase()==GravityTurn::Phase::orbit){orbit=true;out.seco=t;out.perigee=GravityTurn::perigee(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()));
                out.apoapsis=GravityTurn::apoapsis(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()));std::printf("%7.2f s  SECO: perigeu %.0f km, apogeu %.0f km, girando a %.2f°/s\n",t,out.perigee/1e3,out.apoapsis/1e3,norm(vec(ship.angular_rate_radians_per_second()))/deg);}}
        {grpc::ClientContext c;check(flight.Step(&c,step,&ship));}
        if(ship.dynamic_pressure_pascals()>maxQ){maxQ=ship.dynamic_pressure_pascals();maxQAt=ship.elapsed_seconds();}
        if(spawned){ // Caught, it stays a flight: resting on the arms.
            back.armsClosed=armsClosed(boost);const Command c=back.step(boost.elapsed_seconds(),state(boost));api::StepRequest b;b.set_flight_id("booster");b.set_ticks(20);apply(b,c); // The booster's own clock: 0 at its spawn.
            if(!closing&&back.phase()==BoosterReturn::Phase::hover){arms(b,false);closing=true;close=true;} // Around it as it hovers; the site shows the ship's flight's arms: those too.
            {grpc::ClientContext ctx;check(flight.Step(&ctx,b,&boost));}
            if(back.phase()!=last){last=back.phase();if(last==BoosterReturn::Phase::caught)caughtAt=spawnAt+boost.elapsed_seconds();
                const auto [east,north]=aside(boost);
                std::printf("%7.2f s  booster: %s (h %.2f km, v %.0f m/s, %.0f t de propelante, %.0f m a leste e %.0f m ao norte da torre, pouso previsto a %.0f m do ponto sobre o mar)\n",
                    spawnAt+boost.elapsed_seconds(),names[static_cast<int>(last)],boost.geodetic().height_meters()/1e3,speedOf(boost),boost.propellant_kilograms()/1e3,east,north,back.predicted());}
            out.towerClearance=std::min(out.towerClearance,clearance(boost,boosterLength));
            if(boost.landed()||boost.geodetic().height_meters()<towerGround)throw std::runtime_error("O booster atingiu o solo");
        }
        if(ship.elapsed_seconds()>=report){report=std::floor(ship.elapsed_seconds()/30)*30+30;
            std::printf("%7.2f s  nave: h %6.1f km  v %6.0f m/s  q %6.0f Pa  %7.0f t%s\n",ship.elapsed_seconds(),ship.geodetic().height_meters()/1e3,std::hypot(ship.velocity_meters_per_second().x(),ship.velocity_meters_per_second().y(),ship.velocity_meters_per_second().z()),
                ship.dynamic_pressure_pascals(),ship.mass_kilograms()/1e3,spawned?(std::string("; booster h ")+std::to_string(static_cast<int>(boost.geodetic().height_meters()))+" m").c_str():"");}
        if(orbit&&back.phase()==BoosterReturn::Phase::caught&&t-caughtAt>=30){ // 30 s on the arms.
            out.maxQ=maxQ;out.maxQAt=maxQAt;out.caught=caughtAt;out.offAxis=offAxis(boost,back.up());out.speed=speedOf(boost);out.propellant=boost.propellant_kilograms();
            std::printf("Booster: a menor folga da torre, abaixo do topo dela, %.1f m.\n",out.towerClearance);
            std::printf("Max-Q %.0f Pa aos %.0f s. Booster capturado aos %.1f s; %.0f s depois, nos braços: %.2f m do eixo da torre, %.3f m/s, %.0f t de propelante restante.\n",maxQ,maxQAt,caughtAt,
                t-caughtAt,out.offAxis,out.speed,out.propellant/1e3);
            if(orbitOnly)return out;
            {grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("booster");api::Applied a;check(flight.EndFlight(&c,q,&a));} // Taken off the arms.
            out.ship=flyHome(flight,ship,pace,warp);return out;}
        if(ship.elapsed_seconds()>1500)throw std::runtime_error("Missão não concluída em 25 minutos");
        pace(ship.elapsed_seconds(),1);
    }
}
// The stack on the mount at Starbase, not launched, the camera going once around it in `seconds` (distance: times the
// default, which takes in the tower).
inline void onPad(api::FlightService::Stub& flight,api::CameraService::Stub& camera,double seconds=60,double distance=.5){
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=stack();create.set_focus_camera(true);create.set_heading_degrees(heading);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);api::FlightSample s;{grpc::ClientContext c;check(flight.CreateFlight(&c,create,&s));}
    std::printf("%s na plataforma de Starbase\n",s.vehicle().c_str());std::this_thread::sleep_for(std::chrono::seconds(4)); // The camera's arrival.
    {grpc::ClientContext c;api::OrbitRequest o;o.set_distance_scale(distance);api::Applied a;check(camera.Orbit(&c,o,&a));}
    const auto start=std::chrono::steady_clock::now();double turned=0;
    for(double t=0;t<seconds;t=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()){
        grpc::ClientContext c;api::OrbitRequest o;o.set_azimuth_radians(2*pi*t/seconds-turned);api::Applied a;check(camera.Orbit(&c,o,&a));turned=2*pi*t/seconds;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));}
}
// The ship alone, started in a 205 km circular orbit at the inclination a launch from Starbase toward `heading` gives, `ahead`
// degrees of orbit before the point over the tower: its way home without the day in orbit (its propellant: what the mission
// leaves it).
inline Home homecoming(api::FlightService::Stub& flight,bool fast,double warp=1000,double ahead=560){
    const double inc=std::acos(std::cos(towerLatitude*deg)*std::sin(heading*deg)),us=std::asin(std::min(std::sin(towerLatitude*deg)/std::sin(inc),1.)),u0=us-ahead*deg;
    const double r=6378137+205e3,speed=std::sqrt(earth.mu/r),period=2*pi*r/speed,turn=7.292115e-5*ahead/360*period;
    auto along=[&](double u){return std::atan2(std::cos(inc)*std::sin(u),std::cos(u));};double gone=along(us)-along(u0);while(gone<=0)gone+=2*pi;while(gone<ahead*deg-pi)gone+=2*pi;
    const double la=std::asin(std::sin(inc)*std::sin(u0)),lo=towerLongitude*deg+turn-gone,az=std::atan2(std::cos(inc),std::sin(inc)*std::cos(u0));
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=shipAlone();create.set_focus_camera(true);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);
    auto* w=create.mutable_start();w->set_stage(0);w->set_propellant_kilograms(110e3);auto* l=w->mutable_local();
    l->mutable_position()->set_latitude_degrees(la/deg);l->mutable_position()->set_longitude_degrees(std::remainder(lo,2*pi)/deg);l->mutable_position()->set_height_meters(205e3);
    put(l->mutable_velocity_ned_meters_per_second(),speed*std::cos(az),speed*std::sin(az)-7.292115e-5*r*std::cos(la),0);l->set_heading_degrees(az/deg);
    api::FlightSample ship;{grpc::ClientContext c;check(flight.CreateFlight(&c,create,&ship));}
    std::printf("%s em órbita, %.0f° antes de passar sobre Starbase, %s\n",ship.vehicle().c_str(),ahead,fast?"o mais rápido possível":"tempo real");
    Pacer pace;pace.fast=fast;pace.last=ship.elapsed_seconds();return flyHome(flight,ship,pace,warp);
}
}
