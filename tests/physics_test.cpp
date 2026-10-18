#include "physics/vehicles.hpp"
#include "gravity_turn.hpp"
#include <erfa.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using namespace albert::physics;
namespace {
#include "nesc_fixture.inc"
#include "physics/egm2008.inc"
constexpr double ft=.3048,pi=3.14159265358979323846,deg=pi/180;
void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
void near(double a,double b,double tol,const char* m){if(!(std::abs(a-b)<=tol)){std::fprintf(stderr,"%s: %.12g vs %.12g (tolerance %.3g)\n",m,a,b,tol);throw std::runtime_error(m);}}
// WGS 84 with the NESC J2; cases 4-5 use the authalic sphere.
Environment nesc(bool round,bool rotating,bool drag){
    Environment e;
    e.planet=round?Planet{3.986004418e14,6371007.1809,0,0,6371007.1809,{},0}:Planet{3.986004418e14,6378137.,1/298.257223563,7.292115e-5,6378137.,{0,0,1.08262982e-3},2};
    e.rotation=Rotation::uniform(rotating?7.292115e-5:0);e.atmosphere=drag;e.sunMoon=false;return e;
}
// Case 17: first stage ignites at release, coasts 96.79 s after depletion, then stages.
Flight rocket(Tt step,double until,std::vector<State>* trace=nullptr,double every=25){
    const Environment e=nesc(false,true,true);const Vehicle v=nescTwoStage();
    Flight f(v,e,Flight::free(e,v,0,{0,0,0},{0,0,-.1},{90*deg,55.22*deg,0}));f.ignite();
    Tt staging=-1;const Tt end=static_cast<Tt>(std::llround(until*1e9));Tt next=0;
    while(f.state().time<end){
        if(trace&&f.state().time>=next){trace->push_back(f.state());next+=static_cast<Tt>(every*1e9);}
        Tt h=std::min(step,end-f.state().time);
        if(staging<0&&f.state().stage==0&&!f.state().burning)staging=f.state().time+static_cast<Tt>(96.79e9);
        if(staging>=0&&f.state().stage==0){if(f.state().time>=staging){f.separate();f.ignite();}else h=std::min(h,staging-f.state().time);}
        if(trace)h=std::min(h,next-f.state().time);
        f.advance(h);
    }
    if(trace)trace->push_back(f.state());
    return f;
}
}
int main(){try{
    // Geodesy: closed-form inverse against the forward transform.
    std::mt19937_64 random(3);std::uniform_real_distribution<double> u(0,1);
    for(int i=0;i<20000;++i){
        const Geodetic g{(u(random)-.5)*pi,(u(random)*2-1)*pi,-5000+u(random)*2e6};const Geodetic r=geodetic(earth,fixedPosition(earth,g));
        near(r.height,g.height,1e-6,"Geodetic height");near(r.latitude,g.latitude,1e-13,"Geodetic latitude");
        if(std::abs(g.latitude)<pi/2-1e-9)near(std::remainder(r.longitude-g.longitude,2*pi),0,1e-13,"Longitude");
    }
    // Zonal attraction against the numerical gradient of its potential.
    Planet zonal=earth;zonal.egm2008=false;zonal.degree=4;
    for(V3 r:{V3{6.5e6,1e6,2e6},V3{1e5,-3e5,6.4e6},V3{-4e6,4e6,-3e6}}){
        auto potential=[&](V3 p){const double rn=norm(p),s=p.z/rn;double sum=0,p0=1,p1=s;
            for(int n=2;n<=4;++n){const double p2=((2*n-1)*s*p1-(n-1)*p0)/n;sum+=earth.zonal[n]*std::pow(earth.gravityRadius/rn,n)*p2;p0=p1;p1=p2;}
            return earth.gm/rn*(1-sum);};
        const V3 a=gravitation(zonal,r);const double h=1;
        const V3 n{(potential(r+V3{h,0,0})-potential(r-V3{h,0,0}))/(2*h),(potential(r+V3{0,h,0})-potential(r-V3{0,h,0}))/(2*h),(potential(r+V3{0,0,h})-potential(r-V3{0,0,h}))/(2*h)};
        near(norm(a-n),0,1e-8,"Zonal gradient");
    }
    // EGM2008: Cunningham recursion against the gradient of the potential summed with
    // directly normalised Legendre functions; geoid heights through Bruns (T/γ) against
    // published EGM2008/EGM96 values, with the WGS 84 normal field (degrees 71+ omitted).
    {
        auto potential=[](V3 r,int degree,bool disturbing){
            const double rn=norm(r),s=r.z/rn,c=std::sqrt(r.x*r.x+r.y*r.y)/rn,lon=std::atan2(r.y,r.x),q=6378136.3/rn;
            std::vector<double> p(static_cast<std::size_t>((degree+1)*(degree+2)/2));auto at=[&](int n,int m)->double&{return p[static_cast<std::size_t>(n*(n+1)/2+m)];};
            at(0,0)=1;
            for(int m=0;m<=degree;++m){
                if(m)at(m,m)=(m==1?std::sqrt(3.):std::sqrt((2*m+1)/(2.*m)))*c*at(m-1,m-1);
                if(m<degree)at(m+1,m)=std::sqrt(2*m+3.)*s*at(m,m);
                for(int n=m+2;n<=degree;++n)at(n,m)=std::sqrt((2*n-1.)*(2*n+1)/((n-m)*(n+m)))*s*at(n-1,m)-std::sqrt((2*n+1.)*(n+m-1)*(n-m-1)/((n-m)*(n+m)*(2*n-3.)))*at(n-2,m);
            }
            double sum=disturbing?0:1,qn=q;
            for(int n=2;n<=degree;++n){qn*=q;for(int m=0;m<=n;++m)sum+=qn*at(n,m)*(egm2008[n*(n+1)/2+m][0]*std::cos(m*lon)+egm2008[n*(n+1)/2+m][1]*std::sin(m*lon));}
            if(!disturbing)return 3.986004415e14/rn*sum;
            // Minus the WGS 84 normal potential (even zonals, its GM and radius).
            constexpr double normal[]{-.484166774985e-3,.790303733511e-6,-.168724961151e-8,.346052468394e-11,-.265002225747e-14};
            double u=3.986004415e14/rn*(1+sum)-3.986004418e14/rn,qw=6378137./rn,qk=qw;
            for(int k=0;k<5;++k){const int n=2*k+2;qk=std::pow(qw,n);u-=3.986004418e14/rn*qk*normal[k]*at(n,0);}
            return u;
        };
        for(int degree:{20,70})for(V3 r:{V3{6.4e6,1.2e6,1.9e6},V3{-2e6,-1e5,6.5e6},V3{4.4e6,-4.4e6,-2.5e6}}){
            const double h=2;const V3 a=harmonics(degree,r);
            const V3 n{(potential(r+V3{h,0,0},degree,false)-potential(r-V3{h,0,0},degree,false))/(2*h),(potential(r+V3{0,h,0},degree,false)-potential(r-V3{0,h,0},degree,false))/(2*h),(potential(r+V3{0,0,h},degree,false)-potential(r-V3{0,0,h},degree,false))/(2*h)};
            near(norm(a-n),0,2e-7,"EGM2008 gradient");
        }
        struct Geoid {double lat,lon,height,tolerance;};
        for(const Geoid& g:{Geoid{16.7758,-3.0094,28.7,5},Geoid{4.7,78.8,-106,8}}){ // Timbuktu (GeographicLib example); Indian Ocean low.
            const double phi=g.lat*deg,s2=std::sin(phi)*std::sin(phi),gamma=9.7803253359*(1+.00193185265241*s2)/std::sqrt(1-earth.e2()*s2);
            const double n=potential(fixedPosition(earth,{phi,g.lon*deg,0}),70,true)/gamma;
            std::printf("EGM2008 geoid at %.2f, %.2f: %.2f m (reference %.1f)\n",g.lat,g.lon,n,g.height);near(n,g.height,g.tolerance,"Geoid height");
        }
    }
    // U.S. Standard Atmosphere 1976 (published tables, 5 digits) and the NESC values.
    struct Ref {double z,t,p,rho;};
    for(const Ref& r:{Ref{0,288.15,101325,1.2250},Ref{10000,223.25,26500,.41351},Ref{30000,226.51,1197.0,.018410},Ref{50000,270.65,79.779,1.0269e-3},Ref{86000-1e-6,186.87,.37338,6.958e-6}}){
        const Air a=standardAtmosphere(r.z);near(a.temperature/r.t,1,3e-5,"USSA76 temperature");near(a.pressure/r.p,1,1e-4,"USSA76 pressure");near(a.density/r.rho,1,1e-4,"USSA76 density");
    }
    require(standardAtmosphere(86000).density==0&&standardAtmosphere(2e5).pressure==0,"Vacuum above 86 km");
    for(const auto& r:nescAir){
        const Air a=standardAtmosphere(r.altitude*ft);
        // Above 80 km the simulations report the molecular-scale temperature (no M/M0).
        if(r.altitude*ft<80000)near(a.temperature/(r.temperature*5/9),1,1e-5,"NESC temperature");
        near(a.pressure/(r.pressure*47.88025898033584),1,3e-5,"NESC pressure");near(a.density/(r.density*515.3788183931961),1,3e-5,"NESC density");near(a.sound/(r.sound*ft),1,1e-5,"NESC speed of sound");
    }
    // Two-body orbit: ten revolutions against Kepler's equation; energy.
    {
        Environment e;e.planet={earth.gm,6378137.,0,0,6378137.,{},0};e.rotation=Rotation::uniform(0);e.atmosphere=false;e.sunMoon=false;
        const double a=7e6,ecc=.01,n=std::sqrt(e.planet.gm/(a*a*a)),period=2*pi/n;
        State s;s.position={a*(1-ecc),0,0};s.velocity={0,std::sqrt(e.planet.gm/a*(1+ecc)/(1-ecc))*.8,std::sqrt(e.planet.gm/a*(1+ecc)/(1-ecc))*.6};
        Flight f(nescSphere(),e,s);const Tt step=second;const Tt end=static_cast<Tt>(std::llround(10*period*1e9));
        auto energy=[&]{return dot(f.state().velocity,f.state().velocity)/2-e.planet.gm/norm(f.state().position);};const double e0=energy();
        while(f.state().time<end)f.advance(std::min(step,end-f.state().time));
        near(energy()/e0,1,1e-10,"Two-body energy");
        const double m=n*secondsSinceJ2000(end);double E=m;for(int i=0;i<20;++i)E-=(E-ecc*std::sin(E)-m)/(1-ecc*std::cos(E));
        const double x=a*(std::cos(E)-ecc),y=a*std::sqrt(1-ecc*ecc)*std::sin(E);
        near(norm(f.state().position-V3{x,y*.8,y*.6}),0,1e-3,"Kepler position after ten orbits");
    }
    // J2 secular nodal rate (Vallado, mean elements to first order) from one-orbit averages
    // of the osculating elements, 13 orbits apart; ISS-like and sun-synchronous inclinations.
    for(const double inclination:{51.6*deg,98*deg}){
        Environment e;e.planet=earth;e.planet.egm2008=false;e.planet.degree=2;e.rotation=Rotation::uniform(0);e.atmosphere=false;e.sunMoon=false;
        const double mu=e.planet.gm,a0=7e6,ecc=.001,vp=std::sqrt(mu/a0*(1+ecc)/(1-ecc)),period=2*pi*std::sqrt(a0*a0*a0/mu);
        State s;s.position={a0*(1-ecc),0,0};s.velocity={0,vp*std::cos(inclination),vp*std::sin(inclination)};
        Flight f(nescSphere(),e,s);constexpr int per=600;const Tt dt=static_cast<Tt>(period/per*1e9);
        struct Mean {double a=0,e=0,i=0,node=0,t=0;};
        auto average=[&]{Mean m;double previous=0;
            for(int k=0;k<per;++k){
                const V3 r=f.state().position,v=f.state().velocity,h=cross(r,v),ev=cross(v,h)/mu-r/norm(r);double node=std::atan2(h.x,-h.y);
                if(k)node=previous+std::remainder(node-previous,2*pi);previous=node;
                m.a+=1/(2/norm(r)-dot(v,v)/mu)/per;m.e+=norm(ev)/per;m.i+=std::acos(h.z/norm(h))/per;m.node+=node/per;m.t+=secondsSinceJ2000(f.state().time)/per;f.advance(dt);
            }
            return m;};
        const Mean m0=average();for(int k=0;k<per*13;++k)f.advance(dt);const Mean m1=average();
        const double a=(m0.a+m1.a)/2,ec=(m0.e+m1.e)/2,c=std::cos((m0.i+m1.i)/2),p=a*(1-ec*ec),k=e.planet.zonal[2]*std::pow(e.planet.gravityRadius/p,2);
        const double n=std::sqrt(mu/(a*a*a))*(1+.75*k*std::sqrt(1-ec*ec)*(3*c*c-1)),theory=-1.5*n*k*c,measured=(m1.node-m0.node)/(m1.t-m0.t);
        std::printf("J2 nodal rate at %.1f deg: %.5f deg/day (theory %.5f)\n",inclination/deg,measured/deg*86400,theory/deg*86400);
        near(measured/theory,1,1e-3,"J2 secular nodal rate");
    }
    // NESC spheres: 1 dragless (J2, rotating), 4 round non-rotating, 6 ellipsoidal, 9-10 cannonballs.
    for(int c:{1,4,6,9,10}){
        const Environment e=nesc(c==4,c!=4,c!=1);const Vehicle v=nescSphere();
        const V3 ned=c==9?V3{0,1000*ft,-1000*ft}:c==10?V3{1000*ft,0,-1000*ft}:V3{};
        Flight f(v,e,Flight::free(e,v,0,{0,0,c<9?30000*ft:0},ned,{}));
        for(int t=0;t<=30;t+=10){
            if(t)for(int i=0;i<10000;++i)f.advance(second/1000);
            double worst=0,worstV=0,best=1e9,bestV=1e9,spread=0,spreadV=0;const NescPoint* first=nullptr;
            for(const auto& p:nescSpheres)if(p.nescCase==c&&p.t==t){
                const V3 r{p.x*ft,p.y*ft,p.z*ft},vv{p.vx*ft,p.vy*ft,p.vz*ft};
                if(std::getenv("NESC_DEBUG"))std::printf("  sim %d: %.4f m\n",p.sim,norm(f.state().position-r));
                worst=std::max(worst,norm(f.state().position-r));worstV=std::max(worstV,norm(f.state().velocity-vv));
                best=std::min(best,norm(f.state().position-r));bestV=std::min(bestV,norm(f.state().velocity-vv));
                if(first){spread=std::max(spread,norm(r-V3{first->x*ft,first->y*ft,first->z*ft}));spreadV=std::max(spreadV,norm(vv-V3{first->vx*ft,first->vy*ft,first->vz*ft}));}else first=&p;
            }
            std::printf("case %02d t=%2d position %.4f m velocity %.5f m/s (simulations differ %.4f m, %.5f m/s)\n",c,t,worst,worstV,spread,spreadV);
            // Within 1 cm and 1 mm/s of the closest reference, and of the other within their spread.
            near(best,0,.01,"NESC sphere position");near(bestV,0,1e-3,"NESC sphere velocity");
            near(worst,0,spread+.01,"NESC sphere spread");near(worstV,0,spreadV+1e-3,"NESC sphere velocity spread");
        }
    }
    // NESC 17: two-stage rocket to orbit against sims 4 and 5 (US 1976 below 86 km only).
    {
        std::vector<State> trace;const Flight f=rocket(second/1000,200,&trace);
        for(const auto& s:trace){
            const double t=std::round(secondsSinceJ2000(s.time));double d[2]{},dv[2]{},spread=0;V3 other;int n=0;
            for(const auto& p:nescRocket)if(p.t==t){const V3 r{p.x*ft,p.y*ft,p.z*ft};
                d[n]=norm(s.position-r);dv[n]=norm(s.velocity-V3{p.vx*ft,p.vy*ft,p.vz*ft});if(n)spread=norm(r-other);other=r;++n;}
            std::printf("case 17 t=%3.0f sim 4: %.3f m %.4f m/s, sim 5: %.3f m %.4f m/s (simulations differ %.3f m) stage %d\n",t,d[0],dv[0],d[1],dv[1],spread,s.stage);
            // Inside the disagreement of the two references (6.6 m after 250 km of ascent).
            near(std::min(d[0],d[1]),0,spread+.5,"NESC rocket position");near(std::max(d[0],d[1]),0,2*spread+1,"NESC rocket position spread");
            near(std::min(dv[0],dv[1]),0,.3,"NESC rocket velocity");
        }
        require(f.state().stage==1&&!f.state().burning&&f.state().propellant==0,"Both stages burned out");
        const Flight half=rocket(second/2000,200);const double moved=norm(half.state().position-f.state().position);
        std::printf("case 17 halving the step moves the final position %.4f m\n",moved);near(moved,0,1,"Step convergence");
        require(rocket(second/1000,200).hash()==rocket(second/1000,200).hash(),"Deterministic replay"); // Same step sequence.
    }
    // Time scales and Earth orientation.
    require(ttFromUtc(1483228800.)==static_cast<Tt>(536500869184000000),"TT at 2017-01-01 (ΔAT 37 s)");
    near(utcFromTt(ttFromUtc(1792238400.25)),1792238400.25,1e-6,"UTC round trip");
    {
        const Rotation r=Rotation::iau(69.184);const Tt t=ttFromUtc(1792238400.)+12345678901;const Orientation o=r.at(t);
        const double days=secondsSinceJ2000(t)/86400.,ut1=days-69.184/86400.;double m[3][3];eraC2t06a(2451545.0,days,2451545.0,ut1,0,0,m);
        double worst=0;for(int i=0;i<3;++i)for(int j=0;j<3;++j)worst=std::max(worst,std::abs(o.fixed.m[i][j]-m[i][j]));
        near(worst,0,2e-10,"IAU 2006/2000A orientation");near(norm(o.omega),7.292115146706979e-5,1e-15,"Earth rotation rate");
        const Perturbers p=perturbers(t);near(norm(p.moon),3.8e8,3e7,"Moon distance");near(norm(p.sun),1.496e11,3e9,"Sun distance");
    }
    // Launch pad: clamped until thrust exceeds weight; a dropped sphere comes to rest on the ground.
    {
        Environment e;e.ground=0;const Vehicle v=genericLauncher();
        Flight f(v,e,Flight::pad(e,v,ttFromUtc(1792238400.),{28.6082*deg,-80.6041*deg,0},90*deg,90*deg));
        Sample x=f.sample();near(x.specificForce.x,9.79,.02,"Specific force on the pad");
        near(x.geodetic.height,v.stages[0].length-v.stages[0].cgFull,1e-6,"Base of the stack on the ground");near(norm(x.fixedVelocity),0,1e-9,"Pad at rest");
        f.advance(second);require(f.state().clamped&&norm(f.sample().fixedVelocity)<1e-9,"Held without thrust");
        f.ignite();require(f.advance(second/1000)==0&&!f.state().clamped,"Release ends the call");for(Tt t=0;t<10*second;)t+=f.advance(second/1000);
        x=f.sample();require(!f.state().clamped&&!f.state().landed,"Released at ignition");
        std::printf("generic launcher after 10 s: %.1f m, %.2f m/s, %.0f kg, specific force %.2f m/s²\n",x.geodetic.height,-x.nedVelocity.z,x.mass,x.specificForce.x);
        near(x.geodetic.height-(v.stages[0].length-v.stages[0].cgFull),250,60,"Liftoff altitude");near(x.specificForce.x,x.thrust/x.mass+x.aeroForce.x/x.mass,1e-9,"Accelerometer");
        Environment d=nesc(false,true,false);d.ground=0;const Vehicle sphere=nescSphere();
        Flight drop(sphere,d,Flight::free(d,sphere,0,{0,0,100},{},{}));Tt t=0;while(t<10*second)t+=drop.advance(10*second-t);
        require(drop.state().landed&&std::abs(drop.sample().geodetic.height)<1e-6&&norm(drop.sample().fixedVelocity)<1e-9,"Touchdown");
    }
    // Depletion exactly at the end of a step burns all propellant within it; burning in
    // free space with the complete model: d(Iω)/dt = -ṁ l² ω, so with constant l and
    // linear inertia Iω = H0 (I/I0)^(ṁ l²/|İ|).
    {
        Environment e;e.planet={0,6378137.,0,0,6378137.,{},0};e.rotation=Rotation::uniform(0);e.atmosphere=false;e.sunMoon=false;
        Stage st;st.name="test";st.propellant=100;st.massFull=200;st.massEmpty=100;st.cgFull=st.cgEmpty=5;st.inertiaFull={100,2000,2000};st.inertiaEmpty={50,1000,1000};
        st.thrust=1e4;st.exhaust=1e3;st.nozzle=9;st.length=10;st.aero.lift=st.aero.drag=st.aero.side=st.aero.roll=st.aero.pitch=st.aero.yaw={{0},{0},{0}};
        const Vehicle v{"test",{st},0};auto run=[&](Tt step,double rate){State s;s.position={1e8,0,0};s.rate={0,rate,0};s.propellant=st.propellant;
            Flight f(v,e,s);f.ignite();Tt t=0;while(t<10*second)t+=f.advance(std::min(step,10*second-t));return f;};
        const Flight a=run(10*second,0),b=run(second/100,0); // ṁ 10 kg/s: exactly 10 s of propellant.
        require(a.state().propellant==0&&!a.state().burning&&b.state().propellant==0&&!b.state().burning,"Depleted at the step boundary");
        std::printf("rocket equation: one 10 s step %.6f, 10 ms steps %.9f, exact %.9f m/s\n",norm(a.state().velocity),norm(b.state().velocity),1e3*std::log(2.));
        near(norm(a.state().velocity),1e3*std::log(2.),2,"Rocket equation, one RK4 step"); // Simpson error 1.3 m/s; thrust lost in k4 would cost ~140.near(norm(b.state().velocity),1e3*std::log(2.),1e-7,"Rocket equation");
        const Flight c=run(second/1000,.1);const double h0=2000*.1,i1=1000,expected=h0*std::pow(i1/2000.,10*16./100); // ṁ l²/|İ| = 1.6
        near(i1*c.state().rate.y,expected,1e-9*h0,"Jet damping and inertia loss");
    }
    // Coast: from a 300 km circular orbit, one revolution with 1 s steps above the atmosphere
    // against 10 ms steps.
    {
        Environment e;const double r=6678137.,v=std::sqrt(earth.gm/r);State s;s.time=ttFromUtc(1792238400.);s.position={r,0,0};s.velocity={0,v*.6,v*.8};s.propellant=0;
        const Vehicle l=genericLauncher();Flight fine(l,e,s),coarse(l,e,s);fine.steps(second/100);coarse.steps(second/1000,second);
        auto run=[](Flight& f,Tt span){std::size_t n=0;for(Tt t=0;t<span;++n)t+=f.advance(span-t);return n;};
        const auto t0=std::chrono::steady_clock::now();run(fine,5400*second);const auto t1=std::chrono::steady_clock::now();run(coarse,5400*second);const auto t2=std::chrono::steady_clock::now();
        const double gap=norm(fine.state().position-coarse.state().position);
        std::printf("coast: 1 s steps differ %.4f m from 10 ms after one orbit, %.0fx faster\n",gap,std::chrono::duration<double>(t1-t0).count()/std::chrono::duration<double>(t2-t1).count());
        near(gap,0,.01,"Coast steps");
    }
    // The example flight computer (examples/gravity_turn.hpp) flies the generic launcher to orbit at 50 Hz.
    {
        namespace ex=albert::example;auto vec=[](V3 v){return ex::Vec{v.x,v.y,v.z};};
        Environment e;e.ground=0;const Vehicle v=genericLauncher();
        Flight f(v,e,Flight::pad(e,v,ttFromUtc(1792238400.),{28.6082*deg,-80.6041*deg,0},90*deg,90*deg));
        ex::GravityTurn g(90*deg);const Tt t0=f.state().time;double maxq=0;
        while(g.phase()!=ex::GravityTurn::Phase::orbit&&f.state().time-t0<900*second&&!f.state().landed){
            const Sample s=f.sample();ex::Reading r;r.elapsed=secondsSinceJ2000(f.state().time-t0);r.height=s.geodetic.height;r.nedVelocity=vec(s.nedVelocity);
            r.qw=s.nedAttitude.w;r.qx=s.nedAttitude.x;r.qy=s.nedAttitude.y;r.qz=s.nedAttitude.z;r.rate=vec(f.state().rate);r.specificForce=vec(s.specificForce);
            r.position=vec(f.state().position);r.velocity=vec(f.state().velocity);r.stage=f.state().stage;r.burning=f.state().burning;r.clamped=f.state().clamped;r.propellant=f.state().propellant;
            const auto c=g.step(r);if(c.ignite)f.ignite();if(c.separate)f.separate();if(c.cutoff)f.cutoff();f.gimbal(c.gimbalPitch,c.gimbalYaw);f.torque({c.torque.x,c.torque.y,c.torque.z});
            maxq=std::max(maxq,s.dynamicPressure);
            for(Tt left=20*second/1000;left>0;)left-=f.advance(left);
        }
        const double perigee=ex::GravityTurn::perigee(vec(f.state().position),vec(f.state().velocity)),r=norm(f.state().position),v2=dot(f.state().velocity,f.state().velocity);
        const double a=1/(2/r-v2/3.986004418e14),apogee=2*a-perigee-2*6378137.;
        std::printf("example guidance: orbit %.0f x %.0f km after %.0f s, %.0f kg left, max q %.0f Pa\n",perigee/1e3,apogee/1e3,secondsSinceJ2000(f.state().time-t0),f.state().propellant,maxq);
        require(g.phase()==ex::GravityTurn::Phase::orbit&&perigee>=180e3&&apogee<300e3&&f.state().propellant>0&&maxq<6e4,"Example guidance reaches orbit");
    }
    std::cout<<"physics_test: ok\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
