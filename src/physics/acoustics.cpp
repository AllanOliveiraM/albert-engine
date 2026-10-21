#include "physics/acoustics.hpp"
#include <algorithm>
#include <cmath>
namespace albert::physics::acoustics {
namespace {
constexpr double pi=3.14159265358979323846,reference=101325,t0=293.15,t01=273.16;
// SP-8072 Fig. 5: 10 log10[W(f) Ue / (W_OA d_e)] (dB) against log10 of the Strouhal number f d_e/Ue, digitised (±0.5 dB).
constexpr double strouhal[][2]{{.002,-.1},{.003,2.4},{.004,4.6},{.005,6.4},{.007,8.3},{.01,9.9},{.015,10.8},{.02,11.2},{.03,11.1},{.04,10.6},
    {.05,9.9},{.07,7.9},{.1,5.7},{.15,3.1},{.2,1.0},{.3,-2.4},{.4,-5.0},{.5,-6.9},{.7,-9.8},{1,-12.9},{1.5,-16.4},{2,-18.9},{3,-22.6},{4,-25.0},{5,-26.8}};
constexpr int points=sizeof strouhal/sizeof strouhal[0];
double curve(double st){ // dB; extrapolated with the end slopes.
    const double x=std::log10(std::max(st,1e-9));int i=1;while(i<points-1&&std::log10(strouhal[i][0])<x)++i;
    const double x0=std::log10(strouhal[i-1][0]),x1=std::log10(strouhal[i][0]);
    return strouhal[i-1][1]+(strouhal[i][1]-strouhal[i-1][1])*(x-x0)/(x1-x0);
}
double density(double st){ // The curve as a density in St with unit area (the figure's integrates to ~1.28).
    static const double area=[]{double a=0;for(double x=-4;x<1.5;x+=1e-3){const double s=std::pow(10.,x+5e-4);a+=std::pow(10.,curve(s)/10)*s*std::log(10.)*1e-3;}return a;}();
    return std::pow(10.,curve(st)/10)/area;
}
// The directivity lobe: k (floor + exp(-((θ - 65°)/σ)²)), σ 24.06° (−3 dB at ±20°), floor 0.012, k so the sphere averages 1.
constexpr double lobeCentre=65,lobeWidth=24.0637,lobeFloor=.012;
double lobe(double theta){return lobeFloor+std::exp(-std::pow((theta-lobeCentre)/lobeWidth,2));}
}
double bandCentre(int band){return 1000*std::pow(10.,(band-29)/10.);}
double absorption(double f,double temperature,double pressure,double h){
    const double pa=pressure/reference,t=temperature/t0;
    const double frO=pa*(24+4.04e4*h*(.02+h)/(.391+h)),frN=pa/std::sqrt(t)*(9+280*h*std::exp(-4.170*(std::pow(t,-1./3)-1)));
    return 8.686*f*f*(1.84e-11/pa*std::sqrt(t)+std::pow(t,-2.5)*(.01275*std::exp(-2239.1/temperature)/(frO+f*f/frO)+.1068*std::exp(-3352./temperature)/(frN+f*f/frN)));
}
double molarFromRelative(double relative,double temperature,double pressure){
    const double saturation=std::pow(10.,-6.8346*std::pow(t01/temperature,1.261)+4.6151); // psat/pr.
    return relative*saturation/(pressure/reference);
}
double molarFromSpecific(double q){q=std::clamp(q,0.,.1);return 100*q/(q+.62198*(1-q));} // Mole fraction of water (%).
double soundPower(const RocketNoise& r){return r.efficiency*.5*std::max(r.thrust,0.)*std::max(r.exhaust,0.);}
Spectrum powerLevels(const RocketNoise& r,double shift){
    Spectrum l;const double w=soundPower(r),scale=r.exitDiameter/std::max(r.exhaust,1.);
    for(int b=0;b<bands;++b){ // ∫ over the band (edges at 2^±1/6) of W ρ(f d_e/Ue) d_e/Ue df, eight steps in log f.
        const double lo=bandCentre(b)*std::pow(2.,-1./6)/shift,hi=bandCentre(b)*std::pow(2.,1./6)/shift;double p=0;
        for(int k=0;k<8;++k){const double f=lo*std::pow(hi/lo,(k+.5)/8),df=f*std::log(hi/lo)/8;p+=w*density(f*scale)*scale*df;}
        l[static_cast<std::size_t>(b)]=p>0?10*std::log10(p)+120:-1e9;}
    return l;
}
double directivity(double theta){
    static const double k=[]{double s=0,w=0;for(int i=0;i<3600;++i){const double t=(i+.5)/20,ws=std::sin(t*pi/180);s+=lobe(t)*ws;w+=ws;}return w/s;}();
    return 10*std::log10(k*lobe(std::clamp(theta,0.,180.)));
}
Spectrum freeField(const RocketNoise& r,double distance,double theta,double shift){
    Spectrum l=powerLevels(r,shift);const double spread=10*std::log10(4*pi*std::max(distance,1.)*std::max(distance,1.)),d=directivity(theta);
    for(auto& x:l)x+=d-spread;
    return l;
}
double flowPower(double drag,double mach){return 4e-5*std::pow(std::clamp(mach,0.,.5)/.24,3)*std::max(drag,0.);}
Spectrum flowLevels(double w,double speed,double diameter,double shift){
    Spectrum l;const double peak=.2*std::max(speed,1e-3)/std::max(diameter,.01);
    for(int b=0;b<bands;++b){ // ∫ over the band of W 3x²/(1+x²)^2.5 dx, x = f/f_p, eight steps in log f.
        const double lo=bandCentre(b)*std::pow(2.,-1./6)/shift/peak,hi=bandCentre(b)*std::pow(2.,1./6)/shift/peak;double p=0;
        for(int k=0;k<8;++k){const double x=lo*std::pow(hi/lo,(k+.5)/8),dx=x*std::log(hi/lo)/8;p+=w*3*x*x/std::pow(1+x*x,2.5)*dx;}
        l[static_cast<std::size_t>(b)]=p>0?10*std::log10(p)+120:-1e9;}
    return l;
}
double overall(const Spectrum& l){double s=0;for(double x:l)s+=std::pow(10.,x/10);return s>0?10*std::log10(s):-1e9;}
Spectrum ground(double height,double grazing,double c){
    Spectrum g{};const double fade=std::clamp((100-height)/50,0.,1.);if(fade<=0||height<0)return g;
    const double path=2*height*std::max(std::sin(grazing),0.);
    for(int b=0;b<bands;++b){const double phase=std::min(2*pi*bandCentre(b)/c*path,pi/2);g[static_cast<std::size_t>(b)]=fade*10*std::log10(2+2*std::cos(phase));}
    return g;
}
Heard hear(const std::vector<Emission>& history,double now,V3 listener,V3 listenerVelocity,const Planet& planet,const std::function<Air(const Geodetic&)>& air){
    Heard h;if(history.empty())return h;
    auto at=[&](double t){ // Interpolated position and velocity; the rest from the sample before.
        auto it=std::upper_bound(history.begin(),history.end(),t,[](double x,const Emission& e){return x<e.time;});
        if(it==history.begin())return history.front();if(it==history.end())return history.back();
        Emission e=*(it-1);const Emission& n=*it;const double f=(t-e.time)/std::max(n.time-e.time,1e-9);
        e.position=e.position+(n.position-e.position)*f;e.velocity=e.velocity+(n.velocity-e.velocity)*f;e.time=t;return e;};
    const Geodetic here=geodetic(planet,listener);const Air local=air(here);if(!(local.pressure>10))return h;
    auto humidity=[&](const Air& a){return a.humidity>=0?molarFromSpecific(a.humidity):molarFromRelative(50,a.temperature,std::max(a.pressure,1.));};
    struct Path {double slowness=0;Spectrum absorbed{};bool blocked=false;};
    auto path=[&](V3 from){ // Eight midpoints of the straight path.
        Path p;const V3 d=listener-from;const double length=norm(d);
        for(int i=0;i<8;++i){const Geodetic g=geodetic(planet,from+d*((i+.5)/8));if(g.height<-1000){p.blocked=true;return p;}
            const Air a=air(g);const double c=a.sound>0?a.sound:331;p.slowness+=length/8/c;const double m=humidity(a);
            for(int b=0;b<bands;++b)p.absorbed[static_cast<std::size_t>(b)]+=a.pressure>1e-3?absorption(bandCentre(b),std::max(a.temperature,150.),a.pressure,m)*length/8:1e9;}
        return p;};
    // Emission time: the latest root of now - t - |listener - source(t)| c̄⁻¹ (a supersonic source has several; the newest
    // sound is the nearest), back from now to the first sample that has arrived, then bisection after it; once more with the
    // path at that time.
    double te=now;Path p=path(at(now).position);
    for(int pass=0;pass<2;++pass){
        const double mean=p.slowness/std::max(norm(listener-at(te).position),1e-9);
        auto f=[&](double t){return now-t-norm(listener-at(t).position)*mean;};
        std::size_t k=history.size();while(k>0&&!(f(history[k-1].time)>=0))--k;if(k==0)return h; // Nothing emitted yet has arrived.
        double lo=history[k-1].time,hi=k<history.size()?std::min(history[k].time,now):now;
        for(int i=0;i<60;++i){const double mid=(lo+hi)/2;(f(mid)>0?lo:hi)=mid;}
        te=(lo+hi)/2;p=path(at(te).position);}
    const Emission e=at(te);if(!(e.on||e.dragPower>0)||p.blocked)return h;
    const V3 d=listener-e.position;const double r=norm(d);const V3 n=d*(1/std::max(r,1e-9));
    auto windAt=[&](const Geodetic& g){const Air a=air(g);const double sl=std::sin(g.latitude),cl=std::cos(g.latitude),so=std::sin(g.longitude),co=std::cos(g.longitude);
        const V3 north{-sl*co,-sl*so,cl},east{-so,co,0};return north*a.wind.x+east*a.wind.y;};
    const Geodetic source=geodetic(planet,e.position);const double c=local.sound>0?local.sound:331;
    const double vs=dot(e.velocity-windAt(source),n),vl=dot(listenerVelocity-windAt(here),n);
    h.doppler=std::clamp((c-vl)/std::max(c-vs,.25*c),.25,4.);
    const double plume=std::acos(std::clamp(dot(e.plume,n),-1.,1.))*180/pi;
    const double sl=std::sin(here.latitude),cl=std::cos(here.latitude),so=std::sin(here.longitude),co=std::cos(here.longitude);
    const V3 up{cl*co,cl*so,sl};const Spectrum g=ground(here.height,std::asin(std::clamp(-dot(n,up),-1.,1.)),c);
    // The engines' noise and the flow's, each spread, then the ground, the path's absorption and the listener's impedance.
    const double impedance=local.density>0?10*std::log10(local.density*c/416.9):0.,spread=10*std::log10(4*pi*std::max(r,1.)*std::max(r,1.));
    const Spectrum rocket=e.on?freeField(e.noise,r,plume,h.doppler):Spectrum{};
    const Spectrum flow=e.dragPower>0?flowLevels(flowPower(e.dragPower,e.mach),e.mach>0?e.mach*e.sound:norm(e.velocity),e.diameter,h.doppler):Spectrum{};
    Spectrum engines{};
    for(int b=0;b<bands;++b){const auto k=static_cast<std::size_t>(b);const double loss=g[k]-p.absorbed[k]+impedance;
        engines[k]=e.on?rocket[k]+loss:-1e9;const double f=e.dragPower>0?flow[k]-spread+loss:-1e9;
        const double sum=std::pow(10.,engines[k]/10)+std::pow(10.,f/10);h.levels[k]=sum>0?10*std::log10(sum):-1e9;}
    h.rocket=e.on?overall(engines):-1e9;h.delay=now-te;h.distance=r;h.emitted=te;h.audible=true;return h;
}
Boom blast(double tnt,double distance,double pressure){
    const double w=std::max(tnt,1e-3),z=std::max(distance,.1)/std::cbrt(w);
    const double ratio=808*(1+std::pow(z/4.5,2))/std::sqrt((1+std::pow(z/.048,2))*(1+std::pow(z/.32,2))*(1+std::pow(z/1.35,2)));
    const double td=std::cbrt(w)*980*(1+std::pow(z/.54,10))/((1+std::pow(z/.02,3))*(1+std::pow(z/.74,6))*std::sqrt(1+std::pow(z/6.9,2)))*1e-3;
    return{0,ratio*pressure,td};
}
Boom carlson(double mach,double pv,double av,double pg,double height,double length,bool ground){
    constexpr double ks=.1;const double m2=std::max(mach*mach-1,1e-6),he=std::max(height,length);
    return{0,(ground?2:1)*std::sqrt(std::max(pv,0.)*std::max(pg,0.))*std::pow(m2,.125)*std::pow(he,-.75)*std::pow(length,.75)*ks,
        3.42/std::max(av,1.)*mach*std::pow(m2,-.375)*std::pow(he,.25)*std::pow(length,.75)*ks};
}
std::vector<Boom> booms(const std::vector<Emission>& history,double now,V3 listener,const Planet& planet,const std::function<Air(const Geodetic&)>& air){
    std::vector<Boom> out;const Geodetic here=geodetic(planet,listener);const Air local=air(here);if(!(local.pressure>10)||history.size()<3)return out;
    const double cl=local.sound>0?local.sound:331;
    auto arrival=[&](const Emission& e){return e.time+norm(listener-e.position)/(.5*(e.sound>0?e.sound:cl)+.5*cl);};
    double t0=arrival(history[0]),t1=arrival(history[1]);
    for(std::size_t k=1;k+1<history.size();++k){const double t2=arrival(history[k+1]);const Emission& e=history[k];
        if(e.mach>1&&t1<t0&&t1<=t2){ // A local minimum: refined by the parabola through the three.
            const double d=t0-2*t1+t2,u=d>0?std::clamp(.5*(t0-t2)/d,-1.,1.):0.,at=t1-.25*(t0-t2)*u;
            if(at<=now){const Geodetic g=geodetic(planet,e.position);
                Boom b=carlson(e.mach,e.pressure,e.sound,local.pressure,g.height-here.height,std::max(e.length,1.),here.height<100);b.time=at;out.push_back(b);}}
        t0=t1;t1=t2;}
    std::sort(out.begin(),out.end(),[](const Boom& a,const Boom& b){return a.time<b.time;});return out;
}
}
