#include "physics/flight.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace albert::physics {
namespace {
constexpr double degrees=180/3.14159265358979323846;
struct Mass {double m,cg;V3 inertia;};
Mass mass(const Stage& st,double propellant){
    const double f=st.propellant>0?std::clamp(propellant/st.propellant,0.,1.):0.;
    return{st.massEmpty+(st.massFull-st.massEmpty)*f,st.cgEmpty+(st.cgFull-st.cgEmpty)*f,st.inertiaEmpty+(st.inertiaFull-st.inertiaEmpty)*f};
}
Q spin(Q q,V3 w){return{.5*(-q.x*w.x-q.y*w.y-q.z*w.z),.5*(q.w*w.x+q.y*w.z-q.z*w.y),.5*(q.w*w.y-q.x*w.z+q.z*w.x),.5*(q.w*w.z+q.x*w.y-q.y*w.x)};}
M3 bodyToNed(V3 e){ // Yaw, pitch, roll (3-2-1).
    const double cy=std::cos(e.x),sy=std::sin(e.x),cp=std::cos(e.y),sp=std::sin(e.y),cr=std::cos(e.z),sr=std::sin(e.z);
    return{{{cy*cp,cy*sp*sr-sy*cr,cy*sp*cr+sy*sr},{sy*cp,sy*sp*sr+cy*cr,sy*sp*cr-cy*sr},{-sp,cp*sr,cp*cr}}};
}
Tt floorDiv(Tt t,Tt d){return t>=0?t/d:(t-d+1)/d;}
}
double Table::operator()(double m,double a)const{
    auto locate=[](const std::vector<double>& b,double x,std::size_t& i,double& t){
        if(b.size()<2){i=0;t=0;return;}
        x=std::clamp(x,b.front(),b.back());
        i=std::min<std::size_t>(std::upper_bound(b.begin(),b.end(),x)-b.begin(),b.size()-1)-1;t=(x-b[i])/(b[i+1]-b[i]);
    };
    std::size_t i,j;double u,v;locate(mach,m,i,u);locate(angle,a,j,v);
    const std::size_t n=angle.size(),i1=mach.size()<2?i:i+1,j1=n<2?j:j+1;
    return(1-u)*((1-v)*values[i*n+j]+v*values[i*n+j1])+u*((1-v)*values[i1*n+j]+v*values[i1*n+j1]);
}
Flight::Flight(Vehicle vehicle,Environment environment,State state):vehicle_(std::move(vehicle)),env_(std::move(environment)),s_(state){
    if(vehicle_.stages.empty())throw std::invalid_argument("Vehicle without stages");
    for(const auto& st:vehicle_.stages){
        if(!(st.massEmpty>0)||!(st.propellant>=0)||std::abs(st.massFull-st.massEmpty-st.propellant)>1e-6*st.massFull)throw std::invalid_argument("Inconsistent stage mass: "+st.name);
        for(V3 i:{st.inertiaFull,st.inertiaEmpty})if(!(i.x>0&&i.y>0&&i.z>0))throw std::invalid_argument("Non-positive inertia: "+st.name);
        if(st.thrust>0&&!(st.exhaust>0))throw std::invalid_argument("Engine without exhaust velocity: "+st.name);
        for(const Table* t:{&st.aero.lift,&st.aero.drag,&st.aero.side,&st.aero.roll,&st.aero.pitch,&st.aero.yaw})
            if(t->mach.empty()||t->angle.empty()||t->values.size()!=t->mach.size()*t->angle.size())throw std::invalid_argument("Malformed coefficient table: "+st.name);
    }
    if(s_.stage<0||s_.stage>=static_cast<int>(vehicle_.stages.size()))throw std::invalid_argument("Invalid stage");
    const Orientation o=env_.rotation.at(s_.time);
    padPosition_=o.fixed*s_.position;padAttitude_=quaternion(o.fixed*matrix(s_.attitude));
}
State Flight::pad(const Environment& e,const Vehicle& v,Tt t,Geodetic site,double heading,double pitch,double roll){
    const Stage& st=v.stages.at(0);site.height+=(st.length-st.cgFull)*std::sin(pitch); // Base on the ground.
    State s=free(e,v,t,site,{},{heading,pitch,roll});s.clamped=true;return s;
}
State Flight::free(const Environment& e,const Vehicle& v,Tt t,Geodetic g,V3 ned,V3 euler,V3 relative){
    State s;s.time=t;s.propellant=v.stages.at(0).propellant;
    const Orientation o=e.rotation.at(t);const M3 back=transpose(o.fixed),fromNed=transpose(nedFromFixed(g.latitude,g.longitude));
    s.position=back*fixedPosition(e.planet,g);s.velocity=back*(fromNed*ned)+cross(o.omega,s.position);
    const M3 b=back*fromNed*bodyToNed(euler);s.attitude=quaternion(b);s.rate=transpose(b)*o.omega+relative;
    return s;
}
double Flight::massFlow()const{
    const Stage& st=vehicle_.stages[s_.stage];
    return s_.burning&&s_.propellant>0&&st.thrust>0?s_.throttle*st.thrust/st.exhaust:0.;
}
V3 Flight::tidal(const State& s)const{
    constexpr Tt bucket=60*second;const Tt k=floorDiv(s.time,bucket);
    if(k!=perturbed_){p0_=perturbers(k*bucket);p1_=perturbers((k+1)*bucket);perturbed_=k;}
    const double w=secondsSinceJ2000(s.time-k*bucket)/60.;
    return physics::tidal(s.position,p0_.sun+(p1_.sun-p0_.sun)*w,sunGM)+physics::tidal(s.position,p0_.moon+(p1_.moon-p0_.moon)*w,moonGM);
}
Flight::Rate Flight::rate(const State& s,Sample* out)const{
    const Stage& st=vehicle_.stages[s.stage];const Mass ms=mass(st,s.propellant);
    const Orientation o=env_.rotation.at(s.time);const V3 rf=o.fixed*s.position;const M3 back=transpose(o.fixed);
    V3 g=back*gravitation(env_.planet,rf);if(env_.sunMoon)g+=tidal(s);
    const M3 b=matrix(s.attitude),bt=transpose(b);
    const Geodetic geo=geodetic(env_.planet,rf);const M3 ned=nedFromFixed(geo.latitude,geo.longitude);
    const Air atm=!env_.atmosphere?Air{}:env_.air?env_.air->at(s.time,geo.latitude,geo.longitude,geo.height):standardAtmosphere(geo.height);
    const V3 relative=s.velocity-cross(o.omega,s.position),air=bt*(relative-back*(transpose(ned)*atm.wind));const double speed=norm(air);
    const double q=.5*atm.density*speed*speed,mach=atm.sound>0?speed/atm.sound:0.;
    const double alpha=speed>0?std::atan2(air.z,air.x):0.,beta=speed>0?std::asin(std::clamp(air.y/speed,-1.,1.)):0.;
    V3 fa,ma;
    if(q>0){
        const Aerodynamics& ae=st.aero;const double ad=alpha*degrees,bd=beta*degrees,k=q*ae.area;
        const double cl=ae.lift(mach,ad),cd=ae.drag(mach,std::hypot(ad,bd)),ca=std::cos(alpha),sa=std::sin(alpha);
        fa=k*(cl*V3{sa,0,-ca}+V3{0,ae.side(mach,bd),0})-k*cd/speed*air; // Drag along the air velocity.

        const V3 w=s.rate-bt*o.omega;const double hl=ae.length/(2*speed),hs=ae.span/(2*speed);
        ma={k*ae.span*(ae.roll(mach,ad)+ae.rollDamping*w.x*hs),k*ae.length*(ae.pitch(mach,ad)+ae.pitchDamping*w.y*hl),k*ae.span*(ae.yaw(mach,bd)+ae.yawDamping*w.z*hs)};
        ma+=cross(V3{ms.cg-st.reference,0,0},fa);
    }
    V3 ft,mt;double thrust=0,flow=0;
    if(s.burning&&st.thrust>0){ // Steps end at depletion, so the engine state holds through all RK4 stages.
        thrust=std::max(0.,s.throttle*st.thrust-atm.pressure*st.exitArea);flow=s.throttle*st.thrust/st.exhaust;
        const double cp=std::cos(s.gimbalPitch),sp=std::sin(s.gimbalPitch),cy=std::cos(s.gimbalYaw),sy=std::sin(s.gimbalYaw);
        ft=thrust*V3{cp*cy,-sy,sp*cy};mt=cross(V3{ms.cg-st.nozzle,0,0},ft);
    }
    const double lim=vehicle_.torque;
    const V3 force=fa+ft,moment=ma+mt+V3{std::clamp(s.torque.x,-lim,lim),std::clamp(s.torque.y,-lim,lim),std::clamp(s.torque.z,-lim,lim)};
    // d(Iω)/dt with the propellant's inertia loss; the exhaust carries transverse angular
    // momentum ṁ l² ω through the nozzle (jet damping).
    const V3 iw{ms.inertia.x*s.rate.x,ms.inertia.y*s.rate.y,ms.inertia.z*s.rate.z};V3 rhs=moment-cross(s.rate,iw);
    if(flow>0){const V3 di=(st.inertiaFull-st.inertiaEmpty)*(vehicle_.inertiaRate*flow/st.propellant);rhs+=V3{di.x*s.rate.x,di.y*s.rate.y,di.z*s.rate.z};
        if(vehicle_.jetDamping){const double l=ms.cg-st.nozzle;rhs+=-flow*l*l*V3{0,s.rate.y,s.rate.z};}}
    Rate r{s.velocity,g+b*(force/ms.m),spin(s.attitude,s.rate),{rhs.x/ms.inertia.x,rhs.y/ms.inertia.y,rhs.z/ms.inertia.z},-flow};
    if(out){
        Sample& x=*out;x.mass=ms.m;x.mach=mach;x.dynamicPressure=q;x.alpha=alpha;x.beta=beta;x.thrust=thrust;x.massFlow=flow;x.air=atm;x.geodetic=geo;
        x.fixedPosition=rf;x.fixedVelocity=o.fixed*relative;x.nedVelocity=ned*x.fixedVelocity;
        const M3 c=ned*o.fixed*b;x.nedAttitude=quaternion(c);x.euler={std::atan2(c.m[1][0],c.m[0][0]),-std::asin(std::clamp(c.m[2][0],-1.,1.)),std::atan2(c.m[2][1],c.m[2][2])};
        x.aeroForce=fa;x.aeroMoment=ma;x.thrustForce=ft;x.gravity=g;x.acceleration=r.a;x.specificForce=force/ms.m;
        if(s.clamped||s.landed){x.acceleration=cross(o.omega,cross(o.omega,s.position));x.specificForce=bt*(x.acceleration-g);}
    }
    return r;
}
Sample Flight::sample()const{Sample x;rate(s_,&x);return x;}
void Flight::hold(State& s)const{
    const Orientation o=env_.rotation.at(s.time);const M3 back=transpose(o.fixed);
    s.position=back*padPosition_;s.velocity=cross(o.omega,s.position);
    const M3 b=back*matrix(padAttitude_);s.attitude=quaternion(b);s.rate=transpose(b)*o.omega;
}
void Flight::integrate(Tt h){
    const double dt=static_cast<double>(h)*1e-9;
    if(s_.clamped||s_.landed){s_.propellant=std::max(0.,s_.propellant-massFlow()*dt);s_.time+=h;hold(s_);return;}
    auto at=[](const State& s,const Rate& k,double d,Tt t){State r=s;r.time=t;r.position+=k.v*d;r.velocity+=k.a*d;
        r.attitude={s.attitude.w+k.q.w*d,s.attitude.x+k.q.x*d,s.attitude.y+k.q.y*d,s.attitude.z+k.q.z*d};r.rate+=k.w*d;r.propellant+=k.m*d;return r;};
    const Tt t0=s_.time,mid=t0+h/2;
    const Rate k1=rate(s_,nullptr),k2=rate(at(s_,k1,dt/2,mid),nullptr),k3=rate(at(s_,k2,dt/2,mid),nullptr),k4=rate(at(s_,k3,dt,t0+h),nullptr);
    const double w=dt/6;
    s_.position+=(k1.v+2*k2.v+2*k3.v+k4.v)*w;s_.velocity+=(k1.a+2*k2.a+2*k3.a+k4.a)*w;s_.rate+=(k1.w+2*k2.w+2*k3.w+k4.w)*w;
    s_.attitude=normalized({s_.attitude.w+(k1.q.w+2*k2.q.w+2*k3.q.w+k4.q.w)*w,s_.attitude.x+(k1.q.x+2*k2.q.x+2*k3.q.x+k4.q.x)*w,
        s_.attitude.y+(k1.q.y+2*k2.q.y+2*k3.q.y+k4.q.y)*w,s_.attitude.z+(k1.q.z+2*k2.q.z+2*k3.q.z+k4.q.z)*w});
    s_.propellant=std::max(0.,s_.propellant+(k1.m+2*k2.m+2*k3.m+k4.m)*w);s_.time=t0+h;
}
Tt Flight::advance(Tt dt){
    if(dt<=0)throw std::invalid_argument("Non-positive step");
    for(Tt left=dt;left>0;){
        if(s_.clamped&&s_.burning){
            const Sample x=sample();const M3 b=matrix(s_.attitude),back=transpose(env_.rotation.at(s_.time).fixed);
            const V3 up=back*(transpose(nedFromFixed(x.geodetic.latitude,x.geodetic.longitude))*V3{0,0,-1});
            if(dot(b*(x.thrustForce+x.aeroForce),up)>x.mass*dot(x.acceleration-x.gravity,up)){s_.clamped=false;return dt-left;}
        }
        Tt h=left;bool depleted=false;const double flow=massFlow();
        if(step_>0){
            Tt limit=step_;
            if(coast_>step_&&!s_.burning&&!s_.clamped&&!s_.landed){ // Ballistic above the atmosphere: smooth, longer steps.
                const double speed=norm(s_.velocity),spin=norm(s_.rate),c=secondsSinceJ2000(coast_);
                if(geodetic(env_.planet,env_.rotation.at(s_.time).fixed*s_.position).height>standardTop+speed*c)
                    limit=std::max(step_,std::min(coast_,spin>0?static_cast<Tt>(.05/spin*1e9):coast_)); // ≤0.05 rad per step.
            }
            h=std::min(h,limit);
        }
        if(flow>0){const double t=s_.propellant/flow*1e9;if(t<=static_cast<double>(h)+1){h=std::clamp<Tt>(static_cast<Tt>(t),1,h);depleted=true;}} // Within 1 ns.
        integrate(h);left-=h;
        if(depleted){s_.propellant=0;s_.burning=false;return dt-left;}
        if(!s_.clamped&&!s_.landed&&std::isfinite(env_.ground)){
            const Orientation o=env_.rotation.at(s_.time);const V3 rf=o.fixed*s_.position;Geodetic g=geodetic(env_.planet,rf);
            if(g.height<env_.ground){g.height=env_.ground;s_.landed=true;s_.burning=false;padPosition_=fixedPosition(env_.planet,g);padAttitude_=quaternion(o.fixed*matrix(s_.attitude));hold(s_);return dt-left;}
        }
    }
    return dt;
}
void Flight::ignite(){
    const Stage& st=vehicle_.stages[s_.stage];
    if(!(st.thrust>0))throw std::logic_error("Stage has no engine");
    if(!(s_.propellant>0))throw std::logic_error("Stage has no propellant");
    if(s_.landed)throw std::logic_error("Vehicle has landed");
    s_.burning=true;
}
void Flight::cutoff(){s_.burning=false;}
void Flight::release(){s_.clamped=false;}
void Flight::separate(){
    if(s_.stage+1>=static_cast<int>(vehicle_.stages.size()))throw std::logic_error("No stage to separate");
    const double old=mass(vehicle_.stages[s_.stage],s_.propellant).cg;const Stage& next=vehicle_.stages[s_.stage+1];
    const V3 shift=matrix(s_.attitude)*V3{old-next.cgFull,0,0};
    s_.position+=shift;s_.velocity+=cross(matrix(s_.attitude)*s_.rate,shift);
    ++s_.stage;s_.propellant=next.propellant;s_.burning=false;s_.throttle=1;s_.gimbalPitch=s_.gimbalYaw=0;
    if(s_.clamped||s_.landed){padPosition_=env_.rotation.at(s_.time).fixed*s_.position;}
}
void Flight::throttle(double t){
    if(!std::isfinite(t))throw std::invalid_argument("Invalid throttle");
    s_.throttle=std::clamp(t,vehicle_.stages[s_.stage].minThrottle,1.);
}
void Flight::gimbal(double p,double y){
    if(!std::isfinite(p)||!std::isfinite(y))throw std::invalid_argument("Invalid gimbal");
    const double g=vehicle_.stages[s_.stage].gimbal;s_.gimbalPitch=std::clamp(p,-g,g);s_.gimbalYaw=std::clamp(y,-g,g);
}
void Flight::torque(V3 t){
    if(!std::isfinite(t.x)||!std::isfinite(t.y)||!std::isfinite(t.z))throw std::invalid_argument("Invalid torque");
    s_.torque=t;
}
std::uint64_t Flight::hash()const{
    std::uint64_t h=1469598103934665603ull;
    auto mix=[&](const void* p,std::size_t n){const auto* c=static_cast<const unsigned char*>(p);for(std::size_t i=0;i<n;++i){h^=c[i];h*=1099511628211ull;}};
    const double d[]{s_.position.x,s_.position.y,s_.position.z,s_.velocity.x,s_.velocity.y,s_.velocity.z,s_.attitude.w,s_.attitude.x,s_.attitude.y,s_.attitude.z,
        s_.rate.x,s_.rate.y,s_.rate.z,s_.propellant,s_.throttle,s_.gimbalPitch,s_.gimbalYaw,s_.torque.x,s_.torque.y,s_.torque.z,padPosition_.x,padPosition_.y,padPosition_.z};
    mix(&s_.time,sizeof s_.time);mix(d,sizeof d);const int flags[]{s_.stage,s_.burning,s_.clamped,s_.landed};mix(flags,sizeof flags);
    return h;
}
}
