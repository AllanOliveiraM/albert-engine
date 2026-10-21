#include "physics/vehicles.hpp"
#include "physics/msis.hpp"
#include "physics/sensors.hpp"
#include "gravity_turn.hpp"
#include <erfa.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
    e.rotation=Rotation::uniform(rotating?7.292115e-5:0);e.atmosphere=drag;e.sunMoon=false;e.relativity=false;return e;
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
    // NRLMSISE-00: the 17 reference cases of the NRL distribution (He, O, N2, O2, Ar in cm⁻³,
    // mass density g/cm³, H, N, anomalous O, exospheric and local temperature).
    {
        constexpr double reference[17][11]{
        {6.665177E+05,1.138806E+08,1.998211E+07,4.022764E+05,3.557465E+03,4.074714E-15,3.475312E+04,4.095913E+06,2.667273E+04,1.250540E+03,1.241416E+03},
        {3.407293E+06,1.586333E+08,1.391117E+07,3.262560E+05,1.559618E+03,5.001846E-15,4.854208E+04,4.380967E+06,6.956682E+03,1.166754E+03,1.161710E+03},
        {1.123767E+05,6.934130E+04,4.247105E+01,1.322750E-01,2.618848E-05,2.756772E-18,2.016750E+04,5.741256E+03,2.374394E+04,1.239892E+03,1.239891E+03},
        {5.411554E+07,1.918893E+11,6.115826E+12,1.225201E+12,6.023212E+10,3.584426E-10,1.059880E+07,2.615737E+05,2.819879E-42,1.027318E+03,2.068878E+02},
        {1.851122E+06,1.476555E+08,1.579356E+07,2.633795E+05,1.588781E+03,4.809630E-15,5.816167E+04,5.478984E+06,1.264446E+03,1.212396E+03,1.208135E+03},
        {8.673095E+05,1.278862E+08,1.822577E+07,2.922214E+05,2.402962E+03,4.355866E-15,3.686389E+04,3.897276E+06,2.667273E+04,1.220146E+03,1.212712E+03},
        {5.776251E+05,6.979139E+07,1.236814E+07,2.492868E+05,1.405739E+03,2.470651E-15,5.291986E+04,1.069814E+06,2.667273E+04,1.116385E+03,1.112999E+03},
        {3.740304E+05,4.782720E+07,5.240380E+06,1.759875E+05,5.501649E+02,1.571889E-15,8.896776E+04,1.979741E+06,9.121815E+03,1.031247E+03,1.024848E+03},
        {6.748339E+05,1.245315E+08,2.369010E+07,4.911583E+05,4.578781E+03,4.564420E-15,3.244595E+04,5.370833E+06,2.667273E+04,1.306052E+03,1.293374E+03},
        {5.528601E+05,1.198041E+08,3.495798E+07,9.339618E+05,1.096255E+04,4.974543E-15,2.686428E+04,4.889974E+06,2.805445E+04,1.361868E+03,1.347389E+03},
        {1.375488E+14,0.000000E+00,2.049687E+19,5.498695E+18,2.451733E+17,1.261066E-03,0.000000E+00,0.000000E+00,0.000000E+00,1.027318E+03,2.814648E+02},
        {4.427443E+13,0.000000E+00,6.597567E+18,1.769929E+18,7.891680E+16,4.059139E-04,0.000000E+00,0.000000E+00,0.000000E+00,1.027318E+03,2.274180E+02},
        {2.127829E+12,0.000000E+00,3.170791E+17,8.506280E+16,3.792741E+15,1.950822E-05,0.000000E+00,0.000000E+00,0.000000E+00,1.027318E+03,2.374389E+02},
        {1.412184E+11,0.000000E+00,2.104370E+16,5.645392E+15,2.517142E+14,1.294709E-06,0.000000E+00,0.000000E+00,0.000000E+00,1.027318E+03,2.795551E+02},
        {1.254884E+10,0.000000E+00,1.874533E+15,4.923051E+14,2.239685E+13,1.147668E-07,0.000000E+00,0.000000E+00,0.000000E+00,1.027318E+03,2.190732E+02},
        {5.196477E+05,1.274494E+08,4.850450E+07,1.720838E+06,2.354487E+04,5.881940E-15,2.500078E+04,6.279210E+06,2.667273E+04,1.426412E+03,1.408608E+03},
        {4.260860E+07,1.241342E+11,4.929562E+12,1.048407E+12,4.993465E+10,2.914304E-10,8.831229E+06,2.252516E+05,2.415246E-42,1.027318E+03,1.934071E+02}};
        for(int i=0;i<17;++i){
            int doy=172;double sec=29000,alt=400,lat=60,lon=-70,lst=16;SpaceWeather w;
            if(i==1)doy=81;if(i==2){sec=75000;alt=1000;}if(i==3||i==16)alt=100;if(i>=10&&i<=14)alt=std::array{0.,10.,30.,50.,70.}[static_cast<std::size_t>(i-10)];
            if(i==4)lat=0;if(i==5)lon=0;if(i==6)lst=4;if(i==7)w.f107a=70;if(i==8)w.f107=180;if(i==9)w.ap[0]=40;if(i==15||i==16){w.history=true;w.ap.fill(100);}
            const auto o=nrlmsise00(doy,sec,alt,lat,lon,lst,w,false);
            for(int k=0;k<11;++k){const double v=k<9?(k==5?o.density[5]/1000:o.density[static_cast<std::size_t>(k)]/1e6):k==9?o.exospheric:o.temperature;
                near(v,reference[i][k],std::abs(reference[i][k])*5e-7+1e-300,"NRLMSISE-00 reference case");}
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
        Environment e;e.planet={earth.gm,6378137.,0,0,6378137.,{},0};e.rotation=Rotation::uniform(0);e.atmosphere=false;e.sunMoon=false;e.relativity=false;
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
    // NESC spheres: 1 dragless (J2, rotating), 4 round non-rotating, 6 ellipsoidal, 7 steady
    // wind (20 ft/s from the west), 8 shear (0.003 h - 20 ft/s from the west), 9-10 cannonballs.
    struct Wind final:AirModel {
        bool shear;explicit Wind(bool s):shear(s){}
        Air at(std::int64_t,double,double,double h)const override{Air a=standardAtmosphere(h);a.wind={0,shear?(.003*h/ft-20)*ft:20*ft,0};return a;}
        std::string source()const override{return "NESC wind";}
    };
    for(int c:{1,4,6,7,8,9,10}){
        Environment e=nesc(c==4,c!=4,c!=1);const Vehicle v=nescSphere();if(c==7||c==8)e.air=std::make_shared<Wind>(c==8);
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
        Flight c=run(second/1000,.1);const double h0=2000*.1,i1=1000,expected=h0*std::pow(i1/2000.,10*16./100); // ṁ l²/|İ| = 1.6
        near(i1*c.state().rate.y,expected,1e-9*h0,"Jet damping and inertia loss");
        // IMU increments: the whole burn's specific-force integral (no gravity) and the angle
        // swept by the damped rate, ∫ω dt = H0/(1.6 |İ| I0^1.6) (I0^1.6 - I1^1.6).
        const auto inc=c.increments();near(inc.seconds,10,1e-12,"Increment span");near(inc.velocity.x,1e3*std::log(2.),1e-7,"Velocity increment");
        near(inc.angle.y,h0/(1.6*100*std::pow(2000.,1.6))*(std::pow(2000.,1.6)-std::pow(1000.,1.6)),1e-9,"Angle increment");
        require(c.increments().seconds==0,"Increments reset when read");
    }
    // Engines one by one, in free space: the one-engine shorthand is that engine; a three-engine cluster with its +y engine out
    // turns the vehicle about z by T·y (cross product of the lever and the thrust) and draws two engines' flow; gimbals reach
    // their command at their actuators' rate; a sideways engine at the centre of mass pushes without turning.
    {
        Environment e;e.planet={0,6378137.,0,0,6378137.,{},0};e.rotation=Rotation::uniform(0);e.atmosphere=false;e.sunMoon=false;e.relativity=false;
        Stage st;st.name="cluster";st.propellant=500;st.massFull=1500;st.massEmpty=1000;st.cgFull=st.cgEmpty=5;st.inertiaFull=st.inertiaEmpty={300,4000,4000};st.length=10;
        st.aero.lift=st.aero.drag=st.aero.side=st.aero.roll=st.aero.pitch=st.aero.yaw={{0},{0},{0}};
        State s;s.position={1e8,0,0};s.propellant=st.propellant;
        Stage shorthand=st;shorthand.thrust=1e4;shorthand.exhaust=3e3;shorthand.nozzle=9;shorthand.gimbal=.1;
        Stage explicit_=st;explicit_.engines={{"cluster",{9,0,0},{1,0,0},1e4,3e3,0,1,.1,0}};
        Flight a(Vehicle{"a",{shorthand},0},e,s),b(Vehicle{"a",{explicit_},0},e,s);
        for(Flight* f:{&a,&b}){f->ignite();f->gimbal(.05,-.02);Tt t=0;while(t<2*second)t+=f->advance(std::min(second/1000,2*second-t));}
        require(a.hash()==b.hash(),"The one-engine shorthand is that engine");
        Stage cluster=st;for(double y:{-1.,0.,1.})cluster.engines.push_back({"e",{9,y,0},{1,0,0},1e4,3e3,0,.4,.2,.1});
        Flight c(Vehicle{"c",{cluster},0},e,s);c.ignite();c.fail(2);
        bool refused=false;try{c.ignite(2);}catch(const std::logic_error&){refused=true;}require(refused,"A failed engine stays off");
        refused=false;try{c.ignite(3);}catch(const std::invalid_argument&){refused=true;}require(refused,"No fourth engine");
        near(c.massFlow(),2e4/3e3,1e-12,"Flow of the two engines left");near(c.sample().thrustForce.x,2e4,1e-9,"Thrust of the two engines left");
        c.advance(second/1000);const double jet=c.massFlow()*16; // With jet damping: I dω/dt = T y - Σṁl² ω.
        near(c.state().rate.z,1e4/jet*(1-std::exp(-jet*1e-3/4000)),1e-14,"Engine out: yaw rate from T y / I");near(c.state().rate.y,0,1e-15,"No pitch");
        c.gimbal(.15,0,0);for(int i=0;i<1000;++i)c.advance(second/1000);
        near(c.engines()[0].pitch,.1,1e-12,"Gimbal at its rate (0.1 rad/s)");near(c.engines()[1].pitch,0,0,"Only the commanded engine moves");
        for(int i=0;i<1000;++i)c.advance(second/1000);near(c.engines()[0].pitch,.15,1e-12,"Gimbal at its command");
        c.throttle(.5,1);near(c.massFlow(),1.5e4/3e3,1e-12,"Throttle one engine");c.throttle(.1);near(c.engines()[0].throttle,.4,0,"Clamped to its deepest throttle");
        Stage side=st;side.engines={{"side",{5,0,0},{0,2,0},1e3,3e3,0,1,0,0}};Flight d(Vehicle{"d",{side},0},e,s);d.ignite();
        near(d.sample().specificForce.y,1e3/1500,1e-12,"A sideways engine pushes along its direction");d.advance(second/100);near(norm(d.state().rate),0,0,"through the centre of mass, without turning");
    }
    // Reaction control: four 100 N thrusters, a pair at each end pushing ±z, 4 m from the centre of mass: a pitch torque
    // command is met by a couple (no net force) on the least propellant, saturates at their 800 N m, and burns T u / ve.
    // A control surface adds its increments: ΔCm 0.01 a degree at 5° gives q S L 0.05; its deflection moves at its rate.
    {
        Environment e;e.planet={0,6378137.,0,0,6378137.,{},0};e.rotation=Rotation::uniform(0);e.atmosphere=false;e.sunMoon=false;e.relativity=false;
        Stage st;st.name="rcs";st.propellant=100;st.massFull=1100;st.massEmpty=1000;st.cgFull=st.cgEmpty=5;st.inertiaFull=st.inertiaEmpty={300,4000,4000};st.length=10;
        st.aero.lift=st.aero.drag=st.aero.side=st.aero.roll=st.aero.pitch=st.aero.yaw={{0},{0},{0}};
        for(const double x:{1.,9.})for(const double z:{1.,-1.})st.rcs.push_back({"q",{x,0,0},{0,0,z},100,700});
        State s;s.position={1e8,0,0};s.propellant=st.propellant;Flight f(Vehicle{"rcs",{st},0},e,s);
        f.torque({0,400,0});Sample x=f.sample();
        std::printf("RCS for 400 N m pitch: duties %.4f %.4f %.4f %.4f, moment %.6f N m, force %.2e N\n",f.thrusters()[0],f.thrusters()[1],f.thrusters()[2],f.thrusters()[3],x.rcsMoment.y,norm(x.rcsForce));
        near(x.rcsMoment.y,400,1e-4,"Thrusters meet the torque");near(norm(x.rcsForce),0,1e-4,"as a couple");near(f.massFlow(),100./700,1e-8,"on the least propellant");
        f.torque({0,2000,0});near(f.sample().rcsMoment.y,800,1e-4,"Torque saturates at the thrusters' reach");
        f.thruster(0);require(f.thrusters()[0]==0&&f.thrusters()[1]==0,"Thrusters set directly");f.thruster(1,2);near(f.sample().rcsForce.z,100,1e-12,"One thruster's force");
        { // Two arms on the same side, 4 m and 1 m (review regression: the active set stalled at a bound): the long one alone.
            Stage lever=st;lever.rcs={{"long",{1,0,0},{0,0,-1},100,700},{"short",{4,0,0},{0,0,-1},100,700},{"long+",{1,0,0},{0,0,1},100,700}};
            Flight g(Vehicle{"lever",{lever},0},e,s);g.torque({0,200,0});near(g.sample().rcsMoment.y,200,1e-4,"Torque from the long arm");near(g.thrusters()[0],.5,1e-6,"The long arm alone");near(g.thrusters()[1],0,0,"not the short");
        }
        { // The propellant as tanks hold it: the thrusters on their own gas (the propellant untouched, silent once it is gone);
          // 1% boiled off a day; an engine's start costs its 10 kg; the engine starves at the 20 kg of residuals.
            Stage gas=st;gas.gas=1;Flight g(Vehicle{"gas",{gas},0},e,s);g.thruster(1,1);g.advance(second/2);
            near(g.state().gas,1-100./700*.5,1e-9,"Thrusters burn their own gas");near(g.state().propellant,100,1e-9,"and not the propellant");
            for(int i=0;i<20;++i)g.advance(second/2);near(g.state().gas,0,0,"Gas spent");near(norm(g.sample().rcsForce),0,0,"then no thrust");
            Stage tank=st;tank.rcs.clear();tank.boiloff=.01;tank.residual=20;tank.engines={Engine{"e",{10,0,0},{1,0,0},2e4,2000,0,1,0,0,10}};
            State far=s;far.position={1e9,0,0};Flight boil(Vehicle{"boil",{tank},0},e,far);boil.steps(second/100,second*60);
            for(int i=0;i<1440;++i)boil.advance(second*60);near(boil.state().propellant,100*std::exp(-.01),1e-6,"1% a day boiled off");
            Flight burn(Vehicle{"burn",{tank},0},e,far);burn.ignite();near(burn.state().propellant,90,1e-12,"A start costs its propellant");
            for(int i=0;i<200&&burn.state().burning;++i)burn.advance(second/10);near(burn.state().propellant,20,1e-6,"Starves at the residuals");near(burn.state().burning?1:0,0,0,"out then");
        }
        Environment air;air.sunMoon=false;air.relativity=false;
        Stage w=st;w.rcs.clear();w.aero.area=2;w.aero.length=3;Surface flap;flap.name="flap";flap.limit=.3;flap.rate=10*3.14159265358979/180;
        flap.pitch={{0},{0},{-10,10},{-.1,.1}};w.surfaces={flap};
        const Vehicle wv{"winged",{w},0};Flight g(wv,air,Flight::free(air,wv,0,{0,0,1e4},{300,0,0},{}));g.surface(5*3.14159265358979/180);
        for(int i=0;i<250;++i)g.advance(second/1000);near(g.surfaces()[0].deflection*180/3.14159265358979,2.5,1e-9,"Surface at its rate (10°/s)");
        g.advance(second/4);x=g.sample();near(g.surfaces()[0].deflection*180/3.14159265358979,5,1e-9,"Surface at its command");
        near(x.aeroMoment.y,x.dynamicPressure*2*3*.05,1e-9*x.dynamicPressure,"Surface pitching moment q S L ΔCm");
        // A placed surface's drag acts where it is: 1.5 m right of the axis, it yaws the vehicle by 1.5 m × q S ΔCD (none at
        // the reference, nose-on), the force the same.
        Surface brake;brake.name="brake";brake.drag={{0},{0},{0},{.5}};Stage at=w,placed=w;at.surfaces={brake};brake.position={2,1.5,0};brake.placed=true;placed.surfaces={brake};
        const Vehicle av{"braked",{at},0},pv{"braked aside",{placed},0};
        const Sample y0=Flight(av,air,Flight::free(air,av,0,{0,0,1e4},{300,0,0},{})).sample(),y1=Flight(pv,air,Flight::free(air,pv,0,{0,0,1e4},{300,0,0},{})).sample();
        near(y1.aeroMoment.z-y0.aeroMoment.z,1.5*y1.dynamicPressure*2*.5,1e-9*y1.dynamicPressure,"Placed surface's drag yaws from where it is");near(norm(y1.aeroForce-y0.aeroForce),0,1e-9*y1.dynamicPressure,"with the same force");
        // A fin 1.5 m below the axis whose normal force (q S 0.4) pushes along body y: that force, and a roll of -1.5 m times it.
        Surface fin;fin.name="fin";fin.lift={{0},{0},{0},{.4}};fin.position={2,0,1.5};fin.placed=true;fin.normal={0,1,0};fin.normalSet=true;
        Stage finned=w;finned.surfaces={fin};const Vehicle fv{"finned",{finned},0};
        const Sample y2=Flight(fv,air,Flight::free(air,fv,0,{0,0,1e4},{300,0,0},{})).sample();const double force=y2.dynamicPressure*2*.4;
        near(y2.aeroForce.y-y0.aeroForce.y,force,1e-9*force,"A fin's normal force along its normal");near(y2.aeroMoment.x-y0.aeroMoment.x,-1.5*force,1e-9*force,"rolling from where it is");
    }
    // Navigation sensors against their settings: the GNSS's noise per axis and its limits; the radar altimeter's slant range
    // and beam edge; the star tracker's noise across and about its boresight, and its exclusions.
    {
        GnssModel gm;gm.drift=0;Gnss gnss(gm,7);const M3 ned=nedFromFixed(.5,-1.4);const V3 p=fixedPosition(earth,{.5,-1.4,100}),v{10,20,30};
        double sn=0,sd=0,sv=0;constexpr int n=4000;
        for(int i=0;i<n;++i){const auto r=gnss.measure(i*.1,.1,p,v,ned,100,50,true);require(r.valid,"GNSS fix");const V3 e=ned*(r.position-p);sn+=e.x*e.x;sd+=e.z*e.z;sv+=dot(r.velocity-v,r.velocity-v)/3;}
        std::printf("GNSS sigma: north %.3f m, down %.3f m, velocity %.4f m/s\n",std::sqrt(sn/n),std::sqrt(sd/n),std::sqrt(sv/n));
        near(std::sqrt(sn/n),1.5,.08,"GNSS horizontal sigma");near(std::sqrt(sd/n),3,.15,"GNSS vertical sigma");near(std::sqrt(sv/n),.05,.0025,"GNSS velocity sigma");
        GnssModel civil=gm;civil.maxHeight=18000;civil.maxSpeed=515;Gnss limited(civil,1);
        require(!limited.measure(0,.1,p,v,ned,20000,100,true).valid&&!limited.measure(0,.1,p,v,ned,1000,600,true).valid&&limited.measure(0,.1,p,v,ned,1000,100,true).valid,"GNSS export limits");
        require(!limited.measure(0,.1,p,v,ned,1000,100,false).valid,"No GNSS off the Earth");
        RadarAltimeter radar({},3);double sum=0,sq=0;
        for(int i=0;i<n;++i){const auto r=radar.measure(i,100,{0,0,1});sum+=r.range;sq+=r.range*r.range;}
        const double mean=sum/n,sigma=std::sqrt(sq/n-mean*mean);std::printf("radar at 100 m: mean %.3f m, sigma %.3f m\n",mean,sigma);
        near(mean,100,.06,"Radar range");near(sigma,1,.05,"Radar noise (1% of the range)");
        const double t=10*3.14159265358979/180;double tilted=0;for(int i=0;i<n;++i)tilted+=radar.measure(i,100,{std::sin(t),0,std::cos(t)}).range;
        near(tilted/n,100/std::cos(t),.06,"Slant range at 10 degrees");require(!radar.measure(0,100,{.5,0,std::sqrt(.75)}).valid&&!radar.measure(0,3000,{0,0,1}).valid,"Beam edge and range limit");
        StarTracker tracker({},5);const Q q=normalized({.9,.1,.3,.2});double across=0,roll=0;const V3 sun{1,0,0},body{-1,0,0},look=matrix(q)*V3{0,0,-1};
        const V3 away=cross(look,V3{0,1,0})/norm(cross(look,V3{0,1,0})); // Sun and body well off the boresight.
        for(int i=0;i<n;++i){const auto r=tracker.measure(i,q,away,away*-1.,.1,{});require(r.valid,"Star tracker fix");
            const Q d=Q{q.w,-q.x,-q.y,-q.z}*r.attitude;const V3 e{2*d.x,2*d.y,2*d.z}; // Body-axis error.
            roll+=e.z*e.z;across+=(e.x*e.x+e.y*e.y)/2;}
        std::printf("star tracker sigma: across %.2f arcsec, roll %.2f arcsec\n",std::sqrt(across/n)*206265,std::sqrt(roll/n)*206265);
        near(std::sqrt(across/n),2.4e-5,1.2e-6,"Star tracker sigma across the boresight");near(std::sqrt(roll/n),2e-4,1e-5,"Star tracker roll sigma");
        require(!tracker.measure(0,q,look,away,.1,{}).valid&&!tracker.measure(0,q,away,look,.1,{}).valid&&!tracker.measure(0,q,away,away*-1.,.1,{0,0,.1}).valid,"Sun, body and rate exclusions");
        (void)sun;(void)body;
    }
    // Dryden turbulence at 5 km (L 533 m, σ 2 m/s) and 100 m/s: each gust's variance σ², u's autocorrelation e⁻¹ after L/V; at
    // 100 ft the MIL-F-8785C low-altitude scales.
    {
        Turbulence t(2,11);constexpr double dt=.02;constexpr int n=150000;std::vector<double> u(n);double su=0,sv=0,sw=0;
        for(int i=0;i<n;++i){const V3 g=t.step(dt,100,5000);u[static_cast<std::size_t>(i)]=g.x;su+=g.x*g.x;sv+=g.y*g.y;sw+=g.z*g.z;}
        const std::size_t lag=static_cast<std::size_t>(std::lround(533.4/100/dt));double c=0;for(std::size_t i=lag;i<u.size();++i)c+=u[i]*u[i-lag];
        const double ru=c/static_cast<double>(u.size()-lag)/(su/n);
        std::printf("Dryden at 5 km: sigma u %.3f, v %.3f, w %.3f m/s; u autocorrelation at L/V %.3f\n",std::sqrt(su/n),std::sqrt(sv/n),std::sqrt(sw/n),ru);
        near(std::sqrt(su/n),2,.2,"Dryden u intensity");near(std::sqrt(sv/n),2,.2,"Dryden v intensity");near(std::sqrt(sw/n),2,.2,"Dryden w intensity");near(ru,std::exp(-1.),.06,"Dryden u correlation");
        double lu,lw,s1,s2;Turbulence::scales(100*.3048,1,lu,lw,s1,s2);const double k=.177+.000823*100;
        near(lw,100*.3048,1e-9,"Low-altitude L_w = h");near(lu,100/std::pow(k,1.2)*.3048,1e-9,"Low-altitude L_u");near(s1,1/std::pow(k,.4),1e-12,"Low-altitude sigma_u");
    }
    // Wheels and a jet: a 10 t vehicle on three wheels (struts 2 m below the centre of mass) rolling at 20 m/s loses
    // μ_r g a second, stops at μ_b g on full brakes, turns with its nose wheel steered; a 50 kN jet spools up with its time
    // constant (63% at τ), burns TSFC × T, and gives nothing without air.
    {
        Environment e;e.sunMoon=false;e.relativity=false;e.ground=0;const Tt t0=ttFromUtc(1792238400.);
        Stage st;st.name="taxi";st.propellant=1000;st.massFull=10000;st.massEmpty=9000;st.cgFull=st.cgEmpty=10;st.inertiaFull=st.inertiaEmpty={2e4,1e5,1e5};st.length=20;
        st.aero.lift=st.aero.drag=st.aero.side=st.aero.roll=st.aero.pitch=st.aero.yaw={{0},{0},{0}};
        for(const auto& p:{V3{3,0,2},V3{12,-2,2},V3{12,2,2}}){Wheel w;w.name="w";w.position=p;w.stiffness=1e6;w.damping=6e4;w.steer=p.x<5?.5:0;st.wheels.push_back(w);}
        Engine jet;jet.name="jet";jet.position={18,0,0};jet.breathing=true;jet.thrustTable={{0,1},{0,20},{5e4,5e4,1e4,1e4}};jet.fuelTable={{0},{0},{2e-5}};jet.spool=2;st.engines={jet};
        const Vehicle v{"taxi",{st},0};
        auto rolling=[&](double speed){const double sag=10000*9.79/3e6;return Flight(v,e,Flight::free(e,v,t0,{.5,-1.4,2-sag},{speed,0,0},{}));};
        auto run=[](Flight& f,double seconds){for(Tt t=0;t<static_cast<Tt>(seconds*1e9);)t+=f.advance(std::min<Tt>(second/1000,static_cast<Tt>(seconds*1e9)-t));};
        near(Flight(v,e,Flight::pad(e,v,t0,{.5,-1.4,0},0,0)).sample().geodetic.height,2,1e-6,"A level aircraft on the pad stands on its wheels"); // Review regression.
        Flight roll=rolling(20);run(roll,10);const double v10=roll.sample().nedVelocity.x;
        std::printf("wheels: 20 m/s rolling 10 s -> %.3f m/s (mu_r g: %.3f)\n",v10,20-.015*9.79*10);near(v10,20-.015*9.79*10,.08,"Rolling resistance");
        Flight stop=rolling(20);stop.brake(1);run(stop,5);const double vs=stop.sample().nedVelocity.x;near(vs,0,.05,"Full brakes stop it");
        Flight turn=rolling(10);turn.steer(.1,0);run(turn,3);const double yaw=turn.sample().euler.x;
        std::printf("steering 0.1 rad at 10 m/s for 3 s: heading %.3f rad (kinematic %.3f)\n",yaw,10*std::tan(.1)/9*3);near(yaw,10*std::tan(.1)/9*3,.06,"Steered nose wheel turns it");
        Flight jetFlight=rolling(0);jetFlight.ignite();run(jetFlight,2);const double t2=jetFlight.engineThrust()[0];
        near(t2,5e4*(1-std::exp(-1.)),500,"Jet spools to 63% at its time constant");near(jetFlight.massFlow(),t2*2e-5,1e-9,"Fuel flow TSFC x thrust");
        Environment vacuum=e;vacuum.atmosphere=false;Flight dry(v,vacuum,Flight::free(vacuum,v,t0,{.5,-1.4,2-10000*9.79/3e6},{},{}));dry.ignite();run(dry,3);near(dry.engineThrust()[0],0,0,"No air, no jet thrust");
    }
    // Water and structures: an upright 9 m cylinder 3.7 m across (10 t) 2 m deep feels ρ g πR² 2 m of buoyancy and, let go,
    // floats with its weight carried; a 1 t vehicle whose hard point is its centre of mass comes to rest on a catch bar.
    {
        Environment e;e.sunMoon=false;e.relativity=false;e.ground=0;e.water=true;const Tt t0=ttFromUtc(1792238400.);const double pi=3.14159265358979323846;
        Stage st;st.name="hull";st.propellant=0;st.massFull=st.massEmpty=10000;st.cgFull=st.cgEmpty=4.5;st.inertiaFull=st.inertiaEmpty={2e4,8e4,8e4};st.length=9;
        st.aero.lift=st.aero.drag=st.aero.side=st.aero.roll=st.aero.pitch=st.aero.yaw={{0},{0},{0}};Vehicle v{"hull",{st},0};v.diameter=3.7;
        const double R=1.85,rho=1025;
        Flight still(v,e,Flight::free(e,v,t0,{.5,-1.4,2.5},{},{0,pi/2,0}));const Sample x=still.sample();const double gl=e.planet.gm/dot(still.state().position,still.state().position);
        near(norm(x.contactForce),rho*gl*pi*R*R*2,1e-6*rho*gl*pi*R*R*2,"Buoyancy of 2 m of an upright cylinder");
        // Let go upright it tips over (a tall uniform cylinder is unstable) and floats on its side: buoyancy then carries its weight.
        Flight drop(v,e,Flight::free(e,v,t0,{.5,-1.4,4.5-.5},{},{0,pi/2,0}));for(int i=0;i<60000;++i)drop.advance(second/1000);
        const Sample y=drop.sample();const double weight=10000*e.planet.gm/dot(drop.state().position,drop.state().position);
        std::printf("floating after 60 s: buoyancy %.1f N for a weight of %.1f N, pitch %.1f deg, heave %.4f m/s\n",norm(y.contactForce),weight,y.euler.y*180/pi,y.nedVelocity.z);
        near(norm(y.contactForce)/weight,1,.01,"Floats: buoyancy carries the weight");near(y.nedVelocity.z,0,.02,"Heave damped");
        Environment tower=e;tower.water=false;const double h=10;const V3 a=fixedPosition(earth,{.5,-1.4-1e-6,h}),b=fixedPosition(earth,{.5,-1.4+1e-6,h});
        tower.structures={{"arm",a,b,.3,1e7,1e5,.5}};
        Stage hung=st;hung.massFull=hung.massEmpty=1000;hung.inertiaFull=hung.inertiaEmpty={500,2000,2000};hung.hardPoints={{4.5,0,0}};
        const Vehicle hv{"hung",{hung},0};Flight caught(hv,tower,Flight::free(tower,hv,t0,{.5,-1.4,h+.3+.2},{},{0,pi/2,0}));for(int i=0;i<5000;++i)caught.advance(second/1000);
        const double rest=caught.sample().geodetic.height;std::printf("caught: centre of mass %.4f m over the bar's centre (radius 0.3, mg/k %.4f)\n",rest-h,1000*9.79/1e7);
        near(rest-h,.3-1000*9.79/1e7,2e-3,"Rests on the catch bar");near(caught.sample().nedVelocity.z,0,1e-3,"At rest");
        // The same bar on a hinge 5 m north, laid 90° away: swung in, it carries the vehicle alike; at 1 rad/s it takes its time.
        const V3 c=fixedPosition(earth,{.5,-1.4,h}),up=(fixedPosition(earth,{.5,-1.4,h+1})-c)/norm(fixedPosition(earth,{.5,-1.4,h+1})-c);
        const V3 north=(fixedPosition(earth,{.5+1e-7,-1.4,h})-c)/norm(fixedPosition(earth,{.5+1e-7,-1.4,h})-c),pivot=c+north*5;
        auto turn=[&](V3 x,double t){const V3 v=x-pivot;return pivot+v*std::cos(t)+cross(up,v)*std::sin(t)+up*(dot(up,v)*(1-std::cos(t)));};
        Environment hinge=tower;auto& arm=hinge.structures[0];arm.a=turn(a,-pi/2);arm.b=turn(b,-pi/2);arm.hinged=true;arm.pivot=pivot;arm.axis=up;arm.high=pi/2;
        Flight swung(hv,hinge,Flight::free(hinge,hv,t0,{.5,-1.4,h+.3+.2},{},{0,pi/2,0}));swung.swing(pi/2);for(int i=0;i<5000;++i)swung.advance(second/1000);
        near(swung.sample().geodetic.height-h,.3-1000*9.79/1e7,2e-3,"Rests on the bar swung in");
        hinge.structures[0].rate=1;Flight moving(hv,hinge,Flight::free(hinge,hv,t0,{.5,-1.4,h+50},{},{0,pi/2,0}));moving.swing(pi);for(int i=0;i<500;++i)moving.advance(second/1000);
        near(moving.structures()[0].angle,.5,1e-9,"Swings at its rate");near(moving.structures()[0].target,pi/2,0,"Within its range");
        // A solid hull (crash speed 5 m/s): dropped upright at 20 m/s it breaks up striking the ground; at 2 m/s it does not.
        // Driven sideways into a bar at 10 m/s it breaks up there, at 1 m/s the bar holds it off; a tile past its failure
        // temperature breaks it up too; then nothing lights.
        Vehicle solid=v;solid.crashSpeed=5;solid.stages[0].engines={Engine{"e",{9,0,0},{1,0,0},1e5,3000,.5,.4,0,0}};solid.stages[0].propellant=1000;solid.stages[0].massFull=11000;
        Environment flat=tower;flat.structures.clear();
        auto fall=[&](double speed){Flight f(solid,flat,Flight::free(flat,solid,t0,{.5,-1.4,4.5+.1},{0,0,speed},{0,pi/2,0}));
            for(int i=0;i<3000&&f.breakup()==Flight::Breakup::none&&!f.state().landed;++i)f.advance(second/1000);return f;}; // Its lower end 10 cm up.
        const Flight hard=fall(20),soft=fall(2);
        near(hard.breakup()==Flight::Breakup::ground?1:0,1,0,"Breaks up striking the ground at 20 m/s");near(hard.breakupSpeed(),20,.5,"at the speed it struck (m/s)");
        near(soft.breakup()==Flight::Breakup::none&&soft.state().landed?1:0,1,0,"Whole at 2 m/s, at rest where it touched");
        Environment bar=tower;bar.structures[0].a=fixedPosition(earth,{.5+.5e-6,-1.4,300});bar.structures[0].b=fixedPosition(earth,{.5-.5e-6,-1.4,300}); // North-south, 300 m up.
        auto side=[&](double speed){Flight f(solid,bar,Flight::free(bar,solid,t0,{.5,-1.4-2.2/(6378e3*std::cos(.5)),300},{0,speed,0},{0,pi/2,0})); // Just off it.
            for(int i=0;i<500&&f.breakup()==Flight::Breakup::none;++i)f.advance(second/1000);return f;};
        const Flight struck=side(10),touched=side(1);
        near(struck.breakup()==Flight::Breakup::structure?1:0,1,0,"Breaks up driven into a bar at 10 m/s");near(touched.breakup()==Flight::Breakup::none?1:0,1,0,"Held off by the bar at 1 m/s");
        Vehicle tiled=solid;ThermalNode tile;tile.name="tile";tile.start=300;tile.failure=290;tiled.stages[0].thermal={tile};
        Flight hot(tiled,flat,Flight::free(flat,tiled,t0,{.5,-1.4,1000},{},{0,pi/2,0}));hot.advance(second/100);hot.ignite();
        near(hot.breakup()==Flight::Breakup::heat?1:0,1,0,"Breaks up past a tile's failure temperature");near(hot.state().burning?1:0,0,0,"Then nothing lights");
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
        Flight spinFine(l,e,s),spinCoarse(l,e,s);spinFine.steps(second/1000);spinCoarse.steps(second/1000,second);spinFine.torque({0,2e5,0});spinCoarse.torque({0,2e5,0});
        run(spinFine,10*second);run(spinCoarse,10*second);
        require(spinFine.hash()==spinCoarse.hash(),"Commanded torque keeps the fixed step");
    }
    // Solar radiation pressure: full sun, umbra and a monotonic penumbra past the Earth; in
    // sunlight the accelerometer reads P (1 au/d)² Cr A/m away from the Sun.
    {
        const V3 sun{1.496e11,0,0};
        require(sunlight({7e6,0,0},sun,6378137.)==1&&sunlight({-7e6,0,0},sun,6378137.)==0,"Sunlit and umbra");
        double last=0;for(double y=6.30e6;y<6.46e6;y+=1e4){const double nu=sunlight({-7e6,y,0},sun,6378137.);require(nu>=last&&nu>=0&&nu<=1,"Penumbra");last=nu;}
        require(last==1&&sunlight({-7e6,6.37e6,0},sun,6378137.)>0&&sunlight({-7e6,6.37e6,0},sun,6378137.)<1,"Penumbra limits");
        // Below the reference radius (review regression): night side dark, noon lit, the terminator partial.
        require(sunlight({-6.37e6,0,0},sun,6378137.)==0&&sunlight({6.37e6,0,0},sun,6378137.)==1,"Low ground follows the Sun");
        const double dusk=sunlight({0,6.37e6,0},sun,6378137.);require(dusk>.3&&dusk<.7,"Low ground at the terminator");
        Environment e;const Tt t=ttFromUtc(1792238400.);const V3 s=perturbers(t).sun,side=cross(s,V3{0,0,1});
        State st;st.time=t;st.position=side*(4.2e7/norm(side));st.velocity={};st.propellant=0;
        Vehicle v=nescSphere();v.sunArea=10;v.reflectivity=1.5;const Flight f(v,e,st);const Sample x=f.sample();const V3 d=s-st.position;
        const double expected=1361./299792458.*std::pow(149597870700./norm(d),2)*1.5*10/x.mass;
        near(norm(x.specificForce)/expected,1,1e-9,"Radiation pressure magnitude");
        near(dot(matrix(f.state().attitude)*x.specificForce,d)/(norm(x.specificForce)*norm(d)),-1,1e-12,"Radiation pressure points away from the Sun");
    }
    // Relativity: the Schwarzschild term advances the periapsis by 6π GM/(c² a (1−e²)) per orbit
    // (difference of the same integration with and without it, 40 orbits at a = 8000 km, e = 0.2).
    {
        Environment e;e.planet={earth.gm,6378137.,0,0,6378137.,{},0};e.rotation=Rotation::uniform(0);e.atmosphere=false;e.sunMoon=false;
        const double a=8e6,ecc=.2,period=2*pi*std::sqrt(a*a*a/e.planet.gm);State s;s.position={a*(1-ecc),0,0};s.velocity={0,std::sqrt(e.planet.gm/a*(1+ecc)/(1-ecc)),0};
        auto periapsis=[&](bool on){Environment x=e;x.relativity=on;Flight f(nescSphere(),x,s);f.steps(second);f.advance(static_cast<Tt>(std::llround(40*period*1e9)));
            const V3 r=f.state().position,v=f.state().velocity,ev=cross(v,cross(r,v))/x.planet.gm-r/norm(r);return std::atan2(ev.y,ev.x);};
        const double shift=periapsis(true)-periapsis(false),expected=40*6*pi*e.planet.gm/(299792458.*299792458.*a*(1-ecc*ecc));
        std::printf("relativity: periapsis advance %.4e rad in 40 orbits (theory %.4e)\n",shift,expected);near(shift/expected,1,.01,"Schwarzschild precession");
    }
    // Aerodynamic heating: Sutton-Graves at 60 km and 7 km/s (≈ k √(ρ/Rn) v³), integrated into the
    // heat load; none on the pad and none in vacuum.
    {
        Environment e;e.sunMoon=false;const Vehicle v=genericLauncher();const Tt t=ttFromUtc(1792238400.);
        Flight f(v,e,Flight::free(e,v,t,{.3,-1.4,6e4},{7000,0,0},{0,0,0}));const Sample x=f.sample();
        const double expected=1.7415e-4*std::sqrt(x.air.density/v.stages[0].noseRadius)*std::pow(norm(x.nedVelocity),3);
        near(x.heatFlux/expected,1,1e-12,"Stagnation heating");std::printf("heating: %.1f W/cm² at 60 km and 7 km/s, Rn %.2f m\n",x.heatFlux*1e-4,v.stages[0].noseRadius);
        f.steps(second/1000);f.advance(second);near(f.state().heatLoad/x.heatFlux,1,.05,"Heat load over one second");
        Flight pad(v,e,Flight::pad(e,v,t,{.3,-1.4,0},0,pi/2));require(pad.sample().heatFlux==0,"No heating on the pad");
        struct Windy:AirModel{Air at(std::int64_t,double,double,double h)const override{Air a=standardAtmosphere(h);a.wind={60,0,0};return a;}std::string source()const override{return "wind";}};
        Environment windy=e;windy.air=std::make_shared<Windy>();Flight gust(v,windy,Flight::pad(windy,v,t,{.3,-1.4,0},0,pi/2));gust.steps(second/1000); // Review regression.
        const double q=gust.sample().heatFlux;gust.advance(second);require(q>0,"Wind heats the pad");near(gust.state().heatLoad/q,1,1e-6,"Heat load accrues on the pad");
        Environment moon=environment(Center::moon);Flight vacuum(v,moon,Flight::free(moon,v,t,{0,0,6e4},{1500,0,0},{0,0,0}));require(vacuum.sample().heatFlux==0,"No heating in vacuum");
    }
    // Landing legs on the Moon: rest at the static deflection m g / 4k, a 2 m drop dissipates and
    // settles there, a 1 m/s slide stops after v²/(2 μ g), and the engine lifts it off again.
    {
        Environment e=environment(Center::moon);e.ground=0;const Vehicle v=genericLander();const Stage& st=v.stages[0];const Tt t=ttFromUtc(1792238400.);
        const double g=e.planet.gm/(e.planet.radius*e.planet.radius),sag=st.massFull*g/(4*st.legStiffness),rest=st.length-st.cgFull-sag;
        auto run=[](Flight& f,double seconds,const std::function<void()>& each={}){const Tt end=f.state().time+static_cast<Tt>(seconds*1e9);while(f.state().time<end){f.advance(std::min<Tt>(end-f.state().time,10*second/1000));if(each)each();}};
        Flight pad(v,e,Flight::pad(e,v,t,{.2,.4,0},0,pi/2));pad.steps(second/1000,second);pad.release();run(pad,10);
        const Sample x=pad.sample();
        near(x.geodetic.height,rest,2e-4,"Lander rests on its legs");near(norm(x.nedVelocity),0,1e-3,"Lander at rest");near(x.specificForce.x,g,2e-3,"Accelerometer on the legs");
        require(pad.state().contact&&!pad.state().landed,"Leg contact without the CG stop");
        Flight drop(v,e,Flight::free(e,v,t,{.2,.4,st.length-st.cgFull+2},{},{0,pi/2,0}));drop.steps(second/1000,second);
        bool touched=false;double bounce=0;run(drop,20,[&]{touched=touched||drop.state().contact;if(touched)bounce=std::max(bounce,drop.sample().geodetic.height);});
        require(touched&&bounce<st.length-st.cgFull+1,"The drop loses energy");near(drop.sample().geodetic.height,rest,2e-4,"The drop settles");
        Flight slide(v,e,Flight::free(e,v,t,{.2,.4,rest},{1,0,0},{0,pi/2,0}));slide.steps(second/1000,second);
        const V3 start=slide.sample().fixedPosition;run(slide,6);const double distance=norm(slide.sample().fixedPosition-start);
        std::printf("lander: rests %.2f mm down, slides %.3f m from 1 m/s (v²/2μg %.3f m)\n",sag*1e3,distance,1/(2*st.friction*g));
        near(distance,1/(2*st.friction*g),.03,"Friction stops the slide");
        pad.ignite();const Tt off=pad.advance(10*second); // Review regression: lift-off ends the call at its instant.
        require(!pad.state().contact&&off<2*second,"Lift-off keeps its instant");run(pad,2);require(pad.sample().geodetic.height>rest+1,"Lift-off from the legs");
        // Review regressions: a 10 s coast cannot tunnel a hop's apex through the ground, and 100 ms
        // ticks on the legs stay stable (contact sub-steps).
        Flight hop(v,e,Flight::free(e,v,t,{.2,.4,rest+20},{},{0,pi/2,0}));hop.steps(second/1000,10*second);
        double top=0;for(Tt done=0;done<40*second;){done+=hop.advance(40*second-done);top=std::max(top,hop.sample().geodetic.height);}
        require(top<rest+21&&hop.state().contact,"Coast near the ground");near(hop.sample().geodetic.height,rest,2e-4,"The hop settles");
        Flight coarse(v,e,Flight::pad(e,v,t,{.2,.4,0},0,pi/2));coarse.steps(second/10);coarse.release();
        for(Tt done=0;done<10*second;)done+=coarse.advance(10*second-done);
        near(coarse.sample().geodetic.height,rest,1e-3,"100 ms ticks on the legs");near(norm(coarse.sample().nedVelocity),0,1e-3,"Coarse ticks at rest");
    }
    // IMU noise: overlapping Allan variance of a still gyro (10 Hz, 20000 s) fitted with
    // σ²(τ) = N²/τ + K²τ/3 returns the configured angle random walk and rate random walk
    // within 5%; the Gauss-Markov bias has its configured spread and correlation time.
    {
        ImuModel m;m.gyroscope.white=1e-3;m.gyroscope.walk=1e-5;Imu imu(m,42);constexpr double dt=.1;constexpr std::size_t n=200000;
        std::vector<double> angle(n+1);for(std::size_t i=0;i<n;++i)angle[i+1]=angle[i]+imu.measure(dt,{},{},{},{},0).rate.x*dt;
        double saa=0,sab=0,sbb=0,sya=0,syb=0; // Least squares on the relative error.
        for(std::size_t k=1;k<n/10;k=std::max(k+1,k*5/4)){
            const double tau=k*dt;double sum=0;for(std::size_t i=0;i+2*k<=n;++i){const double d=angle[i+2*k]-2*angle[i+k]+angle[i];sum+=d*d;}
            const double v=sum/(2*tau*tau*static_cast<double>(n+1-2*k)),fa=1/tau/v,fb=tau/v;saa+=fa*fa;sab+=fa*fb;sbb+=fb*fb;sya+=fa;syb+=fb;
        }
        const double det=saa*sbb-sab*sab,a=(sya*sbb-syb*sab)/det,b=(syb*saa-sya*sab)/det;
        std::printf("Allan: angle random walk %.4g (1e-3), rate random walk %.4g (1e-5)\n",std::sqrt(a),std::sqrt(3*b));
        near(std::sqrt(a)/1e-3,1,.05,"Angle random walk from the Allan variance");near(std::sqrt(3*b)/1e-5,1,.05,"Rate random walk from the Allan variance");
        ImuModel g;g.gyroscope.instability=1e-3;g.gyroscope.correlation=10;Imu markov(g,7);std::vector<double> x(n);
        for(auto& v:x)v=markov.measure(dt,{},{},{},{},0).rate.y;
        double mean=0,var=0,lag=0;for(double v:x)mean+=v/n;for(double v:x)var+=(v-mean)*(v-mean)/n;for(std::size_t i=0;i+100<n;++i)lag+=(x[i]-mean)*(x[i+100]-mean)/(n-100);
        std::printf("Gauss-Markov: sigma %.4g (1e-3), correlation at 10 s %.3f (%.3f)\n",std::sqrt(var),lag/var,std::exp(-1.));
        near(std::sqrt(var)/1e-3,1,.05,"Gauss-Markov spread");near(lag/var,std::exp(-1.),.05,"Gauss-Markov correlation time");
        Imu again(m,42),other(m,43);const auto first=Imu(m,42).measure(dt,{1,2,3},{},{},{},1e5);
        require(again.measure(dt,{1,2,3},{},{},{},1e5).specificForce.x==first.specificForce.x&&other.measure(dt,{1,2,3},{},{},{},1e5).rate.x!=first.rate.x,"Seeded noise");
        Imu ideal(imuModel(ImuGrade::ideal),1);const auto r=ideal.measure(dt,{1,2,3},{.1,.2,.3},{.1,.2,.3},{.01,.02,.03},101325);
        require(r.specificForce.z==3&&r.rate.y==.2&&r.deltaVelocity.x==.1&&r.deltaAngle.z==.03&&r.pressure==101325,"Ideal IMU is exact");
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
    { // To orbit off the Moon and Mars with the same guidance: an Apollo LM-sized ascent from Tranquility Base
      // (Apollo 11: 435 s to a 17 x 88 km orbit), and a two-stage Mars ascent from Jezero steered from
      // liftoff to a 250 km apoapsis, coasting there and circularizing to a 200 km perigee.
        namespace ex=albert::example;auto vec=[](V3 v){return ex::Vec{v.x,v.y,v.z};};
        auto fly=[&](Environment e,const Vehicle& v,Geodetic site,double heading,ex::GravityTurn g,ex::Body body,const char* name){
            Flight f(v,e,Flight::pad(e,v,ttFromUtc(1792238400.),site,heading,90*deg));const Tt t0=f.state().time;double burn=0;
            while(g.phase()!=ex::GravityTurn::Phase::orbit&&f.state().time-t0<1200*second&&!f.state().landed){
                const Sample s=f.sample();ex::Reading r;r.elapsed=secondsSinceJ2000(f.state().time-t0);r.height=s.geodetic.height;r.nedVelocity=vec(s.nedVelocity);
                r.qw=s.nedAttitude.w;r.qx=s.nedAttitude.x;r.qy=s.nedAttitude.y;r.qz=s.nedAttitude.z;r.rate=vec(f.state().rate);r.specificForce=vec(s.specificForce);
                r.position=vec(f.state().position);r.velocity=vec(f.state().velocity);r.stage=f.state().stage;r.stages=static_cast<int>(v.stages.size());r.burning=f.state().burning;r.clamped=f.state().clamped;r.propellant=f.state().propellant;
                const auto c=g.step(r);if(c.ignite)f.ignite();if(c.separate)f.separate();if(c.cutoff)f.cutoff();f.gimbal(c.gimbalPitch,c.gimbalYaw);f.torque({c.torque.x,c.torque.y,c.torque.z});
                if(f.state().burning)burn+=.02;
                for(Tt left=20*second/1000;left>0;)left-=f.advance(left);
            }
            const double peri=ex::GravityTurn::perigee(vec(f.state().position),vec(f.state().velocity),body),rn=norm(f.state().position),v2=dot(f.state().velocity,f.state().velocity);
            const double a=1/(2/rn-v2/body.mu),apo=2*a-peri-2*body.radius;
            std::printf("%s: orbit %.1f x %.1f km after %.0f s of burn, %.0f kg left\n",name,peri/1e3,apo/1e3,burn,f.state().propellant);
            return std::tuple{g.phase()==ex::GravityTurn::Phase::orbit,peri,apo,burn,f.state().propellant};};
        {Environment e=environment(Center::moon);e.ground=-1900;
         const auto [orbit,peri,apo,burn,left]=fly(e,genericLunarAscent(),{.67416*deg,23.47314*deg,-1900},270*deg,ex::GravityTurn(270*deg,0,18.3e3,15e3,ex::moon,true,85e3,60,60),ex::moon,"lunar ascent");
         require(orbit&&peri>=15e3&&apo>50e3&&apo<120e3&&burn>380&&burn<480&&left>0,"Lunar ascent to orbit as Apollo 11's (435 s, 17 x 88 km)");}
        {Environment e=environment(Center::mars);e.ground=-2500;
         const auto [orbit,peri,apo,burn,left]=fly(e,genericMarsAscent(),{18.4447*deg,77.4508*deg,-2500},90*deg,ex::GravityTurn(90*deg,0,250e3,200e3,ex::mars,true,250e3,200,1000,true),ex::mars,"Mars ascent");
         require(orbit&&peri>=200e3&&apo<600e3&&left>0,"Mars ascent to a 200 km perigee");}
    }
    std::cout<<"physics_test: ok\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
