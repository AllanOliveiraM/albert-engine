#include "physics/earth.hpp"
#include "physics/gravity.hpp"
#include <erfa.h>
#include <array>
#include <cmath>
#include <stdexcept>
namespace albert::physics {
namespace {
constexpr double unixJ2000=946728000.; // 2000-01-01 12:00:00 UTC.
double leapSeconds(double unix){ // TAI-UTC; before 1960 UTC is undefined: 0.
    const double days=std::floor(unix/86400.);
    const long long z=static_cast<long long>(days)+719468,era=(z>=0?z:z-146096)/146097;
    const long long doe=z-era*146097,yoe=(doe-doe/1460+doe/36524-doe/146096)/365,doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
    const int d=static_cast<int>(doy-(153*mp+2)/5+1),m=static_cast<int>(mp<10?mp+3:mp-9),y=static_cast<int>(yoe+era*400+(m<=2));
    double dat=0;return eraDat(y,m,d,unix/86400.-days,&dat)<0?0.:dat;
}
}
Tt ttFromUtc(double unix){
    if(!std::isfinite(unix))throw std::invalid_argument("Invalid UTC instant");
    const double whole=std::floor(unix);
    return static_cast<Tt>(whole-unixJ2000)*second+static_cast<Tt>(std::llround((unix-whole+leapSeconds(unix)+32.184)*1e9));
}
double utcFromTt(Tt t){
    double unix=secondsSinceJ2000(t)+unixJ2000-32.184;
    for(int i=0;i<3;++i)unix=secondsSinceJ2000(t)+unixJ2000-32.184-leapSeconds(unix);
    return unix;
}
Geodetic geodetic(const Planet& pl,V3 r){
    const double a=pl.radius,e2=pl.e2(),e4=e2*e2,rho2=r.x*r.x+r.y*r.y,rho=std::sqrt(rho2);
    const double p=rho2/(a*a),q=(1-e2)/(a*a)*r.z*r.z,s0=(p+q-e4)/6;
    double u;
    if(e4==0)u=(p+q)/2;
    else{const double s=e4*p*q/(4*s0*s0*s0),t=std::cbrt(1+s+std::sqrt(s*(2+s)));u=s0*(1+t+1/t);}
    const double v=std::sqrt(u*u+e4*q),w=e2*(u+v-q)/(2*v),k=std::sqrt(u+v+w*w)-w,d=k*rho/(k+e2),dz=std::hypot(d,r.z);
    return{2*std::atan2(r.z,d+dz),std::atan2(r.y,r.x),(k+e2-1)/k*dz};
}
V3 fixedPosition(const Planet& p,Geodetic g){
    const double s=std::sin(g.latitude),c=std::cos(g.latitude),e2=p.e2(),n=p.radius/std::sqrt(1-e2*s*s);
    return{(n+g.height)*c*std::cos(g.longitude),(n+g.height)*c*std::sin(g.longitude),(n*(1-e2)+g.height)*s};
}
M3 nedFromFixed(double lat,double lon){
    const double sp=std::sin(lat),cp=std::cos(lat),sl=std::sin(lon),cl=std::cos(lon);
    return{{{-sp*cl,-sp*sl,cp},{-sl,cl,0},{-cp*cl,-cp*sl,-sp}}};
}
namespace {
#include "egm2008.inc"
constexpr std::size_t packed(int n,int m){return static_cast<std::size_t>(n*(n+1)/2+m);}
}
V3 harmonics(int degree,V3 r){
    if(degree<0||degree>egm2008Degree)throw std::invalid_argument("EGM2008 degree out of range");
    return egm2008Gravity().acceleration(degree,r);
}
const Gravity& egm2008Gravity(){
    static const Gravity g=[]{auto f=std::make_shared<GravityField>();f->name="EGM2008";f->gm=3.986004415e14;f->radius=6378136.3;f->degree=egm2008Degree;
        const std::size_t n=packed(egm2008Degree+1,0);f->c.resize(n);f->s.resize(n);for(std::size_t i=0;i<n;++i){f->c[i]=egm2008[i][0];f->s[i]=egm2008[i][1];}f->c[0]=1;
        return Gravity(std::move(f));}();
    return g;
}
double geoidHeight(double lat,double lon){
    // Disturbing potential on the ellipsoid (EGM2008 minus the WGS 84 normal field) over normal gravity.
    const V3 r=fixedPosition(earth,{lat,lon,0});constexpr int top=egm2008Degree;
    const double rn=norm(r),s=r.z/rn,c=std::sqrt(r.x*r.x+r.y*r.y)/rn,l=std::atan2(r.y,r.x),q=6378136.3/rn;
    std::array<double,packed(top+1,0)> p{};p[0]=1;
    for(int m=0;m<=top;++m){
        if(m)p[packed(m,m)]=(m==1?std::sqrt(3.):std::sqrt((2*m+1)/(2.*m)))*c*p[packed(m-1,m-1)];
        if(m<top)p[packed(m+1,m)]=std::sqrt(2*m+3.)*s*p[packed(m,m)];
        for(int n=m+2;n<=top;++n)p[packed(n,m)]=std::sqrt((2*n-1.)*(2*n+1)/((n-m)*(n+m)))*s*p[packed(n-1,m)]-std::sqrt((2*n+1.)*(n+m-1)*(n-m-1)/((n-m)*(n+m)*(2*n-3.)))*p[packed(n-2,m)];
    }
    double sum=0,qn=q;
    for(int n=2;n<=top;++n){qn*=q;for(int m=0;m<=n;++m)sum+=qn*p[packed(n,m)]*(egm2008[packed(n,m)][0]*std::cos(m*l)+egm2008[packed(n,m)][1]*std::sin(m*l));}
    constexpr double normal[]{-.484166774985e-3,.790303733511e-6,-.168724961151e-8,.346052468394e-11,-.265002225747e-14},wgm=3.986004418e14;
    double t=3.986004415e14/rn*(1+sum)-wgm/rn;const double qw=6378137./rn;
    for(int k=0;k<5;++k){const int n=2*k+2;t-=wgm/rn*std::pow(qw,n)*normal[k]*p[packed(n,0)];}
    const double s2=std::sin(lat)*std::sin(lat);return t/(9.7803253359*(1+.00193185265241*s2)/std::sqrt(1-earth.e2()*s2));
}
V3 gravitation(const Planet& pl,V3 r){
    if(pl.field)return pl.field->acceleration(pl.degree,r);
    if(pl.egm2008)return harmonics(pl.degree,r);
    const double rn=norm(r),u=r.z/rn,k=pl.gm/(rn*rn);const V3 h=r/rn;
    V3 a=-k*h;
    double p0=1,p1=u,d0=0,d1=1,f=pl.gravityRadius/rn,fn=f;
    for(int n=2;n<=pl.degree;++n){
        const double p2=((2*n-1)*u*p1-(n-1)*p0)/n,d2=d0+(2*n-1)*p1;fn*=f;
        a+=k*pl.zonal[n]*fn*((n+1)*p2*h+d2*(u*h-V3{0,0,1}));
        p0=p1;p1=p2;d0=d1;d1=d2;
    }
    return a;
}
namespace {
M3 rotationX(double a){const double c=std::cos(a),s=std::sin(a);return{{{1,0,0},{0,c,s},{0,-s,c}}};}
M3 rotationY(double a){const double c=std::cos(a),s=std::sin(a);return{{{c,0,-s},{0,1,0},{s,0,c}}};}
// IAU WGCCRE rotational elements as in NAIF pck00011 (Moon: 2009 mean Earth; Mars: 2015),
// TT as TDB: angles (deg, deg/day) and their periodic terms (deg). Rates are analytic.
struct Angle {double base,rate;};struct Term {int angle;double c;};struct Series {double v,r;};
constexpr double century=36525;
constexpr Angle moonAngles[]{{125.045,-.0529921},{250.089,-.1059842},{260.008,13.0120009},{176.625,13.3407154},{357.529,.9856003},{311.589,26.4057084},{134.963,13.0649930},
    {276.617,.3287146},{34.226,1.7484877},{15.134,-.1589763},{119.743,.0036096},{239.961,.1643573},{25.053,12.9590088}};
constexpr Term moonRa[]{{0,-3.8787},{1,-.1204},{2,.0700},{3,-.0172},{5,.0072},{9,-.0052},{12,.0043}};
constexpr Term moonDec[]{{0,1.5419},{1,.0239},{2,-.0278},{3,.0068},{5,-.0029},{6,.0009},{9,.0008},{12,-.0009}};
constexpr Term moonW[]{{0,3.5610},{1,.1208},{2,-.0642},{3,.0158},{4,.0252},{5,-.0066},{6,-.0047},{7,-.0046},{8,.0028},{9,.0052},{10,.0040},{11,.0019},{12,-.0044}};
constexpr Angle marsAngles[]{{198.991226,19139.4819985/century},{226.292679,38280.8511281/century},{249.663391,57420.7251593/century},{266.183510,76560.6367950/century},{79.398797,.5042615/century},
    {122.433576,19139.9407476/century},{43.058401,38280.8753272/century},{57.663379,57420.7517205/century},{79.476401,76560.6495004/century},{166.325722,.5042615/century},
    {129.071773,19140.0328244/century},{36.352167,38281.0473591/century},{56.668646,57420.9295360/century},{67.364003,76560.2552215/century},{104.792680,95700.4387578/century},{95.391654,.5042615/century}};
constexpr Term marsRa[]{{0,.000068},{1,.000238},{2,.000052},{3,.000009},{4,.419057}};
constexpr Term marsDec[]{{5,.000051},{6,.000141},{7,.000031},{8,.000005},{9,1.591274}};
constexpr Term marsW[]{{10,.000145},{11,.000157},{12,.000040},{13,.000001},{14,.000001},{15,.584542}};
template<std::size_t N>void add(Series& s,const Angle* a,double d,const Term(&terms)[N],bool cosine){
    constexpr double deg=3.14159265358979323846/180;
    for(const Term& k:terms){const double e=(a[k.angle].base+a[k.angle].rate*d)*deg,de=a[k.angle].rate*deg,sn=std::sin(e),cs=std::cos(e);
        if(cosine){s.v+=k.c*cs;s.r-=k.c*sn*de;}else{s.v+=k.c*sn;s.r+=k.c*cs*de;}}
}
Orientation wgccre(Center c,Tt t){
    constexpr double deg=3.14159265358979323846/180,arcsec=deg/3600;const double d=secondsSinceJ2000(t)/86400.;Series ra,dec,w;
    if(c==Center::moon){
        ra={269.9949+.0031*d/century,.0031/century};dec={66.5392+.0130*d/century,.0130/century};w={38.3213+13.17635815*d-1.4e-12*d*d,13.17635815-2.8e-12*d};
        add(ra,moonAngles,d,moonRa,false);add(dec,moonAngles,d,moonDec,true);add(w,moonAngles,d,moonW,false);
    }else{
        ra={317.269202-.10927547*d/century,-.10927547/century};dec={54.432516-.05827105*d/century,-.05827105/century};w={176.049863+350.891982443297*d,350.891982443297};
        add(ra,marsAngles,d,marsRa,false);add(dec,marsAngles,d,marsDec,true);add(w,marsAngles,d,marsW,false);
    }
    const double a=ra.v*deg,b=dec.v*deg;M3 m=rotationZ(std::fmod(w.v,360.)*deg)*rotationX(90*deg-b)*rotationZ(90*deg+a);
    // Mean Earth to principal axes: NAIF MOON_ME_DE440_ME421, V_PA = [67.8526"]3 [78.6944"]2 [0.2785"]1 V_ME.
    if(c==Center::moon)m=rotationZ(67.8526*arcsec)*rotationY(78.6944*arcsec)*rotationX(.2785*arcsec)*m;
    const V3 pole{std::cos(b)*std::cos(a),std::cos(b)*std::sin(a),std::sin(b)},node{-std::sin(a),std::cos(a),0};
    return{m,(V3{0,0,ra.r}-dec.r*node+w.r*pole)*(deg/86400.)}; // α̇ about z, then -δ̇ about the node, Ẇ about the pole.
}
}
Orientation Rotation::at(Tt t)const{
    if(body_!=Center::earth)return wgccre(body_,t);
    if(!iau_)return{rotationZ(angle_+rate_*secondsSinceJ2000(t-epoch_)),{0,0,rate_}};
    constexpr Tt bucket=60*second;constexpr Tt day=86400*second;
    const Tt k=t>=0?t/bucket:(t-bucket+1)/bucket;
    if(cached_!=k){
        for(int i=0;i<2;++i){double m[3][3];eraC2i06a(2451545.0,secondsSinceJ2000((k+i)*bucket)/86400.,m);
            for(int r=0;r<3;++r)for(int c=0;c<3;++c)(i?c2i1_:c2i_).m[r][c]=m[r][c];}
        cached_=k;
    }
    const double w=secondsSinceJ2000(t-k*bucket)/60.;M3 c2i;
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)c2i.m[r][c]=c2i_.m[r][c]+(c2i1_.m[r][c]-c2i_.m[r][c])*w;
    const Tt ut1=t-deltaT_,d=ut1>=0?ut1/day:(ut1-day+1)/day;
    const double era=eraEra00(2451545.0+static_cast<double>(d),secondsSinceJ2000(ut1-d*day)/86400.);
    constexpr double rate=2*3.14159265358979323846*1.00273781191135448/86400.;
    return{rotationZ(era)*c2i,transpose(c2i)*V3{0,0,rate}};
}
Planet planet(Center c,const Gravity* field,int degree){
    if(c==Center::earth)return earth;
    if(field&&(degree<0||degree>field->field().degree))throw std::invalid_argument("Gravity degree exceeds the field");
    constexpr double day=86400.,deg=3.14159265358979323846/180;
    Planet p=c==Center::moon?Planet{moonGM,1737400.,0.,13.17635815*deg/day,1737400.}:Planet{marsGM,3396190.,(3396190.-3376200.)/3396190.,350.89198226*deg/day,3396190.};
    if(field){p.gm=field->field().gm;p.gravityRadius=field->field().radius;p.degree=degree;p.field=field;}
    return p;
}
V3 marsHeliocentric(Tt t){
    double pv[2][3];if(eraPlan94(2451545.0,secondsSinceJ2000(t)/86400.,4,pv)<0)throw std::runtime_error("Mars ephemeris failed");
    constexpr double au=149597870700.;return{pv[0][0]*au,pv[0][1]*au,pv[0][2]*au};
}
int thirdBodies(Center c,Tt t,ThirdBody out[2]){
    if(c==Center::mars){out[0]={-marsHeliocentric(t),sunGM};return 1;}
    const Perturbers p=perturbers(t);
    if(c==Center::moon){out[0]={-p.moon,earthGM};out[1]={p.sun-p.moon,sunGM};return 2;}
    out[0]={p.sun,sunGM};out[1]={p.moon,moonGM};return 2;
}
Perturbers perturbers(Tt t){
    const double days=secondsSinceJ2000(t)/86400.;constexpr double au=149597870700.;
    double h[2][3],b[2][3],m[2][3];eraEpv00(2451545.0,days,h,b);eraMoon98(2451545.0,days,m);
    return{{-h[0][0]*au,-h[0][1]*au,-h[0][2]*au},{m[0][0]*au,m[0][1]*au,m[0][2]*au}};
}
}
