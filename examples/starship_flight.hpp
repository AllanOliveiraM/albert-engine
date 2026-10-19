#pragma once
// A Starship-class mission over the flight API (starship.hpp: the vehicles and the booster's guidance), both flights in
// lockstep: the stack and then the ship by the generic ascent program, the booster by BoosterReturn.
#include "flight_api.hpp"
#include "starship.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
namespace albert::example::starship {
// The flight from Starbase: the stack lifts off on 33 engines, cuts off with 450 t left and the booster flies on as its own
// flight ("booster", spawned) to a catch on the tower's arms while the ship goes on to orbit; the booster then rests on the arms
// for 30 s. Throws when either fails. onSpawn: called once the booster is a flight (scene id "flight.booster").
struct Mission {double meco=0,mecoHeight=0,seco=0,perigee=0,apoapsis=0,maxQ=0,maxQAt=0,caught=0,offAxis=0,speed=0,propellant=0;};
inline Mission mission(api::FlightService::Stub& flight,bool fast,const std::function<void()>& onSpawn={}){
    constexpr double lat=25.9969,lon=-97.1573,ground=5,arms=100,armRadius=.8;Mission out;
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=stack();create.set_focus_camera(true);create.set_heading_degrees(97);
    create.mutable_site()->set_latitude_degrees(lat);create.mutable_site()->set_longitude_degrees(lon);create.mutable_site()->set_height_meters(ground);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);
    // Each arm a rail of two steel bars 1.2 m apart, east-west under a pin, shock-absorbed (ζ ~0.8 under the booster): the pin
    // rests in the groove between them, √(0.8² - 0.6²) m over their axes.
    for(const double north:{-pinOut,pinOut})for(const double lip:{-.6,.6}){auto* a=create.add_structures();a->set_name("arm");a->mutable_start_enu_meters()->set_x(-10);
        a->mutable_start_enu_meters()->set_y(north+lip);a->mutable_start_enu_meters()->set_z(arms);a->mutable_end_enu_meters()->set_x(10);a->mutable_end_enu_meters()->set_y(north+lip);
        a->mutable_end_enu_meters()->set_z(arms);a->set_radius_meters(armRadius);a->set_stiffness_newtons_per_meter(1e8);a->set_damping_newton_seconds_per_meter(6e6);a->set_friction(.8);}
    api::FlightSample ship,boost;{grpc::ClientContext c;check(flight.CreateFlight(&c,create,&ship));}
    std::printf("%s de Starbase, %s\n",ship.vehicle().c_str(),fast?"o mais rápido possível":"tempo real");
    const double a=6378137,e2=6.69437999014e-3,phi=lat*pi/180,lam=lon*pi/180,n=a/std::sqrt(1-e2*std::sin(phi)*std::sin(phi)); // WGS 84: the tower's base.
    const Vec site{(n+ground)*std::cos(phi)*std::cos(lam),(n+ground)*std::cos(phi)*std::sin(lam),(n*(1-e2)+ground)*std::sin(phi)};
    BoosterReturn back(site,phi,lam,arms,std::sqrt(armRadius*armRadius-.36));GravityTurn ascent(97*pi/180,.07); // A small kick: lofted, so the booster's way back stays short.
    bool spawned=false,orbit=false,bucket=false,coasting=false;double report=0,maxQ=0,maxQAt=0,spawnAt=0,caughtAt=0;const auto wall=std::chrono::steady_clock::now();
    auto state=[](const api::FlightSample& s){State x;x.position=vec(s.position_ecef_meters());x.velocity=vec(s.velocity_ecef_meters_per_second());x.rate=vec(s.angular_rate_radians_per_second());
        x.contact=vec(s.contact_force_newtons());x.mass=s.mass_kilograms();x.propellant=s.propellant_kilograms();x.thrust=s.thrust_newtons();x.height=s.geodetic().height_meters();x.q=s.dynamic_pressure_pascals();
        const Mat b=bodyToNed(s.attitude_ned().w(),s.attitude_ned().x(),s.attitude_ned().y(),s.attitude_ned().z()),n=nedOf(x.position);
        x.toBody={n.r0*b.r0.x+n.r1*b.r1.x+n.r2*b.r2.x,n.r0*b.r0.y+n.r1*b.r1.y+n.r2*b.r2.y,n.r0*b.r0.z+n.r1*b.r1.z+n.r2*b.r2.z}; // ECEF to body: Bᵀ N.
        return x;};
    const char* names[]{"espera","giro","boostback","costa","queima de pouso","pairando","descendo","capturado"};auto last=back.phase();
    for(;;){
        const double t=ship.elapsed_seconds();
        api::StepRequest step;step.set_ticks(20);
        if(!spawned&&ship.stage()==0&&!ship.clamped()&&ship.propellant_kilograms()<=reserve){ // MECO and hot staging: the booster becomes its own flight.
            api::StepRequest stage;stage.add_commands()->set_cutoff(true);auto* sp=stage.add_commands()->mutable_spawn();sp->set_flight_id("booster");*sp->mutable_definition()=superHeavy();
            sp->set_nose_offset_meters(shipLength);{grpc::ClientContext c;check(flight.Step(&c,stage,&ship));}
            {grpc::ClientContext c;api::FlightRequest q;q.set_flight_id("booster");check(flight.GetFlight(&c,q,&boost));}
            std::printf("%7.2f s  MECO e separação a quente: h %.1f km, v %.0f m/s; booster com %.0f t\n",t,ship.geodetic().height_meters()/1e3,
                std::hypot(ship.velocity_ned_meters_per_second().x(),ship.velocity_ned_meters_per_second().y(),ship.velocity_ned_meters_per_second().z()),boost.propellant_kilograms()/1e3);
            spawned=true;spawnAt=t;if(onSpawn)onSpawn();out.meco=t;out.mecoHeight=ship.geodetic().height_meters();back.start(boost.elapsed_seconds());step.add_commands()->set_ignite(true);ascent.upper(reading(ship));
        }
        if(!spawned&&!bucket&&t>=40){step.add_commands()->set_throttle(.8);bucket=true;} // Through max-Q to cutoff at 80%.
        if(orbit&&!coasting){auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(0);q->set_y(0);q->set_z(0);step.add_commands()->mutable_gimbal();coasting=true;}
        if(!orbit){const Commands c=ascent.step(reading(ship));
            if(c.ignite&&!spawned)step.add_commands()->set_ignite(true);if(c.cutoff)step.add_commands()->set_cutoff(true);
            auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(c.torque.x);q->set_y(c.torque.y);q->set_z(c.torque.z);
            auto* g=step.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw); // The Raptors steer the stack.
            if(ascent.phase()==GravityTurn::Phase::orbit){orbit=true;out.seco=t;out.perigee=GravityTurn::perigee(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()));
                out.apoapsis=GravityTurn::apoapsis(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()));std::printf("%7.2f s  SECO: perigeu %.0f km, apogeu %.0f km\n",t,GravityTurn::perigee(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()))/1e3,
                GravityTurn::apoapsis(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()))/1e3);}}
        {grpc::ClientContext c;check(flight.Step(&c,step,&ship));}
        if(ship.dynamic_pressure_pascals()>maxQ){maxQ=ship.dynamic_pressure_pascals();maxQAt=ship.elapsed_seconds();}
        if(spawned){ // Caught, it stays a flight: resting on the arms.
            const Command c=back.step(boost.elapsed_seconds(),state(boost));api::StepRequest b;b.set_flight_id("booster");b.set_ticks(20); // The booster's own clock: 0 at its spawn.
            for(int i:c.ignite){auto* k=b.add_commands();k->set_ignite(true);k->set_index(static_cast<unsigned>(i));}
            for(int i:c.cutoff){auto* k=b.add_commands();k->set_cutoff(true);k->set_index(static_cast<unsigned>(i));}
            if(c.throttle>=0)b.add_commands()->set_throttle(c.throttle);
            auto* g=b.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw);
            for(int i=0;i<2;++i){auto* k=b.add_commands();k->set_surface_radians(i?c.finYaw:c.finPitch);k->set_index(static_cast<unsigned>(i+1));}
            if(c.torqueOn){auto* q=b.add_commands()->mutable_torque_newton_meters();q->set_x(c.torque.x);q->set_y(c.torque.y);q->set_z(c.torque.z);}
            {grpc::ClientContext ctx;check(flight.Step(&ctx,b,&boost));}
            if(back.phase()!=last){last=back.phase();if(last==BoosterReturn::Phase::caught)caughtAt=spawnAt+boost.elapsed_seconds();
                std::printf("%7.2f s  booster: %s (h %.2f km, v %.0f m/s, %.0f t de propelante, pouso previsto a %.0f m da torre)\n",spawnAt+boost.elapsed_seconds(),names[static_cast<int>(last)],
                boost.geodetic().height_meters()/1e3,std::hypot(boost.velocity_ned_meters_per_second().x(),boost.velocity_ned_meters_per_second().y(),boost.velocity_ned_meters_per_second().z()),boost.propellant_kilograms()/1e3,back.predicted());}
            if(boost.landed()||boost.geodetic().height_meters()<ground)throw std::runtime_error("O booster atingiu o solo");
        }
        if(ship.elapsed_seconds()>=report){report=std::floor(ship.elapsed_seconds()/30)*30+30;
            std::printf("%7.2f s  nave: h %6.1f km  v %6.0f m/s  q %6.0f Pa  %7.0f t%s\n",ship.elapsed_seconds(),ship.geodetic().height_meters()/1e3,std::hypot(ship.velocity_meters_per_second().x(),ship.velocity_meters_per_second().y(),ship.velocity_meters_per_second().z()),
                ship.dynamic_pressure_pascals(),ship.mass_kilograms()/1e3,spawned?(std::string("; booster h ")+std::to_string(static_cast<int>(boost.geodetic().height_meters()))+" m").c_str():"");}
        if(orbit&&back.phase()==BoosterReturn::Phase::caught&&t-caughtAt>=30){ // 30 s on the arms.
            const Vec rel=vec(boost.position_ecef_meters())+site*-1.,up=back.up();const auto& v=boost.velocity_ned_meters_per_second();
            out.maxQ=maxQ;out.maxQAt=maxQAt;out.caught=caughtAt;out.offAxis=norm(rel+up*-dot(rel,up));out.speed=std::hypot(v.x(),v.y(),v.z());out.propellant=boost.propellant_kilograms();
            std::printf("Max-Q %.0f Pa aos %.0f s. Booster capturado aos %.1f s; %.0f s depois, nos braços: %.2f m do eixo da torre, %.3f m/s, %.0f t de propelante restante.\n",maxQ,maxQAt,caughtAt,
                t-caughtAt,out.offAxis,out.speed,out.propellant/1e3);return out;}
        if(ship.elapsed_seconds()>1500)throw std::runtime_error("Missão não concluída em 25 minutos");
        if(!fast)std::this_thread::sleep_until(wall+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(ship.elapsed_seconds())));
    }
}
}
