#include "physics/earth.hpp"
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
struct Field {std::array<double,packed(egm2008Degree+1,0)> c{},s{};}; // De-normalised.
const Field& field(){
    static const Field f=[]{Field r;
        for(int n=0;n<=egm2008Degree;++n)for(int m=0;m<=n;++m){
            double q=(m?2.:1.)*(2*n+1);for(int k=n-m+1;k<=n+m;++k)q/=k;const double k=std::sqrt(q);
            r.c[packed(n,m)]=egm2008[packed(n,m)][0]*k;r.s[packed(n,m)]=egm2008[packed(n,m)][1]*k;}
        r.c[0]=1;return r;}();
    return f;
}
}
V3 harmonics(int degree,V3 r){ // Montenbruck & Gill, Satellite Orbits §3.2.
    if(degree<0||degree>egm2008Degree)throw std::invalid_argument("EGM2008 degree out of range");
    constexpr double gm=3.986004415e14,a=6378136.3;const Field& f=field();
    const int top=degree+1;const double r2=dot(r,r),rho=a*a/r2,x0=a*r.x/r2,y0=a*r.y/r2,z0=a*r.z/r2;
    std::array<double,packed(egm2008Degree+2,0)> v{},w{};
    v[0]=a/std::sqrt(r2);
    for(int m=0;m<=top;++m){
        if(m){const double vm=v[packed(m-1,m-1)],wm=w[packed(m-1,m-1)];v[packed(m,m)]=(2*m-1)*(x0*vm-y0*wm);w[packed(m,m)]=(2*m-1)*(x0*wm+y0*vm);}
        if(m<top){v[packed(m+1,m)]=(2*m+1)*z0*v[packed(m,m)];w[packed(m+1,m)]=(2*m+1)*z0*w[packed(m,m)];}
        for(int n=m+2;n<=top;++n){
            v[packed(n,m)]=((2*n-1)*z0*v[packed(n-1,m)]-(n+m-1)*rho*v[packed(n-2,m)])/(n-m);
            w[packed(n,m)]=((2*n-1)*z0*w[packed(n-1,m)]-(n+m-1)*rho*w[packed(n-2,m)])/(n-m);
        }
    }
    V3 acc;
    for(int n=degree;n>=0;--n)for(int m=n;m>=0;--m){
        const double c=f.c[packed(n,m)],s=f.s[packed(n,m)];
        if(m==0){acc.x-=c*v[packed(n+1,1)];acc.y-=c*w[packed(n+1,1)];}
        else{const double k=(n-m+2)*(n-m+1);
            acc.x+=.5*(-c*v[packed(n+1,m+1)]-s*w[packed(n+1,m+1)]+k*(c*v[packed(n+1,m-1)]+s*w[packed(n+1,m-1)]));
            acc.y+=.5*(-c*w[packed(n+1,m+1)]+s*v[packed(n+1,m+1)]+k*(-c*w[packed(n+1,m-1)]+s*v[packed(n+1,m-1)]));}
        acc.z+=(n-m+1)*(-c*v[packed(n+1,m)]-s*w[packed(n+1,m)]);
    }
    return acc*(gm/(a*a));
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
