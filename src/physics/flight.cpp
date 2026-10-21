#include "physics/flight.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <stdexcept>
namespace albert::physics {
namespace {
constexpr double degrees=180/3.14159265358979323846;
// The closest points of segments p0-p1 and q0-q1, as their parameters (0-1) along each (Ericson, Real-Time Collision
// Detection, 5.1.9).
void closest(V3 p0,V3 p1,V3 q0,V3 q1,double& s,double& t){
    const V3 d1=p1-p0,d2=q1-q0,r=p0-q0;const double a=dot(d1,d1),e=dot(d2,d2),f=dot(d2,r);
    if(a<=1e-12&&e<=1e-12){s=t=0;return;}
    if(a<=1e-12){s=0;t=std::clamp(f/e,0.,1.);return;}
    const double c=dot(d1,r);if(e<=1e-12){t=0;s=std::clamp(-c/a,0.,1.);return;}
    const double b=dot(d1,d2),den=a*e-b*b;s=den>1e-12?std::clamp((b*f-c*e)/den,0.,1.):0.;t=(b*s+f)/e;
    if(t<0){t=0;s=std::clamp(-c/a,0.,1.);}else if(t>1){t=1;s=std::clamp((b-c)/a,0.,1.);}
}
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
Air climatology(Center c,double h){return c==Center::earth?standardAtmosphere(h):c==Center::mars?marsAtmosphere(h):Air{};}
double airTop(Center c){return c==Center::earth?standardTop:c==Center::mars?marsTop:0.;}
}
Environment environment(Center c,std::shared_ptr<const Gravity> field,int degree){
    Environment e;e.center=c;
    if(c!=Center::earth){e.planet=planet(c,field.get(),degree);e.rotation=Rotation::body(c);e.field=std::move(field);}
    return e;
}
namespace {
std::uint64_t mixSeed(std::uint64_t& x){std::uint64_t z=(x+=0x9e3779b97f4a7c15ull);z=(z^(z>>30))*0xbf58476d1ce4e5b9ull;z=(z^(z>>27))*0x94d049bb133111ebull;return z^(z>>31);}
}
Turbulence::Turbulence(double sigma,std::uint64_t seed):sigma_(sigma){for(auto& r:rng_)r=mixSeed(seed);}
double Turbulence::normal(){ // xoshiro256** and Box-Muller (one value of the pair: simple, deterministic).
    auto rotl=[](std::uint64_t x,int k){return x<<k|x>>(64-k);};
    auto next=[&]{const std::uint64_t r=rotl(rng_[1]*5,7)*9,t=rng_[1]<<17;rng_[2]^=rng_[0];rng_[3]^=rng_[1];rng_[1]^=rng_[2];rng_[0]^=rng_[3];rng_[2]^=t;rng_[3]=rotl(rng_[3],45);return r;};
    double a=static_cast<double>(next()>>11)*0x1.0p-53;while(a<=0)a=static_cast<double>(next()>>11)*0x1.0p-53;const double b=static_cast<double>(next()>>11)*0x1.0p-53;
    return std::sqrt(-2*std::log(a))*std::cos(2*3.14159265358979323846*b);
}
void Turbulence::scales(double height,double sigma,double& lu,double& lw,double& su,double& sw){
    const double ft=.3048,h=std::max(height/ft,10.);
    auto low=[&](double x,double& Lu,double& Lw,double& Su){const double k=.177+.000823*x;Lw=x;Lu=x/std::pow(k,1.2);Su=sigma/std::pow(k,.4);};
    double Lu,Lw,Su;if(h<=1000){low(h,Lu,Lw,Su);lu=Lu*ft;lw=Lw*ft;su=Su;sw=sigma;return;}
    if(h>=2000){lu=lw=1750*ft;su=sw=sigma;return;}
    low(1000,Lu,Lw,Su);const double f=(h-1000)/1000;lu=(Lu+(1750-Lu)*f)*ft;lw=(Lw+(1750-Lw)*f)*ft;su=Su+(sigma-Su)*f;sw=sigma;
}
V3 Turbulence::step(double dt,double airspeed,double height){
    if(!(sigma_>0)||!(dt>0))return gust();
    double lu,lw,su,sw;scales(height,sigma_,lu,lw,su,sw);const double V=std::max(airspeed,1.);
    const double tu=lu/V,a=std::exp(-dt/tu);u_=a*u_+su*std::sqrt(1-a*a)*normal(); // Exact first-order update: variance σ_u².
    // v and w: (1 + √3 T s)/(1 + T s)², gain σ sqrt(T/π), white noise of two-sided PSD π; Euler sub-steps of at most T/20.
    auto pole=[&](double* x,double T,double s){const int n=std::max(1,static_cast<int>(std::ceil(dt/(T/20))));const double h=dt/n,k=s*std::sqrt(T/3.14159265358979323846);
        for(int i=0;i<n;++i){const double e=k*std::sqrt(3.14159265358979323846/h)*normal();const double x0=x[0];x[0]+=h*(e-x[0])/T;x[1]+=h*(x0-x[1])/T;}};
    pole(v_,lu/V,su);pole(w_,lw/V,sw);return gust();
}
namespace {
void locate(const std::vector<double>& b,double x,std::size_t& i,double& t){ // Cell and fraction, clamped.
    if(b.size()<2){i=0;t=0;return;}
    x=std::clamp(x,b.front(),b.back());
    i=std::min<std::size_t>(std::upper_bound(b.begin(),b.end(),x)-b.begin(),b.size()-1)-1;t=(x-b[i])/(b[i+1]-b[i]);
}
}
double Table3::operator()(double m,double a,double d)const{
    std::size_t i,j,k;double u,v,w;locate(mach,m,i,u);locate(angle,a,j,v);locate(deflection,d,k,w);
    const std::size_t n=angle.size(),p=deflection.size(),i1=mach.size()<2?i:i+1,j1=n<2?j:j+1,k1=p<2?k:k+1;
    auto at=[&](std::size_t x,std::size_t y,std::size_t z){return values[(x*n+y)*p+z];};
    auto plane=[&](std::size_t x){return(1-v)*((1-w)*at(x,j,k)+w*at(x,j,k1))+v*((1-w)*at(x,j1,k)+w*at(x,j1,k1));};
    return(1-u)*plane(i)+u*plane(i1);
}
double Table::operator()(double m,double a)const{
    std::size_t i,j;double u,v;locate(mach,m,i,u);locate(angle,a,j,v);
    const std::size_t n=angle.size(),i1=mach.size()<2?i:i+1,j1=n<2?j:j+1;
    return(1-u)*((1-v)*values[i*n+j]+v*values[i*n+j1])+u*((1-v)*values[i1*n+j]+v*values[i1*n+j1]);
}
Flight::Flight(Vehicle vehicle,Environment environment,State state):vehicle_(std::move(vehicle)),env_(std::move(environment)),s_(state){
    if(vehicle_.stages.empty())throw std::invalid_argument("Vehicle without stages");
    auto finite=[](std::initializer_list<double> x){return std::all_of(x.begin(),x.end(),[](double v){return std::isfinite(v);});};
    if(!finite({vehicle_.torque,vehicle_.inertiaRate,vehicle_.diameter,vehicle_.sunArea,vehicle_.reflectivity}))throw std::invalid_argument("Non-finite vehicle value");
    for(auto& x:env_.structures){
        if(!finite({x.a.x,x.a.y,x.a.z,x.b.x,x.b.y,x.b.z,x.radius,x.stiffness,x.damping,x.friction})||!(x.radius>0)||!(x.stiffness>0)||!(x.damping>=0)||!(x.friction>=0)||
           (x.hinged&&(!finite({x.pivot.x,x.pivot.y,x.pivot.z,x.axis.x,x.axis.y,x.axis.z,x.rate,x.low,x.high,x.start})||!(norm(x.axis)>0)||!(x.rate>=0)||!(x.low<=x.start&&x.start<=x.high))))
            throw std::invalid_argument("Invalid structure: "+x.name);
        if(x.hinged)x.axis=x.axis/norm(x.axis);
        structures_.push_back({x.hinged?x.start:0.,x.hinged?x.start:0.});}
    place();
    for(auto& st:vehicle_.stages){
        for(const V3& h:st.hardPoints)if(!finite({h.x,h.y,h.z}))throw std::invalid_argument("Invalid hard point: "+st.name);
        if(!finite({st.massFull,st.massEmpty,st.propellant,st.cgFull,st.cgEmpty,st.reference,st.length,st.noseRadius,st.legStiffness,st.legDamping,st.friction,st.aero.area,st.aero.length,st.aero.span,
            st.inertiaFull.x,st.inertiaFull.y,st.inertiaFull.z,st.inertiaEmpty.x,st.inertiaEmpty.y,st.inertiaEmpty.z}))throw std::invalid_argument("Non-finite stage value: "+st.name);
        if(!(st.massEmpty>0)||!(st.propellant>=0)||std::abs(st.massFull-st.massEmpty-st.propellant)>1e-6*st.massFull)throw std::invalid_argument("Inconsistent stage mass: "+st.name);
        for(V3 i:{st.inertiaFull,st.inertiaEmpty})if(!(i.x>0&&i.y>0&&i.z>0))throw std::invalid_argument("Non-positive inertia: "+st.name);
        if(!finite({st.boiloff,st.residual,st.gas})||!(st.boiloff>=0&&st.boiloff<1)||!(st.residual>=0&&st.residual<=st.propellant)||!(st.gas>=0))
            throw std::invalid_argument("Invalid boil-off, residuals or thruster gas: "+st.name);
        if(st.engines.empty()&&st.thrust>0)st.engines.push_back({st.name,{st.nozzle,0,0},{1,0,0},st.thrust,st.exhaust,st.exitArea,st.minThrottle,st.gimbal,0}); // The shorthand.
        for(auto& e:st.engines){
            const double n=norm(e.direction);
            if(e.breathing){
                for(const Table* t:{&e.thrustTable,&e.fuelTable})if(t->mach.empty()||t->angle.empty()||t->values.size()!=t->mach.size()*t->angle.size())throw std::invalid_argument("Malformed jet table: "+st.name+" "+e.name);
                if(!(e.spool>=0)||!std::isfinite(e.spool))throw std::invalid_argument("Invalid spool time: "+st.name+" "+e.name);}
            else if(!(e.thrust>0)||!(e.exhaust>0)||!finite({e.thrust,e.exhaust}))throw std::invalid_argument("Engine without finite thrust or exhaust velocity: "+st.name+" "+e.name);
            if(!(n>0)||!std::isfinite(n)||!std::isfinite(e.position.x)||!std::isfinite(e.position.y)||!std::isfinite(e.position.z)||!(e.exitArea>=0)||!(e.minThrottle>0&&e.minThrottle<=1)||!(e.gimbal>=0)||!(e.gimbalRate>=0)
                ||!(e.spool>=0)||!std::isfinite(e.spool))
                throw std::invalid_argument("Invalid engine: "+st.name+" "+e.name);
            e.direction=e.direction/n;
        }
        for(auto& f:st.surfaces){
            if(!(f.limit>=0)||!(f.rate>=0)||!std::isfinite(f.limit+f.rate))throw std::invalid_argument("Invalid surface: "+st.name+" "+f.name);
            for(const Table3* t:{&f.lift,&f.drag,&f.side,&f.roll,&f.pitch,&f.yaw})
                if(t->mach.empty()||t->angle.empty()||t->deflection.empty()||t->values.size()!=t->mach.size()*t->angle.size()*t->deflection.size())throw std::invalid_argument("Malformed surface table: "+st.name+" "+f.name);
        }
        for(const auto& w:st.wheels)if(!finite({w.position.x,w.position.y,w.position.z,w.stiffness,w.damping,w.rolling,w.braking,w.cornering,w.steer})||!(w.stiffness>0)||!(w.damping>=0)
            ||!(w.rolling>=0)||!(w.braking>=w.rolling)||!(w.cornering>=0)||!(w.steer>=0))throw std::invalid_argument("Invalid wheel: "+st.name+" "+w.name);
        for(const auto& n:st.thermal)if(!finite({n.position.x,n.position.y,n.position.z,n.normal.x,n.normal.y,n.normal.z,n.area,n.capacity,n.emissivity,n.absorptivity,n.conductance,n.structure,n.factor,n.start,n.limit})
            ||!(n.area>0)||!(n.capacity>0)||!(n.factor>=0)||!(n.emissivity>=0&&n.emissivity<=1)||!(n.absorptivity>=0&&n.absorptivity<=1)||!(n.conductance>=0)||!(n.start>0)||!(n.structure>0)||!(n.limit>=0)||norm(n.normal)==0)
            throw std::invalid_argument("Invalid thermal node: "+st.name+" "+n.name);
        for(auto& t:st.rcs){
            const double n=norm(t.direction);
            if(!(t.thrust>0)||!(t.exhaust>0)||!(n>0)||!std::isfinite(n+t.position.x+t.position.y+t.position.z+t.thrust+t.exhaust))throw std::invalid_argument("Invalid thruster: "+st.name+" "+t.name);
            t.direction=t.direction/n;
        }
        for(const Table* t:{&st.aero.lift,&st.aero.drag,&st.aero.side,&st.aero.roll,&st.aero.pitch,&st.aero.yaw})
            if(t->mach.empty()||t->angle.empty()||t->values.size()!=t->mach.size()*t->angle.size())throw std::invalid_argument("Malformed coefficient table: "+st.name);
    }
    if(s_.stage<0||s_.stage>=static_cast<int>(vehicle_.stages.size()))throw std::invalid_argument("Invalid stage");
    for(const Engine& e:vehicle_.stages[s_.stage].engines){ // As the state's commands to every engine.
        const double p=std::clamp(s_.gimbalPitch,-e.gimbal,e.gimbal),y=std::clamp(s_.gimbalYaw,-e.gimbal,e.gimbal);
        engines_.push_back({s_.burning,false,std::clamp(s_.throttle,e.minThrottle,1.),p,y,p,y});}
    s_.burning=s_.burning&&!engines_.empty();
    surfaces_.assign(vehicle_.stages[s_.stage].surfaces.size(),SurfaceState{});duty_.assign(vehicle_.stages[s_.stage].rcs.size(),0.);allocate();
    if(!(s_.gas>0))s_.gas=vehicle_.stages[s_.stage].gas; // A fresh stage's thruster gas, full.
    for(const auto& n:vehicle_.stages[s_.stage].thermal)temperatures_.push_back(n.start);
    wheels_.assign(vehicle_.stages[s_.stage].wheels.size(),WheelState{});
    if(!(env_.turbulence>=0)||!std::isfinite(env_.turbulence))throw std::invalid_argument("Invalid turbulence");
    turbulence_=Turbulence(env_.turbulence,env_.turbulenceSeed);
    const Orientation o=env_.rotation.at(s_.time);
    padPosition_=o.fixed*s_.position;padAttitude_=quaternion(o.fixed*matrix(s_.attitude));
}
State Flight::pad(const Environment& e,const Vehicle& v,Tt t,Geodetic site,double heading,double pitch,double roll){
    // The lowest of the base, the legs and the wheels on the ground, in the pad's attitude.
    const Stage& st=v.stages.at(0);const M3 b=bodyToNed({heading,pitch,roll});double low=1e300;
    auto at=[&](V3 r){low=std::min(low,-(b*r).z);};at({st.cgFull-st.length,0,0});
    for(const V3& l:st.legs)at({st.cgFull-l.x,l.y,l.z});for(const Wheel& w:st.wheels)at({st.cgFull-w.position.x,w.position.y,w.position.z});
    site.height-=low;
    State s=free(e,v,t,site,{},{heading,pitch,roll});s.clamped=true;return s;
}
State Flight::free(const Environment& e,const Vehicle& v,Tt t,Geodetic g,V3 ned,V3 euler,V3 relative){
    State s;s.time=t;s.propellant=v.stages.at(0).propellant;
    const Orientation o=e.rotation.at(t);const M3 back=transpose(o.fixed),fromNed=transpose(nedFromFixed(g.latitude,g.longitude));
    s.position=back*fixedPosition(e.planet,g);s.velocity=back*(fromNed*ned)+cross(o.omega,s.position);
    const M3 b=back*fromNed*bodyToNed(euler);s.attitude=quaternion(b);s.rate=transpose(b)*o.omega+relative;
    return s;
}
double Flight::flowOf(std::size_t i)const{const Engine& e=vehicle_.stages[s_.stage].engines[i];return s_.burning&&engines_[i].on&&!e.breathing?(e.spool>0?engines_[i].spool:1.)*engines_[i].throttle*e.thrust/e.exhaust:0.;}
double Flight::massFlow()const{
    if(!(s_.propellant>vehicle_.stages[s_.stage].residual))return 0;
    const auto& es=vehicle_.stages[s_.stage].engines; // A running jet's flow depends on the air: the full sample's.
    for(std::size_t i=0;i<es.size();++i)if(es[i].breathing&&s_.burning&&engines_[i].on)return sample().massFlow;
    double f=0;for(std::size_t i=0;i<engines_.size();++i)f+=flowOf(i);
    const auto& rcs=vehicle_.stages[s_.stage].rcs;for(std::size_t i=0;i<rcs.size();++i)f+=duty_[i]*rcs[i].thrust/rcs[i].exhaust;
    return f;
}
std::vector<double> Flight::engineThrust()const{
    const auto& es=vehicle_.stages[s_.stage].engines;std::vector<double> t(es.size());if(es.empty())return t;
    const Sample x=sample();const double p=x.air.pressure;
    for(std::size_t i=0;i<es.size();++i)t[i]=!(s_.burning&&engines_[i].on)?0.:es[i].breathing?(x.air.density>0?engines_[i].spool*es[i].thrustTable(x.mach,x.geodetic.height*1e-3):0.)
        :std::max(0.,(es[i].spool>0?engines_[i].spool:1.)*engines_[i].throttle*es[i].thrust-p*es[i].exitArea);
    return t;
}
double Flight::bodies(Tt t)const{ // Third bodies at 60 s nodes, linear between them; returns the weight.
    constexpr Tt bucket=60*second;const Tt k=floorDiv(t,bucket);
    if(k!=perturbed_){bodies_=thirdBodies(env_.center,k*bucket,p0_);thirdBodies(env_.center,(k+1)*bucket,p1_);perturbed_=k;}
    return secondsSinceJ2000(t-k*bucket)/60.;
}
V3 Flight::tidal(const State& s)const{
    const double w=bodies(s.time);V3 a;
    for(int i=0;i<bodies_;++i)a+=physics::tidal(s.position,p0_[i].position+(p1_[i].position-p0_[i].position)*w,p0_[i].gm);
    return a;
}
V3 Flight::sun(Tt t)const{const double w=bodies(t);const int i=env_.center==Center::moon?1:0;return p0_[i].position+(p1_[i].position-p0_[i].position)*w;}
double sunlight(V3 r,V3 sun,double bodyRadius){
    constexpr double sunRadius=6.957e8;const V3 d=sun-r;const double dn=norm(d),rn=norm(r);
    if(!(rn>0))return 1;
    // Below the reference radius (low ground, oblate poles) the body is taken as a sphere just
    // under the point, so the local horizon still decides.
    const double a=std::asin(std::min(1.,sunRadius/dn)),b=std::asin(std::min(bodyRadius,rn*(1-1e-9))/rn),c=std::acos(std::clamp(-dot(r,d)/(rn*dn),-1.,1.));
    if(c>=a+b)return 1;
    if(c<=b-a)return 0;
    if(c<=a-b)return 1-b*b/(a*a);
    const double x=(c*c+a*a-b*b)/(2*c),y=std::sqrt(std::max(0.,a*a-x*x));
    return 1-(a*a*std::acos(std::clamp(x/a,-1.,1.))+b*b*std::acos(std::clamp((c-x)/b,-1.,1.))-c*y)/(3.14159265358979323846*a*a);
}
Flight::Rate Flight::rate(const State& s,Sample* out)const{
    const Stage& st=vehicle_.stages[s.stage];const Mass ms=mass(st,s.propellant);
    const Orientation o=env_.rotation.at(s.time);const V3 rf=o.fixed*s.position;const M3 back=transpose(o.fixed);
    V3 g=back*gravitation(env_.planet,rf);if(env_.sunMoon)g+=tidal(s);
    if(env_.relativity){constexpr double c2=299792458.*299792458.;const double r=norm(s.position),gm=env_.planet.gm;
        g+=(s.position*(4*gm/r-dot(s.velocity,s.velocity))+s.velocity*(4*dot(s.position,s.velocity)))*(gm/(c2*r*r*r));}
    const M3 b=matrix(s.attitude),bt=transpose(b);
    const Geodetic geo=geodetic(env_.planet,rf);const M3 ned=nedFromFixed(geo.latitude,geo.longitude);
    Air atm=!env_.atmosphere?Air{}:env_.air?env_.air->at(s.time,geo.latitude,geo.longitude,geo.height):climatology(env_.center,geo.height);
    if(!env_.wind)atm.wind={}; // Air at rest with the ground.
    if(atm.density>0)atm.wind+=gustNed_+wind_; // Turbulence over the step and the added wind (none in vacuum).
    const V3 relative=s.velocity-cross(o.omega,s.position),air=bt*(relative-back*(transpose(ned)*atm.wind));const double speed=norm(air);
    const double q=.5*atm.density*speed*speed,mach=atm.sound>0?speed/atm.sound:0.;
    const double alpha=speed>0?std::atan2(air.z,air.x):0.,beta=speed>0?std::asin(std::clamp(air.y/speed,-1.,1.)):0.;
    V3 fa,ma;
    if(q>0){
        const Aerodynamics& ae=st.aero;const double ad=alpha*degrees,bd=beta*degrees,k=q*ae.area;
        double cl=ae.lift(mach,ad),cd=ae.drag(mach,std::hypot(ad,bd)),cy=ae.side(mach,bd),cr=ae.roll(mach,ad),cm=ae.pitch(mach,ad),cn=ae.yaw(mach,bd);
        const double ca=std::cos(alpha),sa=std::sin(alpha);
        auto force=[&](double l,double d,double y){return k*(l*V3{sa,0,-ca}+V3{0,y,0})-k*d/speed*air;}; // Drag along the air velocity.
        const auto& fs=st.surfaces;V3 fp,mp,fn; // Control surfaces' increments at their deflections; placed ones' forces act where they are.
        for(std::size_t i=0;i<fs.size();++i){const double d=surfaces_[i].deflection*degrees,l=fs[i].lift(mach,ad,d),dr=fs[i].drag(mach,ad,d),y=fs[i].side(mach,bd,d);
            cr+=fs[i].roll(mach,ad,d);cm+=fs[i].pitch(mach,ad,d);cn+=fs[i].yaw(mach,bd,d);const V3 f=fs[i].normalSet?force(0,dr,y)+fs[i].normal*(k*l):force(l,dr,y);
            if(fs[i].placed){fp+=f;mp+=cross(V3{ms.cg-fs[i].position.x,fs[i].position.y,fs[i].position.z},f);}else if(fs[i].normalSet)fn+=f;else{cl+=l;cd+=dr;cy+=y;}}
        fa=force(cl,cd,cy)+fn;

        const V3 w=s.rate-bt*o.omega;const double hl=ae.length/(2*speed),hs=ae.span/(2*speed);
        ma={k*ae.span*(cr+ae.rollDamping*w.x*hs),k*ae.length*(cm+ae.pitchDamping*w.y*hl),k*ae.span*(cn+ae.yawDamping*w.z*hs)};
        ma+=cross(V3{ms.cg-st.reference,0,0},fa)+mp;fa+=fp;
    }
    V3 ft,mt;double thrust=0,flow=0,jet=0; // jet: Σ ṁ l², the exhaust's transverse momentum lever.
    if(s.burning)for(std::size_t i=0;i<engines_.size();++i){ // Steps end at depletion, so the engines hold through all RK4 stages.
        const EngineState& e=engines_[i];if(!e.on)continue;const Engine& en=st.engines[i];
        double t,f;const double l=ms.cg-en.position.x;
        if(en.breathing){t=atm.density>0?e.spool*en.thrustTable(mach,geo.height*1e-3):0.;f=t*en.fuelTable(mach,geo.height*1e-3);} // A jet: its tables.
        else{const double u=en.spool>0?e.spool:1.; // A rocket starting: its chamber filling (thrust and flow by its spool).
            t=std::max(0.,u*e.throttle*en.thrust-atm.pressure*en.exitArea);f=u*e.throttle*en.thrust/en.exhaust;}
        thrust+=t;flow+=f;jet+=f*l*l;
        // Yaw about body z, then pitch about body y, of the engine's direction.
        const double cp=std::cos(e.pitch),sp=std::sin(e.pitch),cy=std::cos(e.yaw),sy=std::sin(e.yaw);const V3 d=en.direction,yawed{d.x*cy+d.y*sy,-d.x*sy+d.y*cy,d.z};
        const V3 fi=t*V3{yawed.x*cp-yawed.z*sp,yawed.y,yawed.x*sp+yawed.z*cp};ft+=fi;mt+=cross(V3{l,en.position.y,en.position.z},fi);
    }
    V3 fc,mc; // Legs on the ground: spring-damper normal force and friction, at each contact point.
    if(!st.legs.empty()&&!s.clamped&&std::isfinite(env_.ground)&&!env_.water){
        const V3 wi=b*s.rate;
        for(const V3& leg:st.legs){
            const V3 r{ms.cg-leg.x,leg.y,leg.z},p=s.position+b*r;const Geodetic lg=geodetic(env_.planet,o.fixed*p);const double depth=env_.ground-lg.height;
            if(!(depth>0))continue;
            const V3 n=back*V3{std::cos(lg.latitude)*std::cos(lg.longitude),std::cos(lg.latitude)*std::sin(lg.longitude),std::sin(lg.latitude)};
            const V3 v=s.velocity+cross(wi,b*r)-cross(o.omega,p);const double vn=dot(v,n);const V3 vt=v-n*vn;
            const double fn=std::max(0.,st.legStiffness*depth-st.legDamping*vn),speedT=norm(vt);
            const V3 f=n*fn-vt*(st.friction*fn/std::max(speedT,.05)); // Linear below 5 cm/s: no stick-slip chatter.
            const V3 fb=bt*f;fc+=fb;mc+=cross(r,fb);
        }
    }
    if(!st.wheels.empty()&&!s.clamped&&std::isfinite(env_.ground)&&!env_.water){ // Wheels: struts, and friction split along the wheel's heading and across.
        const V3 wi=b*s.rate;
        for(std::size_t i=0;i<st.wheels.size();++i){const Wheel& w=st.wheels[i];
            const V3 r{ms.cg-w.position.x,w.position.y,w.position.z},p=s.position+b*r;const Geodetic lg=geodetic(env_.planet,o.fixed*p);const double depth=env_.ground-lg.height;
            if(!(depth>0))continue;
            const V3 n=back*V3{std::cos(lg.latitude)*std::cos(lg.longitude),std::cos(lg.latitude)*std::sin(lg.longitude),std::sin(lg.latitude)};
            const V3 v=s.velocity+cross(wi,b*r)-cross(o.omega,p);const double vn=dot(v,n);const V3 vt=v-n*vn;
            const double fn=std::max(0.,w.stiffness*depth-w.damping*vn);
            const double a=wheels_[i].steer;V3 h=b*V3{std::cos(a),std::sin(a),0};h=h-n*dot(h,n);const double hn=norm(h);
            const V3 along=hn>1e-9?h/hn:V3{},across=cross(n,along);const double va=dot(vt,along),vc=dot(vt,across);
            const double mu=w.rolling+wheels_[i].brake*(w.braking-w.rolling); // Linear below 5 cm/s, as the legs.
            const V3 f=n*fn-along*(mu*fn*va/std::max(std::abs(va),.05))-across*(w.cornering*fn*vc/std::max(std::abs(vc),.05));
            const V3 fb=bt*f;fc+=fb;mc+=cross(r,fb);
        }
    }
    if(env_.water&&!s.clamped&&std::isfinite(env_.ground)){ // Water: the stack as a cylinder in 20 slices, each buoyed and dragged by its wet part.
        constexpr int slices=20;constexpr double rho=1025,cd=1;const double rad=vehicle_.diameter/2,dl=st.length/slices,volume=3.14159265358979323846*rad*rad*dl;
        const V3 wi=b*s.rate,axis=b*V3{1,0,0};const double gl=env_.planet.gm/dot(s.position,s.position);
        for(int i=0;i<slices;++i){
            const V3 r{ms.cg-(i+.5)*dl,0,0},p=s.position+b*r;const Geodetic lg=geodetic(env_.planet,o.fixed*p);const double depth=env_.ground-lg.height;
            const V3 up=back*V3{std::cos(lg.latitude)*std::cos(lg.longitude),std::cos(lg.latitude)*std::sin(lg.longitude),std::sin(lg.latitude)};
            const double c=std::abs(dot(axis,up)),e=.5*dl*c+rad*std::sqrt(std::max(0.,1-c*c)); // The slice's vertical half-extent.
            const double wet=std::clamp((depth+e)/(2*e),0.,1.);if(!(wet>0))continue;
            const V3 v=s.velocity+cross(wi,b*r)-cross(o.omega,p);const double speed=norm(v);
            V3 f=up*(rho*gl*volume*wet);
            if(speed>0){const double ca=std::abs(dot(axis,v))/speed; // Cross-flow on the side; the end caps take the axial part.
                const double area=2*rad*dl*std::sqrt(std::max(0.,1-ca*ca))+(i==0||i==slices-1?3.14159265358979323846*rad*rad*ca:0.);f+=v*(-.5*rho*cd*area*speed*wet);
                f+=up*(-.2*rho*2*rad*dl*std::sqrt(gl*rad)*dot(v,up)*wet);} // Heave lost to the waves it makes (radiation damping, ~ρ A √(g R)).
            const V3 fb=bt*f;fc+=fb;mc+=cross(r,fb);
        }
    }
    if(!st.hardPoints.empty()&&!env_.structures.empty()&&!s.clamped){ // Hard points against structures: spring, damper, regularised friction.
        const V3 wi=b*s.rate;
        for(const V3& h:st.hardPoints){
            const V3 r{ms.cg-h.x,h.y,h.z},p=s.position+b*r,pf=o.fixed*p;
            for(std::size_t i=0;i<segments_.size();++i){const auto& x=env_.structures[i];const auto& g=segments_[i];
                const V3 ab=g.b-g.a;const double t=std::clamp(dot(pf-g.a,ab)/std::max(dot(ab,ab),1e-12),0.,1.);const V3 d=pf-(g.a+ab*t);const double dist=norm(d);
                if(!(dist<x.radius)||!(dist>0))continue;
                const V3 n=back*(d/dist),v=s.velocity+cross(wi,b*r)-cross(o.omega,p)-back*cross(g.spin,pf-g.pivot);const double vn=dot(v,n);const V3 vt=v-n*vn; // Relative to the structure.
                const double fn=std::max(0.,x.stiffness*(x.radius-dist)-x.damping*vn);const V3 f=n*fn-vt*(x.friction*fn/std::max(norm(vt),.05));
                const V3 fb=bt*f;fc+=fb;mc+=cross(r,fb);
            }
        }
    }
    if(vehicle_.crashSpeed>0&&!env_.structures.empty()&&!s.clamped){ // The hull against the structures: spring, damper and friction at its surface.
        const double rh=vehicle_.diameter/2;const V3 wi=b*s.rate,nf=o.fixed*(s.position+b*V3{ms.cg,0,0}),tf=o.fixed*(s.position+b*V3{ms.cg-st.length,0,0});
        for(std::size_t i=0;i<segments_.size();++i){const auto& x=env_.structures[i];const auto& g=segments_[i];double u,v;closest(nf,tf,g.a,g.b,u,v);
            const V3 d=nf+(tf-nf)*u-(g.a+(g.b-g.a)*v);const double dist=norm(d);if(!(dist<rh+x.radius)||!(dist>0))continue;
            const V3 pc=nf+(tf-nf)*u-d*(rh/dist),p=back*pc,r=p-s.position,n=back*(d/dist);
            const V3 vel=s.velocity+cross(wi,r)-cross(o.omega,p)-back*cross(g.spin,pc-g.pivot);const double vn=dot(vel,n);const V3 vt=vel-n*vn;
            const double fn=std::max(0.,x.stiffness*(rh+x.radius-dist)-x.damping*vn);const V3 f=n*fn-vt*(x.friction*fn/std::max(norm(vt),.05));
            const V3 fb=bt*f;fc+=fb;mc+=cross(bt*r,fb);}
    }
    V3 fs; // Solar radiation pressure, 1361 W/m² at 1 au, shadowed by the central body.
    if(vehicle_.sunArea>0&&env_.solarPressure){
        constexpr double au=149597870700.,pressure=1361./299792458.;const V3 d=sun(s.time)-s.position;const double dn=norm(d);
        const double nu=sunlight(s.position,d+s.position,env_.planet.radius);
        if(nu>0)fs=bt*(d*(-nu*pressure*au*au/(dn*dn*dn)*vehicle_.reflectivity*vehicle_.sunArea));
    }
    V3 fr,mr; // Thrusters at their duty cycles while propellant lasts; without any, the torque command is ideal.
    const double lim=st.rcs.empty()?vehicle_.torque:0.;
    double gas=0; // The thrusters' own gas when the stage has a supply, else they burn its propellant.
    if(st.gas>0?s.gas>0:s.propellant>0)for(std::size_t i=0;i<st.rcs.size();++i)if(duty_[i]>0){const Thruster& t=st.rcs[i];const V3 f=t.direction*(duty_[i]*t.thrust);fr+=f;mr+=cross(V3{ms.cg-t.position.x,t.position.y,t.position.z},f);
        (st.gas>0?gas:flow)+=duty_[i]*t.thrust/t.exhaust;}
    const V3 fx=push_.x!=0||push_.y!=0||push_.z!=0?bt*(back*(transpose(ned)*push_)):V3{}; // The outside force.
    const V3 force=fa+ft+fs+fc+fr+fx,moment=ma+mt+mc+mr+V3{std::clamp(s.torque.x,-lim,lim),std::clamp(s.torque.y,-lim,lim),std::clamp(s.torque.z,-lim,lim)};
    // d(Iω)/dt with the propellant's inertia loss; the exhaust carries transverse angular
    // momentum ṁ l² ω through the nozzle (jet damping).
    const V3 iw{ms.inertia.x*s.rate.x,ms.inertia.y*s.rate.y,ms.inertia.z*s.rate.z};V3 rhs=moment-cross(s.rate,iw);
    if(flow>0){const V3 di=(st.inertiaFull-st.inertiaEmpty)*(vehicle_.inertiaRate*flow/st.propellant);rhs+=V3{di.x*s.rate.x,di.y*s.rate.y,di.z*s.rate.z};
        if(vehicle_.jetDamping)rhs+=-jet*V3{0,s.rate.y,s.rate.z};}
    // Sutton-Graves convective stagnation heating, k for air and for the Martian CO2 mix (kg^½/m).
    const double heat=atm.density>0?(env_.center==Center::mars?1.9027e-4:1.7415e-4)*std::sqrt(atm.density/st.noseRadius)*speed*speed*speed:0.;
    Rate r{s.velocity,g+b*(force/ms.m),spin(s.attitude,s.rate),{rhs.x/ms.inertia.x,rhs.y/ms.inertia.y,rhs.z/ms.inertia.z},-flow,force/ms.m,heat,-gas};
    if(out){
        Sample& x=*out;x.mass=ms.m;x.mach=mach;x.dynamicPressure=q;x.alpha=alpha;x.beta=beta;x.thrust=thrust;x.massFlow=flow;x.air=atm;x.geodetic=geo;
        x.fixedPosition=rf;x.fixedVelocity=o.fixed*relative;x.nedVelocity=ned*x.fixedVelocity;
        const M3 c=ned*o.fixed*b;x.nedAttitude=quaternion(c);x.euler={std::atan2(c.m[1][0],c.m[0][0]),-std::asin(std::clamp(c.m[2][0],-1.,1.)),std::atan2(c.m[2][1],c.m[2][2])};
        x.airBody=air*-1.;x.contactForce=fc;x.aeroForce=fa;x.aeroMoment=ma;x.thrustForce=ft;x.rcsForce=fr;x.rcsMoment=mr;x.gravity=g;x.acceleration=r.a;x.specificForce=force/ms.m;x.heatFlux=heat;
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
    if(s_.clamped||s_.landed){const Sample x=sample();increments_.velocity+=x.specificForce*dt;increments_.angle+=s_.rate*dt;increments_.seconds+=dt;
        s_.propellant=std::max(0.,s_.propellant-massFlow()*dt);s_.heatLoad+=x.heatFlux*dt;s_.time+=h;hold(s_);boil(dt);return;}
    auto at=[](const State& s,const Rate& k,double d,Tt t){State r=s;r.time=t;r.position+=k.v*d;r.velocity+=k.a*d;
        r.attitude={s.attitude.w+k.q.w*d,s.attitude.x+k.q.x*d,s.attitude.y+k.q.y*d,s.attitude.z+k.q.z*d};r.rate+=k.w*d;r.propellant+=k.m*d;r.gas+=k.gas*d;return r;};
    const Tt t0=s_.time,mid=t0+h/2;
    const Rate k1=rate(s_,nullptr),k2=rate(at(s_,k1,dt/2,mid),nullptr),k3=rate(at(s_,k2,dt/2,mid),nullptr),k4=rate(at(s_,k3,dt,t0+h),nullptr);
    const double w=dt/6;const V3 rate0=s_.rate;
    s_.position+=(k1.v+2*k2.v+2*k3.v+k4.v)*w;s_.velocity+=(k1.a+2*k2.a+2*k3.a+k4.a)*w;s_.rate+=(k1.w+2*k2.w+2*k3.w+k4.w)*w;
    s_.attitude=normalized({s_.attitude.w+(k1.q.w+2*k2.q.w+2*k3.q.w+k4.q.w)*w,s_.attitude.x+(k1.q.x+2*k2.q.x+2*k3.q.x+k4.q.x)*w,
        s_.attitude.y+(k1.q.y+2*k2.q.y+2*k3.q.y+k4.q.y)*w,s_.attitude.z+(k1.q.z+2*k2.q.z+2*k3.q.z+k4.q.z)*w});
    increments_.velocity+=(k1.f+2*k2.f+2*k3.f+k4.f)*w;increments_.angle+=(rate0+(k1.w+k2.w+k3.w)*w)*dt;increments_.seconds+=dt; // Stage rates, Simpson-weighted.
    s_.propellant=std::max(0.,s_.propellant+(k1.m+2*k2.m+2*k3.m+k4.m)*w);s_.heatLoad+=(k1.heat+2*k2.heat+2*k3.heat+k4.heat)*w;s_.time=t0+h;
    s_.gas=std::max(0.,s_.gas+(k1.gas+2*k2.gas+2*k3.gas+k4.gas)*w);boil(dt);
}
void Flight::boil(double seconds){ // Cryogens boiling off the tanks, vented: a share of what is left a day.
    if(const double b=vehicle_.stages[static_cast<std::size_t>(s_.stage)].boiloff;b>0)s_.propellant*=std::exp(-b*seconds/86400);
}
Tt Flight::advance(Tt dt){
    if(dt<=0)throw std::invalid_argument("Non-positive step");
    for(Tt left=dt;left>0;){
        if(s_.clamped&&s_.burning&&!env_.holdDown){
            const Sample x=sample();const M3 b=matrix(s_.attitude),back=transpose(env_.rotation.at(s_.time).fixed);
            const V3 up=back*(transpose(nedFromFixed(x.geodetic.latitude,x.geodetic.longitude))*V3{0,0,-1});
            if(dot(b*(x.thrustForce+x.aeroForce),up)>x.mass*dot(x.acceleration-x.gravity,up)){s_.clamped=false;return dt-left;}
        }
        Tt h=left;bool depleted=false;const double flow=massFlow();
        if(step_>0){
            Tt limit=step_;
            if(coast_>step_&&!s_.burning&&!s_.clamped&&!s_.landed&&!s_.contact&&push_.x==0&&push_.y==0&&push_.z==0&&s_.torque.x==0&&s_.torque.y==0&&s_.torque.z==0&&std::all_of(duty_.begin(),duty_.end(),[](double d){return d==0;})){ // Ballistic, torque-free, above the atmosphere and the ground.
                const double speed=norm(s_.velocity),spin=norm(s_.rate),c=secondsSinceJ2000(coast_),h=geodetic(env_.planet,env_.rotation.at(s_.time).fixed*s_.position).height;
                const double r=norm(s_.position),fall=.5*env_.planet.gm/(r*r)*c*c; // Gravity can close the gap as fast as speed.
                if(h>airTop(env_.center)+speed*c&&!(h-env_.ground<=speed*c+fall+vehicle_.stages[s_.stage].length))
                    limit=std::max(step_,std::min(coast_,spin>0?static_cast<Tt>(.05/spin*1e9):coast_)); // ≤0.05 rad per step.
            }
            h=std::min(h,limit);
        }
        if(const Stage& st=vehicle_.stages[s_.stage];(!st.legs.empty()||!st.wheels.empty()||env_.water)&&!s_.clamped&&!s_.landed&&std::isfinite(env_.ground)){
            // Legs or wheels on or near the ground, or the hull near the water: steps the springs, damping, friction and buoyancy keep stable.
            const Orientation o=env_.rotation.at(s_.time);const Mass ms=mass(st,s_.propellant);double reach=0;
            for(const V3& leg:st.legs)reach=std::max(reach,norm(V3{ms.cg-leg.x,leg.y,leg.z}));
            for(const Wheel& w:st.wheels)reach=std::max(reach,norm(V3{ms.cg-w.position.x,w.position.y,w.position.z}));
            if(env_.water)reach=std::max(reach,st.length+vehicle_.diameter);
            const double r=norm(s_.position),g=env_.planet.gm/(r*r),t=static_cast<double>(h)*1e-9,speed=norm(s_.velocity-cross(o.omega,s_.position));
            const double clearance=geodetic(env_.planet,o.fixed*s_.position).height-env_.ground-reach;
            if(s_.contact||clearance<speed*t+.5*g*t*t+1){
                const double n=static_cast<double>(st.legs.size());double rate=std::max({std::sqrt(n*st.legStiffness/ms.m),n*st.legDamping/ms.m,st.legs.empty()?0.:st.friction*g/.05,1.});
                double k=0,c=0,mu=0;for(const Wheel& w:st.wheels){k+=w.stiffness;c+=w.damping;mu=std::max({mu,w.braking,w.cornering});}
                if(!st.wheels.empty())rate=std::max({rate,std::sqrt(k/ms.m),c/ms.m,mu*g/.05});
                if(env_.water){const double rad=vehicle_.diameter/2,area=2*rad*st.length; // Buoyancy, heave damping and the explicit quadratic drag.
                    rate=std::max({rate,std::sqrt(1025*g*3.14159265358979323846*rad*rad/ms.m),.2*1025*area*std::sqrt(g*rad)/ms.m,.5*1025*area*speed/ms.m});}
                h=std::min(h,std::max<Tt>(1000,static_cast<Tt>(.5/rate*1e9))); // Half the RK4 bound on the stiffest mode.
            }
        }
        if(const Stage& st=vehicle_.stages[s_.stage];!st.hardPoints.empty()&&!env_.structures.empty()&&!s_.clamped){ // Near a structure: its stiff contact's step.
            const Orientation o=env_.rotation.at(s_.time);const V3 pf=o.fixed*s_.position;const Mass ms=mass(st,s_.propellant);double near=1e300,reach=0,rate=1;
            for(const V3& p:st.hardPoints)reach=std::max(reach,norm(V3{ms.cg-p.x,p.y,p.z}));
            for(std::size_t i=0;i<segments_.size();++i){const auto& x=env_.structures[i];const auto& g=segments_[i];
                const V3 ab=g.b-g.a;const double t=std::clamp(dot(pf-g.a,ab)/std::max(dot(ab,ab),1e-12),0.,1.);near=std::min(near,norm(pf-(g.a+ab*t))-x.radius);
                rate=std::max({rate,std::sqrt(x.stiffness/ms.m),x.damping/ms.m,x.friction*env_.planet.gm/dot(s_.position,s_.position)/.05});}
            if(near<reach+norm(s_.velocity-cross(o.omega,s_.position))*static_cast<double>(h)*1e-9+10)h=std::min(h,std::max<Tt>(1000,static_cast<Tt>(.5/rate*1e9)));
        }
        if(flow>0){const double t=(s_.propellant-vehicle_.stages[s_.stage].residual)/flow*1e9;if(t<=static_cast<double>(h)+1){h=std::clamp<Tt>(static_cast<Tt>(t),1,h);depleted=true;}} // Within 1 ns; to the residuals.
        const Breakup was=broken_;slew(static_cast<double>(h)*1e-9);heat(static_cast<double>(h)*1e-9);integrate(h);left-=h;strike();
        if(broken_!=was)return dt-left; // A breakup ends the call: its instant kept.
        if(!depleted&&s_.burning&&!(s_.propellant>vehicle_.stages[s_.stage].residual))depleted=true; // A jet whose flow rose within the step (its spool) ran dry in it.
        if(depleted){s_.propellant=std::min(s_.propellant,vehicle_.stages[s_.stage].residual);s_.burning=false;for(auto& e:engines_)e.on=false;std::fill(duty_.begin(),duty_.end(),0.);return dt-left;}
        if(!s_.clamped&&!s_.landed&&std::isfinite(env_.ground)){
            const Orientation o=env_.rotation.at(s_.time);const V3 rf=o.fixed*s_.position;Geodetic g=geodetic(env_.planet,rf);
            { // One contact state for events and telemetry: legs and wheels by geometry; water and structures, when near, by their force.
                const Stage& st=vehicle_.stages[s_.stage];bool touching=false;
                if((!st.legs.empty()||!st.wheels.empty())&&!env_.water){
                    const double c=mass(st,s_.propellant).cg;const M3 b=matrix(s_.attitude);
                    for(const V3& leg:st.legs)touching=touching||geodetic(env_.planet,o.fixed*(s_.position+b*V3{c-leg.x,leg.y,leg.z})).height<env_.ground;
                    for(const Wheel& w:st.wheels)touching=touching||geodetic(env_.planet,o.fixed*(s_.position+b*V3{c-w.position.x,w.position.y,w.position.z})).height<env_.ground;
                }
                const bool wet=env_.water&&g.height-st.length-vehicle_.diameter<env_.ground,held=!st.hardPoints.empty()&&structureGap(rf)<st.length+10;
                if(wet||held)touching=touching||norm(sample().contactForce)>0;
                if(touching!=s_.contact){s_.contact=touching;return dt-left;} // Touchdown and lift-off end the call, so each transition keeps its instant.
            }
            if(g.height<env_.ground&&!env_.water){g.height=env_.ground;s_.landed=true;s_.burning=false;for(auto& e:engines_)e.on=false;std::fill(duty_.begin(),duty_.end(),0.);padPosition_=fixedPosition(env_.planet,g);padAttitude_=quaternion(o.fixed*matrix(s_.attitude));hold(s_);return dt-left;}
        }
    }
    return dt;
}
namespace {
std::size_t pick(int engine,std::size_t count){ // An engine's index, checked.
    if(engine<0||static_cast<std::size_t>(engine)>=count)throw std::invalid_argument("No engine, wheel, surface or thruster "+std::to_string(engine+1)+" on this stage");
    return static_cast<std::size_t>(engine);}
}
void Flight::ignite(int engine){
    if(broken_!=Breakup::none)return; // Nothing left to command.
    if(engines_.empty())throw std::logic_error("Stage has no engine");
    const Stage& st=vehicle_.stages[s_.stage];if(!(s_.propellant>st.residual))throw std::logic_error("Stage has no propellant");
    if(s_.landed)throw std::logic_error("Vehicle has landed");
    auto start=[&](std::size_t i){if(!engines_[i].on)s_.propellant=std::max(st.residual,s_.propellant-st.engines[i].start);engines_[i].on=true;}; // Spinning it up costs propellant.
    // A silent failure takes the command as a healthy engine would (it says it is lit) and gives nothing.
    if(engine>=0){const std::size_t i=pick(engine,engines_.size());auto& e=engines_[i];if(e.failed&&!e.silent)throw std::logic_error("Engine has failed");if(e.failed)e.claims=true;else start(i);}
    else{bool any=false;for(std::size_t i=0;i<engines_.size();++i){auto& e=engines_[i];if(!e.failed)start(i);else e.claims=e.silent;any=any||!e.failed||e.silent;}
        if(!any)throw std::logic_error("Every engine has failed");}
    s_.burning=std::any_of(engines_.begin(),engines_.end(),[](const EngineState& e){return e.on;});
}
void Flight::cutoff(int engine){
    if(engine>=0){auto& e=engines_[pick(engine,engines_.size())];e.on=e.claims=false;}else for(auto& e:engines_)e.on=e.claims=false;
    s_.burning=std::any_of(engines_.begin(),engines_.end(),[](const EngineState& e){return e.on;});
}
void Flight::stick(int engine){if(engine>=0)engines_[pick(engine,engines_.size())].stuck=true;else for(auto& e:engines_)e.stuck=true;}
void Flight::stickSurface(int index){if(index>=0)surfaces_[pick(index,surfaces_.size())].stuck=true;else for(auto& f:surfaces_)f.stuck=true;}
void Flight::terminate(){if(broken_==Breakup::none)breakUp(Breakup::terminated,norm(s_.velocity));}
void Flight::fail(int engine,bool silent){auto& e=engines_[pick(engine,engines_.size())];e.claims=silent&&(e.on||e.claims);e.failed=true;e.silent=silent;e.on=false;s_.burning=std::any_of(engines_.begin(),engines_.end(),[](const EngineState& x){return x.on;});}
void Flight::slew(double seconds){
    const auto& st=vehicle_.stages[s_.stage];const auto& es=st.engines;
    for(std::size_t i=0;i<engines_.size();++i){auto& e=engines_[i];const double step=es[i].gimbalRate*seconds;
        if(es[i].gimbalRate>0&&!e.stuck){e.pitch+=std::clamp(e.targetPitch-e.pitch,-step,step);e.yaw+=std::clamp(e.targetYaw-e.yaw,-step,step);}}
    for(std::size_t i=0;i<engines_.size();++i){auto& e=engines_[i];if(!es[i].breathing&&!(es[i].spool>0))continue; // Spool up or down: a jet to its throttle,
        const double target=s_.burning&&e.on?(es[i].breathing?e.throttle:1.):0.;                                       // a rocket to running
        e.spool=es[i].spool>0&&(es[i].breathing||target>0)?target+(e.spool-target)*std::exp(-seconds/es[i].spool):target;} // (out at once: its next start from nothing).
    for(std::size_t i=0;i<surfaces_.size();++i)if(st.surfaces[i].rate>0&&!surfaces_[i].stuck){const double step=st.surfaces[i].rate*seconds;auto& f=surfaces_[i];f.deflection+=std::clamp(f.target-f.deflection,-step,step);}
    { // Hinged structures toward their commands at their rates.
        bool moving=false;std::vector<double> before;
        for(std::size_t i=0;i<structures_.size();++i){auto& x=structures_[i];before.push_back(x.angle);const double step=env_.structures[i].rate*seconds;
            if(x.angle!=x.target){x.angle+=std::clamp(x.target-x.angle,-step,step);moving=true;}}
        if(moving||std::any_of(segments_.begin(),segments_.end(),[](const Segment& g){return g.spin.x!=0||g.spin.y!=0||g.spin.z!=0;}))place(before,seconds);
    }
    if(turbulence_.sigma()>0&&env_.atmosphere&&!s_.clamped&&!s_.landed){ // Gusts in the air-relative velocity's axes, held over the step.
        const Orientation o=env_.rotation.at(s_.time);const V3 rf=o.fixed*s_.position;const Geodetic geo=geodetic(env_.planet,rf);
        if(geo.height<airTop(env_.center)+1e5){
            const M3 ned=nedFromFixed(geo.latitude,geo.longitude);V3 v=ned*(o.fixed*(s_.velocity-cross(o.omega,s_.position))); // Relative to the air: the mean wind out.
            if(env_.wind)v=v-(env_.air?env_.air->at(s_.time,geo.latitude,geo.longitude,geo.height):climatology(env_.center,geo.height)).wind;const double speed=norm(v);
            const V3 g=turbulence_.step(seconds,speed,geo.height-(std::isfinite(env_.ground)?env_.ground:0.));
            const V3 x=speed>.1?v/speed:V3{1,0,0},h=std::hypot(x.x,x.y)>1e-6?V3{-x.y,x.x,0}/std::hypot(x.x,x.y):V3{0,1,0},z=cross(x,h);
            gustNed_=x*g.x+h*g.y+z*g.z;
        }else gustNed_={};
    }
}
double Flight::structureGap(V3 fixed)const{ // From the centre of mass to the nearest structure's surface (m).
    double gap=1e300;
    for(std::size_t i=0;i<segments_.size();++i){const auto& g=segments_[i];const V3 ab=g.b-g.a;const double t=std::clamp(dot(fixed-g.a,ab)/std::max(dot(ab,ab),1e-12),0.,1.);
        gap=std::min(gap,norm(fixed-(g.a+ab*t))-env_.structures[i].radius);}
    return gap;
}
void Flight::heat(double seconds){ // Explicit in the flow (the step's start), implicit in each node's temperature.
    const auto& nodes=vehicle_.stages[s_.stage].thermal;if(nodes.empty()||!(seconds>0))return;
    const Sample x=sample();const double sky=x.air.temperature>0?x.air.temperature:3.; // The air around, or deep space.
    for(std::size_t i=0;i<nodes.size();++i){temperatures_[i]=nodeStep(nodes[i],temperatures_[i],nodeHeating(nodes[i],x.heatFlux,x.airBody),sky,seconds);
        if(nodes[i].failure>0&&temperatures_[i]>nodes[i].failure&&broken_==Breakup::none)breakUp(Breakup::heat,0);}
}
void Flight::breakUp(Breakup why,double speed){
    broken_=why;brokenAt_=s_.time;brokenSpeed_=speed;s_.burning=false;for(auto& e:engines_)e.on=false;std::fill(duty_.begin(),duty_.end(),0.);s_.torque={};
}
void Flight::strike(){
    if(!(vehicle_.crashSpeed>0)||broken_!=Breakup::none||s_.clamped||s_.landed)return;
    const Stage& st=vehicle_.stages[s_.stage];const Mass ms=mass(st,s_.propellant);const Orientation o=env_.rotation.at(s_.time);const M3 b=matrix(s_.attitude),back=transpose(o.fixed);
    const double rh=vehicle_.diameter/2;const V3 wi=b*s_.rate;
    auto ground=[&](V3 r){const V3 p=s_.position+r;return s_.velocity+cross(wi,r)-cross(o.omega,p);}; // Relative to the ground, inertial axes.
    if(std::isfinite(env_.ground)&&!env_.water)for(const double x:{ms.cg,ms.cg-st.length}){ // Either end of the hull (a flat disc) below the ground, coming down.
        const V3 r=b*V3{x,0,0};const Geodetic g=geodetic(env_.planet,o.fixed*(s_.position+r));
        const V3 up=back*V3{std::cos(g.latitude)*std::cos(g.longitude),std::cos(g.latitude)*std::sin(g.longitude),std::sin(g.latitude)};
        const double a=dot(b*V3{1,0,0},up),low=g.height-rh*std::sqrt(std::max(1-a*a,0.)),down=-dot(ground(r),up);
        if(!(low<env_.ground)||!(down>0))continue;
        if(down>vehicle_.crashSpeed){breakUp(Breakup::ground,down);return;}
        s_.landed=true;s_.burning=false;for(auto& e:engines_)e.on=false;std::fill(duty_.begin(),duty_.end(),0.); // Set down gently: at rest where it touched.
        padPosition_=o.fixed*s_.position;padAttitude_=quaternion(o.fixed*matrix(s_.attitude));hold(s_);return;}
    const V3 nf=o.fixed*(s_.position+b*V3{ms.cg,0,0}),tf=o.fixed*(s_.position+b*V3{ms.cg-st.length,0,0});
    for(std::size_t i=0;i<segments_.size();++i){const auto& g=segments_[i];double u,v;closest(nf,tf,g.a,g.b,u,v); // Into a structure faster than the hull takes.
        const V3 d=nf+(tf-nf)*u-(g.a+(g.b-g.a)*v);const double dist=norm(d);if(!(dist<rh+env_.structures[i].radius)||!(dist>0))continue;
        const V3 pc=nf+(tf-nf)*u-d*(rh/dist),p=back*pc;const double in=-dot(ground(p-s_.position)-back*cross(g.spin,pc-g.pivot),back*(d/dist));
        if(in>vehicle_.crashSpeed){breakUp(Breakup::structure,in);return;}}
}
void Flight::surface(double d,int index){
    if(broken_!=Breakup::none)return; // Nothing left to command.
    if(!std::isfinite(d))throw std::invalid_argument("Invalid deflection");
    const auto& fs=vehicle_.stages[s_.stage].surfaces;
    auto set=[&](std::size_t i){auto& f=surfaces_[i];f.target=std::clamp(d,-fs[i].limit,fs[i].limit);if(!(fs[i].rate>0)&&!f.stuck)f.deflection=f.target;};
    if(index>=0)set(pick(index,fs.size()));else for(std::size_t i=0;i<fs.size();++i)set(i);
}
void Flight::brake(double f,int index){
    if(!std::isfinite(f))throw std::invalid_argument("Invalid brake");const double u=std::clamp(f,0.,1.);
    if(index>=0)wheels_[pick(index,wheels_.size())].brake=u;else for(auto& w:wheels_)w.brake=u;
}
void Flight::steer(double a,int index){
    if(!std::isfinite(a))throw std::invalid_argument("Invalid steering");const auto& ws=vehicle_.stages[s_.stage].wheels;
    auto set=[&](std::size_t i){wheels_[i].steer=std::clamp(a,-ws[i].steer,ws[i].steer);};if(index>=0)set(pick(index,ws.size()));else for(std::size_t i=0;i<ws.size();++i)set(i);
}
void Flight::swing(double a,int index){
    if(!std::isfinite(a))throw std::invalid_argument("Invalid structure angle");const auto& xs=env_.structures;
    auto set=[&](std::size_t i){if(!xs[i].hinged)return;auto& x=structures_[i];x.target=std::clamp(a,xs[i].low,xs[i].high);if(!(xs[i].rate>0))x.angle=x.target;};
    if(index>=0){if(!xs[pick(index,xs.size())].hinged)throw std::invalid_argument("The structure has no hinge");set(pick(index,xs.size()));}else for(std::size_t i=0;i<xs.size();++i)set(i);
    place();
}
void Flight::place(const std::vector<double>& before,double seconds){
    segments_.resize(env_.structures.size());
    for(std::size_t i=0;i<segments_.size();++i){const auto& x=env_.structures[i];auto& g=segments_[i];
        if(!x.hinged){g={x.a,x.b,x.a,{}};continue;}
        const double a=structures_[i].angle,c=std::cos(a),n=std::sin(a);const V3 k=x.axis;
        auto turn=[&](V3 p){const V3 v=p-x.pivot;return x.pivot+v*c+cross(k,v)*n+k*(dot(k,v)*(1-c));}; // Rodrigues about the hinge.
        g={turn(x.a),turn(x.b),x.pivot,i<before.size()&&seconds>0?k*((a-before[i])/seconds):V3{}};
    }
}
void Flight::thruster(double d,int index){
    if(broken_!=Breakup::none)return; // Nothing left to command.
    if(!std::isfinite(d))throw std::invalid_argument("Invalid duty cycle");
    const double u=std::clamp(d,0.,1.);if(index>=0)duty_[pick(index,duty_.size())]=u;else std::fill(duty_.begin(),duty_.end(),u);
}
// Duties u in [0, 1] minimising |A u - torque|²/2 + ε (Σu + |u|²/2), A's columns the thrusters' torques about the CG and ε
// 10⁻⁹ of the largest |Aᵢ|²: the torque within ~10⁻⁹, then the least propellant, then couples over lone thrusters (the
// ridge). A strictly convex quadratic on a box, solved exactly by the primal active-set method: the minimiser on the free
// set (Cholesky), a step to the first bound it crosses (that thruster then held there), and a held thruster freed when its
// multiplier says so.
void Flight::allocate(){
    const Stage& st=vehicle_.stages[s_.stage];const std::size_t n=st.rcs.size();if(!n)return;
    const double cg=mass(st,s_.propellant).cg;std::vector<V3> a(n);double big=0;
    for(std::size_t i=0;i<n;++i){const Thruster& t=st.rcs[i];a[i]=cross(V3{cg-t.position.x,t.position.y,t.position.z},t.direction*t.thrust);big=std::max(big,dot(a[i],a[i]));}
    std::fill(duty_.begin(),duty_.end(),0.);if(!(big>0))return;
    const double eps=1e-9*big;std::vector<double> h(n*n),c(n),u(n,0.),z(n),l(n*n);std::vector<char> held(n,1); // All held at 0 to start.
    for(std::size_t i=0;i<n;++i){c[i]=dot(a[i],s_.torque)-eps;for(std::size_t j=0;j<n;++j)h[i*n+j]=dot(a[i],a[j])+(i==j?eps:0.);}
    for(std::size_t k=0;k<8*n+8;++k){
        std::vector<std::size_t> f;for(std::size_t i=0;i<n;++i)if(!held[i])f.push_back(i);
        const std::size_t m=f.size();bool interior=true;
        if(m){ // The minimiser over the free thrusters, the held ones fixed: H_ff z = c_f - H_fh u_h.
            for(std::size_t i=0;i<m;++i)for(std::size_t j=0;j<=i;++j){double x=h[f[i]*n+f[j]];for(std::size_t q=0;q<j;++q)x-=l[i*m+q]*l[j*m+q];l[i*m+j]=i==j?std::sqrt(std::max(x,1e-300)):x/l[j*m+j];}
            for(std::size_t i=0;i<m;++i){double x=c[f[i]];for(std::size_t j=0;j<n;++j)if(held[j])x-=h[f[i]*n+j]*u[j];for(std::size_t q=0;q<i;++q)x-=l[i*m+q]*z[q];z[i]=x/l[i*m+i];}
            for(std::size_t i=m;i-->0;){double x=z[i];for(std::size_t q=i+1;q<m;++q)x-=l[q*m+i]*z[q];z[i]=x/l[i*m+i];}
            double step=1;std::size_t block=n;
            for(std::size_t i=0;i<m;++i){const double x=u[f[i]],d=z[i]-x;
                if(z[i]<0&&-x/d<step){step=-x/d;block=f[i];}else if(z[i]>1&&(1-x)/d<step){step=(1-x)/d;block=f[i];}}
            for(std::size_t i=0;i<m;++i)u[f[i]]=std::clamp(u[f[i]]+step*(z[i]-u[f[i]]),0.,1.);
            if(block<n){u[block]=u[block]<.5?0.:1.;held[block]=1;interior=false;}
        }
        if(!interior)continue;
        std::size_t worst=n;double most=0; // Optimal on this set: free the held thruster whose multiplier is most wrong, if any.
        for(std::size_t i=0;i<n;++i)if(held[i]){double g=-c[i];for(std::size_t j=0;j<n;++j)g+=h[i*n+j]*u[j];const double wrong=u[i]<=0?-g:g;if(wrong>most*(1+1e-12)&&wrong>1e-12*big){most=wrong;worst=i;}}
        if(worst==n)break;
        held[worst]=0;
    }
    duty_=u;
}
void Flight::release(){s_.clamped=false;}
void Flight::separate(){
    if(s_.stage+1>=static_cast<int>(vehicle_.stages.size()))throw std::logic_error("No stage to separate");
    const double old=mass(vehicle_.stages[s_.stage],s_.propellant).cg;const Stage& next=vehicle_.stages[s_.stage+1];
    const V3 shift=matrix(s_.attitude)*V3{old-next.cgFull,0,0};
    s_.position+=shift;s_.velocity+=cross(matrix(s_.attitude)*s_.rate,shift);
    ++s_.stage;s_.propellant=next.propellant;s_.burning=false;s_.throttle=1;s_.gimbalPitch=s_.gimbalYaw=0;
    engines_.assign(next.engines.size(),EngineState{});surfaces_.assign(next.surfaces.size(),SurfaceState{});duty_.assign(next.rcs.size(),0.);allocate();s_.gas=next.gas;
    temperatures_.clear();for(const auto& n:next.thermal)temperatures_.push_back(n.start);wheels_.assign(next.wheels.size(),WheelState{});
    if(s_.clamped||s_.landed){padPosition_=env_.rotation.at(s_.time).fixed*s_.position;}
}
void Flight::throttle(double t,int engine){
    if(broken_!=Breakup::none)return; // Nothing left to command.
    if(!std::isfinite(t))throw std::invalid_argument("Invalid throttle");
    const auto& es=vehicle_.stages[s_.stage].engines;
    auto set=[&](std::size_t i){engines_[i].throttle=std::clamp(t,es[i].minThrottle,1.);};
    if(engine>=0){set(pick(engine,es.size()));return;}
    double low=es.empty()?1.:es[0].minThrottle;for(std::size_t i=0;i<es.size();++i){set(i);low=std::min(low,es[i].minThrottle);}
    s_.throttle=std::clamp(t,low,1.);
}
void Flight::gimbal(double p,double y,int engine){
    if(broken_!=Breakup::none)return; // Nothing left to command.
    if(!std::isfinite(p)||!std::isfinite(y))throw std::invalid_argument("Invalid gimbal");
    const auto& es=vehicle_.stages[s_.stage].engines;
    auto set=[&](std::size_t i){auto& e=engines_[i];const double g=es[i].gimbal;e.targetPitch=std::clamp(p,-g,g);e.targetYaw=std::clamp(y,-g,g);
        if(!(es[i].gimbalRate>0)&&!e.stuck){e.pitch=e.targetPitch;e.yaw=e.targetYaw;}};
    if(engine>=0){set(pick(engine,es.size()));return;}
    double g=0;for(std::size_t i=0;i<es.size();++i){set(i);g=std::max(g,es[i].gimbal);}
    s_.gimbalPitch=std::clamp(p,-g,g);s_.gimbalYaw=std::clamp(y,-g,g);
}
void Flight::torque(V3 t){
    if(broken_!=Breakup::none)return; // Nothing left to command.
    if(!std::isfinite(t.x)||!std::isfinite(t.y)||!std::isfinite(t.z))throw std::invalid_argument("Invalid torque");
    s_.torque=t;allocate();
}
std::uint64_t Flight::hash()const{
    std::uint64_t h=1469598103934665603ull;
    auto mix=[&](const void* p,std::size_t n){const auto* c=static_cast<const unsigned char*>(p);for(std::size_t i=0;i<n;++i){h^=c[i];h*=1099511628211ull;}};
    const double d[]{s_.position.x,s_.position.y,s_.position.z,s_.velocity.x,s_.velocity.y,s_.velocity.z,s_.attitude.w,s_.attitude.x,s_.attitude.y,s_.attitude.z,
        s_.rate.x,s_.rate.y,s_.rate.z,s_.propellant,s_.heatLoad,s_.throttle,s_.gimbalPitch,s_.gimbalYaw,s_.torque.x,s_.torque.y,s_.torque.z,padPosition_.x,padPosition_.y,padPosition_.z};
    mix(&s_.time,sizeof s_.time);mix(d,sizeof d);const double pad[]{padAttitude_.w,padAttitude_.x,padAttitude_.y,padAttitude_.z};mix(pad,sizeof pad);const int flags[]{s_.stage,s_.burning,s_.clamped,s_.landed,s_.contact};mix(flags,sizeof flags);
    for(const auto& e:engines_){const double x[]{e.throttle,e.pitch,e.yaw,e.targetPitch,e.targetYaw};const int b[]{e.on,e.failed,e.silent,e.claims,e.stuck};mix(x,sizeof x);mix(b,sizeof b);}
    for(const auto& x:structures_){const double a[]{x.angle,x.target};mix(a,sizeof a);}
    {const int why=static_cast<int>(broken_);mix(&why,sizeof why);mix(&brokenAt_,sizeof brokenAt_);mix(&brokenSpeed_,sizeof brokenSpeed_);mix(&s_.gas,sizeof s_.gas);}
    for(const auto& f:surfaces_){mix(&f.deflection,8);mix(&f.target,8);const int k=f.stuck;mix(&k,sizeof k);}for(double u:duty_)mix(&u,8);
    {const V3 g=turbulence_.gust();const double t[]{g.x,g.y,g.z,gustNed_.x,gustNed_.y,gustNed_.z,wind_.x,wind_.y,wind_.z,push_.x,push_.y,push_.z};mix(t,sizeof t);}for(double t:temperatures_)mix(&t,8);for(const auto& w:wheels_){mix(&w.brake,8);mix(&w.steer,8);}
    for(const auto& e:engines_)mix(&e.spool,8);
    return h;
}
}
