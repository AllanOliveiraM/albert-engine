#pragma once
// The flight computer's navigation: what the guidance reads in place of the flight's truth.
// - Strapdown in the Earth-fixed frame from the IMU's increments (gravity with J2, the Earth's turning; sub-steps of 1 s
//   over a long step).
// - Position and velocity corrected by the GNSS fixes (centimetres by RTK near the tower) through a Kalman filter on each
//   axis that refuses a fix out of line with the estimate (5 σ) while the inertial is surer than that: its uncertainty grows
//   meanwhile (the IMU's errors, and the attitude's under thrust), so a receiver right after all is taken again once the
//   inertial alone can no longer tell.
// - The attitude pulled toward the star tracker's when it sees the sky (refused beyond 2°), through the computer's own
//   Earth orientation (IAU 1976 precession, GMST; a few arcseconds off).
// - The propellant by the tanks' gauges; the air's dynamic pressure from the estimated speed and the standard atmosphere
//   (the wind unknown); the engines' state, thrust and the surfaces' deflections as they report them; the contact force as
//   the tower's load cells (or the ground) report it.
// FDIR, what it finds wrong set aside and reported:
// - the IMU units voted (each component's median; two left: the one nearer the last vote), a unit off the vote beyond its
//   errors for half a second, or without readings, isolated;
// - GNSS fixes out of line with the inertial refused while it is surer than they are (its uncertainty grows meanwhile);
// - star-tracker attitudes 0.2° off refused;
// - the gauges against the propellant the engines' and thrusters' flow leaves (each engine's from the thrust it reads; 4 σ of
//   the gauge's noise, at least 2 t, apart for 2 s: the model used instead);
// - an engine that says it is lit but reads no thrust for 0.2 s taken as failed.
#include "flight_api.hpp"
#include "starship.hpp"
#include <array>
#include <cstdint>
namespace albert::example::starship {
struct Quat {double w=1,x=0,y=0,z=0;};
inline Quat operator*(Quat a,Quat b){
    return{a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
inline Quat conj(Quat q){return{q.w,-q.x,-q.y,-q.z};}
inline Quat normalize(Quat q){const double n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);return{q.w/n,q.x/n,q.y/n,q.z/n};}
inline Quat turn(Vec a){const double t=norm(a);if(t<1e-12)return normalize({1,a.x/2,a.y/2,a.z/2});const double s=std::sin(t/2)/t;return{std::cos(t/2),a.x*s,a.y*s,a.z*s};} // By a rotation vector.
inline Vec angleOf(Quat q){const double s=q.w<0?-2.:2.;return{q.x*s,q.y*s,q.z*s};} // Its rotation vector (small).
inline Mat matrix(Quat q){return bodyToNed(q.w,q.x,q.y,q.z);} // Rows of the rotation (frame = M body).
inline Mat transposed(const Mat& m){return{{m.r0.x,m.r1.x,m.r2.x},{m.r0.y,m.r1.y,m.r2.y},{m.r0.z,m.r1.z,m.r2.z}};}
inline Mat times(const Mat& a,const Mat& b){auto row=[&](Vec r){return b.r0*r.x+b.r1*r.y+b.r2*r.z;};return{row(a.r0),row(a.r1),row(a.r2)};}
inline Quat quatOf(const Mat& m){ // Shepperd's method.
    const double t=m.r0.x+m.r1.y+m.r2.z;Quat q;
    if(t>0){const double s=2*std::sqrt(1+t);q={s/4,(m.r2.y-m.r1.z)/s,(m.r0.z-m.r2.x)/s,(m.r1.x-m.r0.y)/s};}
    else if(m.r0.x>m.r1.y&&m.r0.x>m.r2.z){const double s=2*std::sqrt(1+m.r0.x-m.r1.y-m.r2.z);q={(m.r2.y-m.r1.z)/s,s/4,(m.r0.y+m.r1.x)/s,(m.r0.z+m.r2.x)/s};}
    else if(m.r1.y>m.r2.z){const double s=2*std::sqrt(1+m.r1.y-m.r0.x-m.r2.z);q={(m.r0.z-m.r2.x)/s,(m.r0.y+m.r1.x)/s,s/4,(m.r1.z+m.r2.y)/s};}
    else{const double s=2*std::sqrt(1+m.r2.z-m.r0.x-m.r1.y);q={(m.r1.x-m.r0.y)/s,(m.r0.z+m.r2.x)/s,(m.r1.z+m.r2.y)/s,s/4};}
    return normalize(q);
}
constexpr double earthRate=7.292115e-5;
// GCRF to the Earth-fixed frame at a UTC instant (Unix s), rows: IAU 1976 precession and GMST (IAU 1982); no nutation, no
// polar motion, UT1 = UTC.
inline Mat earthFromInertial(double utc){
    const double t=(utc/86400+2440587.5-2451545)/36525,as=pi/180/3600;
    const double zeta=(2306.2181*t+.30188*t*t+.017998*t*t*t)*as,z=(2306.2181*t+1.09468*t*t+.018203*t*t*t)*as,th=(2004.3109*t-.42665*t*t-.041833*t*t*t)*as;
    const double gmst=std::fmod(67310.54841+(876600.*3600+8640184.812866)*t+.093104*t*t-6.2e-6*t*t*t,86400)/86400*2*pi;
    auto r3=[](double a){return Mat{{std::cos(a),std::sin(a),0},{-std::sin(a),std::cos(a),0},{0,0,1}};};
    auto r2=[](double a){return Mat{{std::cos(a),0,-std::sin(a)},{0,1,0},{std::sin(a),0,std::cos(a)}};};
    return times(r3(gmst),times(r3(-z),times(r2(th),r3(-zeta))));
}
inline Vec gravityFixed(Vec r){ // J2 gravitation and the centrifugal pull, in the rotating frame (m/s²).
    const double re=6378137,j2=1.08263e-3,rn=norm(r),zz=r.z*r.z/(rn*rn),f=1.5*j2*(re/rn)*(re/rn),k=-earth.mu/(rn*rn*rn);
    return Vec{r.x*k*(1+f*(1-5*zz)),r.y*k*(1+f*(1-5*zz)),r.z*k*(1+f*(3-5*zz))}+Vec{earthRate*earthRate*r.x,earthRate*earthRate*r.y,0};
}
inline double ellipsoidHeight(Vec r){ // WGS 84 (Bowring, two passes).
    const double a=6378137,e2=6.69437999014e-3,b=a*std::sqrt(1-e2),p=std::hypot(r.x,r.y);double lat=std::atan2(r.z,p*(1-e2));
    for(int i=0;i<2;++i){const double n=a/std::sqrt(1-e2*std::sin(lat)*std::sin(lat));lat=std::atan2(r.z+e2*n*std::sin(lat),p);}(void)b;
    const double n=a/std::sqrt(1-e2*std::sin(lat)*std::sin(lat));return std::abs(lat)<1.4?p/std::cos(lat)-n:r.z/std::sin(lat)-n*(1-e2);
}
class Navigator {
public:
    // From a surveyed start: the truth with an alignment's errors (seeded; a gyrocompass on the pad: 0.005° level, 0.03°
    // heading; the position 2 cm, the velocity 1 mm/s).
    void align(const api::FlightSample& s,std::uint64_t seed){
        seed_=seed?seed:1;position=vec(s.position_ecef_meters())+Vec{gauss()*.02,gauss()*.02,gauss()*.02};velocity=vec(s.velocity_ecef_meters_per_second())+Vec{gauss()*1e-3,gauss()*1e-3,gauss()*1e-3};
        const State x=state0(s);const Quat q=quatOf(transposed(x.toBody)),err=turn(Vec{gauss()*.005*deg,gauss()*.005*deg,gauss()*.03*deg});attitude=normalize(err*q);
        for(auto& a:axes_)a={.02*.02,0,1e-6};last_=s.elapsed_seconds();fix_=gnss(s).elapsed_seconds();star_=s.star_tracker().elapsed_seconds();propellant_=model_=s.propellant_kilograms();stage_=static_cast<int>(s.stage()); // As loaded.
    }
    // A vehicle born in flight (spawned) from its parent's computer: the parent's estimate moved by the separation's
    // geometry (offset: from the parent's centre of mass to its own, ECEF).
    void from(const Navigator& parent,const api::FlightSample& s,Vec offset){
        *this=parent;position=parent.position+offset;velocity=parent.velocity;last_=s.elapsed_seconds();fix_=gnss(s).elapsed_seconds();star_=s.star_tracker().elapsed_seconds();
        propellant_=model_=parent.model_;stage_=static_cast<int>(s.stage());residual_=0;lit_.clear(); // Its tanks: what the stack's model left in them.
        refused_=0;held_=0;gaugeOut_=false;anchor_=-1;quiet_.clear();silent_.clear();out_={};strikes_={};
    }
    void update(const api::FlightSample& s){health(s);propagate(s);correct(s);}
    std::vector<std::string> reports; // FDIR's findings since the mission last took them.
    // By stage, each engine's flow at full throttle and each thruster's at full duty (kg/s; a thruster on its own gas: 0): the design's.
    // load: each stage's propellant as loaded (the ground's measure of it), the model's start.
    // engineStart: what an ignition costs (kg); engineExhaust, engineArea: each one's exhaust velocity (m/s) and exit area (m²).
    // engineDirection: where each pushes at zero gimbal (body).
    std::vector<std::vector<double>> engineFlow,engineStart,engineExhaust,engineArea,thrusterFlow;std::vector<std::vector<Vec>> engineDirection;std::vector<double> load;
    void design(const api::VehicleDefinition& d){engineFlow.clear();engineStart.clear();engineExhaust.clear();engineArea.clear();engineDirection.clear();thrusterFlow.clear();load.clear();
        for(const auto& st:d.stages()){auto& e=engineFlow.emplace_back();auto& k=engineStart.emplace_back();auto& c=engineExhaust.emplace_back();auto& a=engineArea.emplace_back();auto& u=engineDirection.emplace_back();
            load.push_back(st.propellant_kilograms());
            for(const auto& x:st.engines()){e.push_back(x.vacuum_thrust_newtons()/x.exhaust_velocity_meters_per_second());k.push_back(x.start_kilograms());
                c.push_back(x.exhaust_velocity_meters_per_second());a.push_back(x.exit_area_meters2());u.push_back(x.has_direction()?unit(vec(x.direction())):Vec{1,0,0});}
            auto& t=thrusterFlow.emplace_back();for(const auto& x:st.thrusters())t.push_back(st.has_thruster_gas_kilograms()?0.:x.thrust_newtons()/x.exhaust_velocity_meters_per_second());}}
    // What the guidance reads.
    State state(const api::FlightSample& s)const{
        State x;x.position=position;x.velocity=velocity;x.toBody=transposed(matrix(attitude));x.rate=rate_;x.specific=specific_;
        x.contact=vec(s.contact_force_newtons());x.propellant=propellant_;x.mass=s.mass_kilograms()-s.propellant_kilograms()+propellant_; // The design's dry mass and the gauges.
        const auto st=static_cast<std::size_t>(s.stage());
        for(int i=0;i<s.engines_size();++i){const auto& e=s.engines(i);const auto k=static_cast<std::size_t>(i);x.thrust+=e.thrust_newtons();x.failed.push_back(e.failed()||(k<silent_.size()&&silent_[k]));
            // Its push: yawed about body z, then pitched about body y, as the physics turns an engine (its actuators' feedback).
            const Vec d=st<engineDirection.size()&&k<engineDirection[st].size()?engineDirection[st][k]:Vec{1,0,0};const double p=e.gimbal().pitch_radians(),y=e.gimbal().yaw_radians();
            const Vec w{d.x*std::cos(y)+d.y*std::sin(y),-d.x*std::sin(y)+d.y*std::cos(y),d.z};x.thrustBody=x.thrustBody+Vec{w.x*std::cos(p)-w.z*std::sin(p),w.y,w.x*std::sin(p)+w.z*std::cos(p)}*e.thrust_newtons();}
        x.navSigma=std::sqrt(std::max({axes_[0].pp,axes_[1].pp,axes_[2].pp}));
        for(const auto& f:s.surfaces())x.surfaces.push_back(f.deflection_radians());
        x.height=ellipsoidHeight(position);x.q=.5*density(x.height)*dot(velocity,velocity);return x;
    }
    // What the ascent program reads (its Reading).
    Reading reading(const api::FlightSample& s)const{
        const State x=state(s);Reading r;r.elapsed=s.elapsed_seconds();r.height=x.height;const Mat ned=nedOf(position),c=matrix(attitude);
        r.nedVelocity=mul(ned,velocity);const Quat qn=quatOf(times(ned,c));r.qw=qn.w;r.qx=qn.x;r.qy=qn.y;r.qz=qn.z;
        const Mat ie=transposed(earthFromInertial(s.utc_unix_seconds()));r.position=mul(ie,position);r.velocity=mul(ie,velocity+cross(Vec{0,0,earthRate},position));
        const Quat qi=quatOf(times(ie,c));r.iw=qi.w;r.ix=qi.x;r.iy=qi.y;r.iz=qi.z;r.fixedPosition=position;r.fixedVelocity=velocity;
        r.rate=x.rate;r.specificForce=x.specific;r.contact=s.leg_contact();r.stage=static_cast<int>(s.stage());r.stages=static_cast<int>(s.stages());r.burning=s.burning();r.clamped=s.clamped();
        r.propellant=x.propellant;r.mass=x.mass;return r;
    }
    int refused()const{return refused_;} // GNSS fixes refused so far (out of line with the estimate).
    int refusedStars()const{return refusedStars_;} // Star tracker attitudes refused (beyond 2°).
    Vec position,velocity,base;Quat attitude; // ECEF; body to ECEF; base: the RTK base station's place (its error grows with the range).
private:
    struct Axis {double pp=1,pv=0,vv=1;};
    static const api::GnssReading& gnss(const api::FlightSample& s){return s.gnss();}
    static State state0(const api::FlightSample& s){ // Only to align: the attitude as surveyed.
        const double la=s.geodetic().latitude_degrees()*deg,lo=s.geodetic().longitude_degrees()*deg;const Mat b=bodyToNed(s.attitude_ned().w(),s.attitude_ned().x(),s.attitude_ned().y(),s.attitude_ned().z());
        const Mat n{{-std::sin(la)*std::cos(lo),-std::sin(la)*std::sin(lo),std::cos(la)},{-std::sin(lo),std::cos(lo),0},{-std::cos(la)*std::cos(lo),-std::cos(la)*std::sin(lo),-std::sin(la)}};
        State x;x.toBody={n.r0*b.r0.x+n.r1*b.r1.x+n.r2*b.r2.x,n.r0*b.r0.y+n.r1*b.r1.y+n.r2*b.r2.y,n.r0*b.r0.z+n.r1*b.r1.z+n.r2*b.r2.z};return x;
    }
    double gauss(){ // xorshift64* and Box-Muller.
        auto u=[&]{seed_^=seed_>>12;seed_^=seed_<<25;seed_^=seed_>>27;return static_cast<double>((seed_*2685821657736338717ull)>>11)*0x1.0p-53+1e-300;};
        return std::sqrt(-2*std::log(u()))*std::cos(2*pi*u());
    }
    void propagate(const api::FlightSample& s){
        const double dt=s.elapsed_seconds()-last_;last_=s.elapsed_seconds();if(!(dt>0))return;
        Vec dv,da;vote(s,dt,dv,da);specific_=dv*(1/dt);rate_=da*(1/dt);const int n=std::max(1,static_cast<int>(std::ceil(dt)));const double h=dt/n;
        const Vec w{0,0,earthRate};auto acc=[&](Vec r,Vec v,Vec f){return f+gravityFixed(r)+cross(w,v)*-2.;}; // f: the specific force (ECEF).
        for(int i=0;i<n;++i){ // RK4, the specific force held over the sub-step at its mid-step attitude.
            const Quat mid=normalize(turn(Vec{0,0,-earthRate*h/2})*attitude*turn(da*(.5/n)));const Vec f=mul(matrix(mid),dv*(1./(n*h)));
            attitude=normalize(turn(Vec{0,0,-earthRate*h})*attitude*turn(da*(1./n)));
            const Vec r=position,v=velocity,a1=acc(r,v,f),v1=v+a1*(h/2),r1=r+v*(h/2),a2=acc(r1,v1,f),v2=v+a2*(h/2),r2=r+v1*(h/2),a3=acc(r2,v2,f),v3=v+a3*h,r3=r+v2*h,a4=acc(r3,v3,f);
            position=r+(v+v1*2+v2*2+v3)*(h/6);velocity=v+(a1+a2*2+a3*2+a4)*(h/6);}
        const double tilt=norm(specific_)*3.5e-4,qa=5e-3*5e-3+tilt*tilt; // Acceleration error (m²/s³): the IMU's, the gravity model's; the thrust off by the attitude's 0.02°.
        for(auto& a:axes_){a.pp+=2*a.pv*dt+a.vv*dt*dt+qa*dt*dt*dt/3;a.pv+=a.vv*dt+qa*dt*dt/2;a.vv+=qa*dt;}
    }
    void correct(const api::FlightSample& s){
        if(const auto& g=gnss(s);g.valid()&&g.elapsed_seconds()>fix_){fix_=g.elapsed_seconds();
            const double range=norm(position+base*-1.),sp=g.rtk()?.03+1e-6*range:3.5,sv=g.rtk()?.03:.08;const Vec zp=vec(g.position_ecef_meters()),zv=vec(g.velocity_ecef_meters_per_second());
            // RTK lost for a fix or two (its link drops one in fifty): those plain fixes, a metre or more off, skipped (the inertial
            // between RTK's is better); lost for 2 s, the plain fixes the reference; back on RTK after that, the RTK's (the plain
            // fix's slow error, ~1-3 m, not in the filter: its uncertainty widened to it either way).
            if(g.rtk())rtkAt_=g.elapsed_seconds();
            const bool lapse=!g.rtk()&&rtk_&&g.elapsed_seconds()-rtkAt_<2;
            if(!lapse&&g.rtk()!=rtk_){rtk_=g.rtk();for(auto& a:axes_)a.pp=std::max(a.pp,9.);}
            double r[3]{position.x,position.y,position.z},v[3]{velocity.x,velocity.y,velocity.z};const double p[3]{zp.x,zp.y,zp.z},u[3]{zv.x,zv.y,zv.z};
            bool out=false;for(int i=0;i<3;++i){const double y=p[i]-r[i];if(y*y>25*(axes_[i].pp+sp*sp))out=true;}
            if(lapse){} // Skipped.
            else if(out){++refused_;if(++held_==10)report("GNSS fora de linha com a inercial: fixos recusados");} // Refused while the inertial is surer.
            else{if(held_>=10)report("GNSS de volta em linha: aceito");held_=0;
                for(int i=0;i<3;++i){Axis& a=axes_[i];double y=p[i]-r[i],S=a.pp+sp*sp,kp=a.pp/S,kv=a.pv/S;r[i]+=kp*y;v[i]+=kv*y;a.vv-=kv*a.pv;a.pv-=kp*a.pv;a.pp-=kp*a.pp;
                    y=u[i]-v[i];S=a.vv+sv*sv;kp=a.pv/S;kv=a.vv/S;r[i]+=kp*y;v[i]+=kv*y;a.pp-=kp*a.pv;a.pv-=kp*a.vv;a.vv-=kv*a.vv;}
                position={r[0],r[1],r[2]};velocity={v[0],v[1],v[2]};}}
        if(const auto& t=s.star_tracker();t.valid()&&t.elapsed_seconds()>star_){star_=t.elapsed_seconds();
            const Quat m=quatOf(times(earthFromInertial(s.utc_unix_seconds()),matrix(Quat{t.attitude().w(),t.attitude().x(),t.attitude().y(),t.attitude().z()})));
            const Vec e=angleOf(m*conj(attitude));if(norm(e)<.2*deg){attitude=normalize(turn(e*.2)*attitude);starHeld_=0;}
            else if(++refusedStars_,++starHeld_==5)report("rastreador de estrelas fora de linha: atitudes recusadas");}
        if(const auto& g=s.propellant_gauge();anchor_<0&&g.valid()&&!gaugeOut_&&!engineFlow.empty()){ // The gauges against the flow the engines and thrusters report.
            // The residual, filtered over a second, against 4 σ of the gauge's noise (0.5% + 300 kg; at least 2 t) for 2 s.
            const double z=g.propellant_kilograms();residual_+=.1*(z-model_-residual_);
            if(std::abs(residual_)>std::max(4*(.005*z+300),2e3)){if(++gaugeHeld_>20){gaugeOut_=true;report("medidor de propelante diverge do consumo: substituído pelo modelo ("+std::to_string(static_cast<int>(z))+" lido, "+std::to_string(static_cast<int>(model_))+" modelo, "+std::to_string(static_cast<int>(s.propellant_kilograms()))+" real)");}}
            else{gaugeHeld_=0;propellant_+=.2*(z-propellant_);}}
        else if(anchor_<0&&!g.valid()&&!gaugeOut_&&s.has_propellant_gauge()){gaugeOut_=true;report("medidor de propelante sem leitura: substituído pelo modelo");}
        if(gaugeOut_)propellant_=model_;
    }
    void report(const std::string& what){reports.push_back(what);}
    // The engines' and thrusters' reported flow (the propellant model); an engine lit without thrust for 0.2 s; a new stage.
    void health(const api::FlightSample& s){
        const double dt=std::max(s.elapsed_seconds()-last_,0.);const auto st=static_cast<std::size_t>(s.stage());
        if(static_cast<int>(st)!=stage_){stage_=static_cast<int>(st);gaugeOut_=false;quiet_.clear();silent_.clear();lit_.clear();residual_=0; // A new stage: its load, or its gauge's next reading.
            if(st<load.size()){model_=propellant_=load[st];anchor_=-1;}else anchor_=s.elapsed_seconds();}
        if(anchor_>=0&&s.propellant_gauge().valid()&&s.propellant_gauge().elapsed_seconds()>anchor_){model_=propellant_=s.propellant_gauge().propellant_kilograms();anchor_=-1;}
        double flow=0;lit_.resize(static_cast<std::size_t>(s.engines_size()));const Air76 air=air76(ellipsoidHeight(position));const double p=air.density*287.053*air.temperature;
        if(st<engineFlow.size())for(int i=0;i<s.engines_size()&&static_cast<std::size_t>(i)<engineFlow[st].size();++i){const auto& e=s.engines(i);const auto k=static_cast<std::size_t>(i);
            // Its chamber's flow from the thrust it reads (starting, throttled or short alike) and the air's pressure on its exit.
            if(e.on()&&!e.failed()&&e.thrust_newtons()>0)flow+=(e.thrust_newtons()+p*engineArea[st][k])/engineExhaust[st][k];
            if(e.on()&&!lit_[k]&&st<engineStart.size())model_-=engineStart[st][k];lit_[k]=e.on();}
        if(st<thrusterFlow.size())for(int i=0;i<s.thruster_duty_size()&&static_cast<std::size_t>(i)<thrusterFlow[st].size();++i)flow+=s.thruster_duty(i)*thrusterFlow[st][static_cast<std::size_t>(i)];
        model_=std::max(0.,model_-flow*dt);
        quiet_.resize(static_cast<std::size_t>(s.engines_size()));silent_.resize(quiet_.size());
        for(std::size_t i=0;i<quiet_.size();++i){const auto& e=s.engines(static_cast<int>(i));
            if(e.on()&&!e.failed()&&e.thrust_newtons()<1e3){if(++quiet_[i]==10&&!silent_[i]){silent_[i]=true;report("motor "+std::to_string(i+1)+" diz estar aceso mas não dá empuxo: tratado como falho");}}else quiet_[i]=0;}
    }
    // The IMU units voted: each component's median of the valid ones not set aside (two: the one nearer the last vote; one:
    // it); a unit beyond its errors off the vote for half a second, or without readings, set aside for good.
    void vote(const api::FlightSample& s,double dt,Vec& dv,Vec& da){
        if(s.imus_size()==0){dv=vec(s.delta_velocity_meters_per_second());da=vec(s.delta_angle_radians());return;}
        std::vector<int> ok;for(int u=0;u<s.imus_size()&&u<3;++u)if(!out_[static_cast<std::size_t>(u)]){
            if(s.imus(u).valid())ok.push_back(u);else{out_[static_cast<std::size_t>(u)]=true;report("IMU "+std::to_string(u+1)+" sem leitura: isolada");}}
        auto get=[&](int u,bool angle){const auto& m=s.imus(u);return angle?vec(m.delta_angle_radians()):vec(m.delta_velocity_meters_per_second());};
        auto pick=[&](bool angle,Vec last)->Vec{
            if(ok.empty())return last*dt;if(ok.size()==1)return get(ok[0],angle);
            if(ok.size()==2){const Vec a=get(ok[0],angle),b=get(ok[1],angle);return norm(a+last*-dt)<=norm(b+last*-dt)?a:b;}
            Vec v[3]{get(ok[0],angle),get(ok[1],angle),get(ok[2],angle)};auto med=[](double a,double b,double c){return std::max(std::min(a,b),std::min(std::max(a,b),c));};
            return{med(v[0].x,v[1].x,v[2].x),med(v[0].y,v[1].y,v[2].y),med(v[0].z,v[1].z,v[2].z)};};
        dv=pick(false,specific_);da=pick(true,rate_);
        for(int u:ok){auto& k=strikes_[static_cast<std::size_t>(u)]; // Off the vote: accelerometers 0.02 m/s², gyroscopes 2e-5 rad/s (4°/h) past their noise.
            if(norm(get(u,false)+dv*-1.)>.02*dt+2e-4||norm(get(u,true)+da*-1.)>2e-5*dt+2e-6){if(++k>25){out_[static_cast<std::size_t>(u)]=true;report("IMU "+std::to_string(u+1)+" fora do voto: isolada");}}else k=0;}
    }
    std::array<Axis,3> axes_{};double last_=0,fix_=-1,star_=-1,rtkAt_=-1e9,propellant_=0,model_=0;double anchor_=-1,residual_=0;std::vector<bool> lit_;int refused_=0,held_=0,refusedStars_=0,starHeld_=0,gaugeHeld_=0,stage_=0;bool rtk_=false,gaugeOut_=false;
    std::uint64_t seed_=1;Vec rate_,specific_;std::array<bool,3> out_{};std::array<int,3> strikes_{};std::vector<int> quiet_;std::vector<bool> silent_;
};
}
