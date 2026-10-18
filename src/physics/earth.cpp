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
Orientation Rotation::at(Tt t)const{
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
Perturbers perturbers(Tt t){
    const double days=secondsSinceJ2000(t)/86400.;constexpr double au=149597870700.;
    double h[2][3],b[2][3],m[2][3];eraEpv00(2451545.0,days,h,b);eraMoon98(2451545.0,days,m);
    return{{-h[0][0]*au,-h[0][1]*au,-h[0][2]*au},{m[0][0]*au,m[0][1]*au,m[0][2]*au}};
}
}
