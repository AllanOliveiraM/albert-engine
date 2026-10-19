#include "physics/hwm14.hpp"
#include <algorithm>
#include <cmath>
// Port of NRL's hwm14.f90 (HWM14.123114). Statement order and the single precision of the
// disturbance model are kept so the results match the distribution's check output.
namespace albert::physics {
namespace {
#include "physics/hwm14_data.inc"
constexpr int levels=30,terms=800,nodes=36,order=3; // Quiet model: levels, basis terms, nnode = nlev + p, cubic B-splines.
constexpr double twoPi=2*3.1415926535897932384626433832795,deg2rad=twoPi/360;
// Associated Legendre functions P, V, W in the model's normalisation (alfbasis), degree ≤ 10, order ≤ 4.
constexpr int N=10,M=4;
struct Legendre {double p[N+1][M+1]{},v[N+1][M+1]{},w[N+1][M+1]{};};
struct Recursion {
    double anm[N+1][M+1]{},bnm[N+1][M+1]{},dnm[N+1][M+1]{},cm[M+1]{},en[N+1]{},marr[M+1]{},narr[N+1]{};
    Recursion(){
        for(long long n=1;n<=N;++n){narr[n]=double(n);en[n]=std::sqrt(double(n*(n+1)));
            anm[n][0]=std::sqrt(double((2*n-1)*(2*n+1)))/narr[n];bnm[n][0]=std::sqrt(double((2*n+1)*(n-1)*(n-1))/double(2*n-3))/narr[n];}
        for(long long m=1;m<=M;++m){marr[m]=double(m);cm[m]=std::sqrt(double(2*m+1)/double(2*m*m*(m+1)));
            for(long long n=m+1;n<=N;++n){
                anm[n][m]=std::sqrt(double((2*n-1)*(2*n+1)*(n-1))/double((n-m)*(n+m)*(n+1)));
                bnm[n][m]=std::sqrt(double((2*n+1)*(n+m-1)*(n-m-1)*(n-2)*(n-1))/double((n-m)*(n+m)*(2*n-3)*n*(n+1)));
                dnm[n][m]=std::sqrt(double((n-m)*(n+m)*(2*n+1)*(n-1))/double((2*n-1)*(n+1)));}}
    }
};
Legendre alfbasis(int nmax,int mmax,double theta){
    static const Recursion r;Legendre l;auto& P=l.p;auto& V=l.v;auto& W=l.w;
    P[0][0]=0.70710678118654746;const double x=std::cos(theta),y=std::sin(theta);
    for(int m=1;m<=mmax;++m){
        W[m][m]=r.cm[m]*P[m-1][m-1];P[m][m]=y*r.en[m]*W[m][m];
        for(int n=m+1;n<=nmax;++n){
            W[n][m]=r.anm[n][m]*x*W[n-1][m]-r.bnm[n][m]*W[n-2][m];P[n][m]=y*r.en[n]*W[n][m];
            V[n][m]=r.narr[n]*x*W[n][m]-r.dnm[n][m]*W[n-1][m];W[n-2][m]=r.marr[m]*W[n-2][m];}
        W[nmax-1][m]=r.marr[m]*W[nmax-1][m];W[nmax][m]=r.marr[m]*W[nmax][m];V[m][m]=x*W[m][m];
    }
    P[1][0]=r.anm[1][0]*x*P[0][0];V[1][0]=-P[1][1];
    for(int n=2;n<=nmax;++n){P[n][0]=r.anm[n][0]*x*P[n-1][0]-r.bnm[n][0]*P[n-2][0];V[n][0]=-P[n][1];}
    return l;
}
// Zonal (mparm, with the meridional-only terms cleared) and meridional (tparm) parameters, as initqwm's parity.
struct Quiet {
    double zonal[levels][terms],meridional[levels][terms];
    Quiet(){
        for(int l=0;l<levels;++l){double* m=zonal[l];double* t=meridional[l];std::copy(hwmParameters[l],hwmParameters[l]+terms,m);int c=0;
            for(int n=1;n<=8;++n){t[c]=0;t[c+1]=-m[c+1];m[c+1]=0;c+=2;}
            for(int s=1;s<=2;++s)for(int n=1;n<=8;++n){t[c]=0;t[c+1]=0;t[c+2]=-m[c+2];t[c+3]=-m[c+3];m[c+2]=0;m[c+3]=0;c+=4;}
            auto waves=[&](int k){
                for(int n=k;n<=8;++n){t[c]=m[c+2];t[c+1]=m[c+3];t[c+2]=-m[c];t[c+3]=-m[c+1];c+=4;}
                for(int s=1;s<=2;++s)for(int n=k;n<=8;++n){t[c]=m[c+2];t[c+1]=m[c+3];t[c+2]=-m[c];t[c+3]=-m[c+1];t[c+4]=m[c+6];t[c+5]=m[c+7];t[c+6]=-m[c+4];t[c+7]=-m[c+5];c+=8;}};
            for(int k=1;k<=2;++k)waves(k); // Stationary planetary waves.
            for(int k=1;k<=3;++k)waves(k); // Migrating tides.
        }
    }
};
double bspline(int i,double u){
    const double* V=hwmKnots;constexpr int p=order,m=nodes;
    if(i==0&&u==V[0])return 1;
    if(i==m-p-1&&u==V[m])return 1;
    if(u<V[i]||u>=V[i+p+1])return 0;
    double n[p+2]{};for(int j=0;j<=p;++j)n[j]=u>=V[i+j]&&u<V[i+j+1]?1:0;
    for(int k=1;k<=p;++k){
        double saved=n[0]==0?0:((u-V[i])*n[0])/(V[i+k]-V[i]);
        for(int j=0;j<=p-k;++j){const double left=V[i+j+1],right=V[i+j+k+1];
            if(n[j+1]==0){n[j]=saved;saved=0;}else{const double t=n[j+1]/(right-left);n[j]=saved+(right-u)*t;saved=(u-left)*t;}}
    }
    return n[0];
}
// Vertical weights of the four levels from the returned one (vertwght): B-splines up to 200 km,
// then relaxing to the constant top over a 60 km scale height.
int vertical(double alt,double w[4]){
    constexpr int n=nodes-order-1;const double* V=hwmKnots;int span;
    if(alt>=V[n+1])span=n;else{int low=order,high=n+1,mid=(low+high)/2;while(alt<V[mid]||alt>=V[mid+1]){if(alt<V[mid])high=mid;else low=mid;mid=(low+high)/2;}span=mid;}
    const int iz=std::min(span-order,26);
    w[0]=bspline(iz,alt);w[1]=bspline(iz+1,alt);
    if(iz<=25){w[2]=bspline(iz+2,alt);w[3]=bspline(iz+3,alt);return iz;}
    const double transition=V[31];double we[5]{};
    if(alt>transition){we[3]=std::exp(-(alt-transition)/60.);we[4]=1;}else{we[0]=bspline(iz+2,alt);we[1]=bspline(iz+3,alt);we[2]=bspline(iz+4,alt);}
    w[2]=w[3]=0;for(int k=0;k<5;++k){w[2]+=we[k]*hwmE1[k];w[3]+=we[k]*hwmE2[k];}
    return iz;
}
struct Wind {float north,east;};
Wind quiet(int day,float sec,float alt,float glat,float glon){
    static const Quiet q;
    const double lon=glon,theta=(90.-double(glat))*deg2rad;double fs[3][2],fl[4][2],fm[5][2];
    {const double a=double(day)*twoPi/365.25;for(int s=0;s<=2;++s){fs[s][0]=std::cos(s*a);fs[s][1]=std::sin(s*a);}}
    {const double a=std::fmod(double(sec)/3600.+lon/15.+48.,24.)*twoPi/24.;for(int l=0;l<=3;++l){fl[l][0]=std::cos(l*a);fl[l][1]=std::sin(l*a);}}
    {const double a=lon*deg2rad;for(int m=0;m<=4;++m){fm[m][0]=std::cos(m*a);fm[m][1]=std::sin(m*a);}}
    const Legendre g=alfbasis(8,4,theta);double bz[terms];int c=0;
    for(int n=1;n<=8;++n){bz[c]=-std::sin(n*theta);bz[c+1]=std::sin(n*theta);c+=2;}
    for(int s=1;s<=2;++s)for(int n=1;n<=8;++n){const double sc=std::sin(n*theta),cs=fs[s][0],ss=fs[s][1];bz[c]=-sc*cs;bz[c+1]=sc*ss;bz[c+2]=sc*cs;bz[c+3]=-sc*ss;c+=4;}
    auto waves=[&](int k,double ck,double sk){
        for(int n=k;n<=8;++n){const double vb=g.v[n][k],wb=g.w[n][k];bz[c]=-vb*ck;bz[c+1]=vb*sk;bz[c+2]=-wb*sk;bz[c+3]=-wb*ck;c+=4;}
        for(int s=1;s<=2;++s){const double cs=fs[s][0],ss=fs[s][1];for(int n=k;n<=8;++n){const double vb=g.v[n][k],wb=g.w[n][k];
            bz[c]=-vb*ck*cs;bz[c+1]=vb*sk*cs;bz[c+2]=-wb*sk*cs;bz[c+3]=-wb*ck*cs;bz[c+4]=-vb*ck*ss;bz[c+5]=vb*sk*ss;bz[c+6]=-wb*sk*ss;bz[c+7]=-wb*ck*ss;c+=8;}}};
    for(int m=1;m<=2;++m)waves(m,fm[m][0],fm[m][1]);
    for(int l=1;l<=3;++l)waves(l,fl[l][0],fl[l][1]);
    double w[4];const int lev=vertical(std::max(double(alt),0.),w);double u=0,v=0;
    for(int b=0;b<=order;++b){if(w[b]==0)continue;const double* zm=q.zonal[lev+b];const double* tm=q.meridional[lev+b];double du=0,dv=0;
        for(int i=0;i<terms;++i){du+=bz[i]*zm[i];dv+=bz[i]*tm[i];}u+=w[b]*du;v+=w[b]*dv;}
    return{float(v),float(u)};
}
float ap2kp(float ap0){
    static constexpr float grid[28]{0,2,3,4,5,6,7,9,12,15,18,22,27,32,39,48,56,67,80,94,111,132,154,179,207,236,300,400};
    auto kp=[](int i){return float(i)/3.0f;};
    float ap=ap0;if(ap<0)ap=0;if(ap>400)ap=400;
    int i=1;while(ap>grid[i])++i;
    return ap==grid[i]?kp(i):kp(i-1)+(ap-grid[i-1])/(3.0f*(grid[i]-grid[i-1]));
}
void kpspl3(float kp,float out[3]){
    static constexpr float node[8]{-10,-8,0,2,5,8,18,20};
    const float x=std::min(std::max(kp,0.f),8.f);float s[7];
    for(int i=0;i<=6;++i)s[i]=x>=node[i]&&x<node[i+1]?1.f:0.f;
    for(int j=2;j<=3;++j)for(int i=0;i<=8-j-1;++i)s[i]=s[i]*(x-node[i])/(node[i+j-1]-node[i])+s[i+1]*(node[i+j]-x)/(node[i+j]-node[i+1]);
    out[0]=s[0]+s[1];out[1]=s[2];out[2]=s[3]+s[4];
}
float latwgt2(float mlat,float mlt,float kp0,float width){
    static constexpr float c[6]{65.7633f,-4.60256f,-3.53915f,-1.99971f,-0.752193f,0.972388f};
    constexpr float pi=3.141592653590f,dtor=float(double(pi)/180.);
    const float r=mlt*15.0f*dtor,s=std::sin(r),co=std::cos(r),kp=std::min(std::max(kp0,0.f),8.f);
    const float tlat=c[0]+c[1]*co+c[2]*s+kp*(c[3]+c[4]*co+c[5]*s);
    return 1.0f/(1+std::exp(-(std::abs(mlat)-tlat)/width));
}
// Spherical harmonics of the quasi-dipole expansion (gd2qd, mltcalc): 61 terms to degree 8, order 4.
constexpr double qdDtor=3.1415926535897932/180.;
void harmonics(const Legendre& g,double phi,double sh[61],double* gt=nullptr,double* gp=nullptr){
    int i=0;
    for(int n=0;n<=8;++n){sh[i]=g.p[n][0];if(gt){gt[i]=g.v[n][0]*std::sqrt(double(n*(n+1)));gp[i]=0;}++i;}
    for(int m=1;m<=4;++m){const double cm=std::cos(m*phi),sm=std::sin(m*phi);
        for(int n=m;n<=8;++n){sh[i]=g.p[n][m]*cm;sh[i+1]=g.p[n][m]*sm;
            if(gt){const double k=std::sqrt(double(n*(n+1)));gt[i]=g.v[n][m]*k*cm;gt[i+1]=g.v[n][m]*k*sm;gp[i]=-g.w[n][m]*k*sm;gp[i+1]=g.w[n][m]*k*cm;}
            i+=2;}}
}
double dot61(const double* a,const double* b){double s=0;for(int i=0;i<61;++i)s+=a[i]*b[i];return s;}
struct QuasiDipole {float lat,lon,f1e,f1n,f2e,f2n;};
QuasiDipole gd2qd(float glat,float glon){
    double sh[61],gt[61],gp[61];harmonics(alfbasis(8,4,(90.-double(glat))*qdDtor),double(glon)*qdDtor,sh,gt,gp);
    const double* X=qdCoefficients[0];const double* Y=qdCoefficients[1];const double* Z=qdCoefficients[2];
    const double x=dot61(sh,X),y=dot61(sh,Y),z=dot61(sh,Z),lon=std::atan2(y,x),cl=std::cos(lon),sl=std::sin(lon),cosLat=x*cl+y*sl;
    const double xt=dot61(gt,X),yt=dot61(gt,Y),zt=dot61(gt,Z),xp=dot61(gp,X),yp=dot61(gp,Y),zp=dot61(gp,Z);
    return{float(std::atan2(z,cosLat)/qdDtor),float(lon/qdDtor),float(-zt*cosLat+(xt*cl+yt*sl)*z),float(-zp*cosLat+(xp*cl+yp*sl)*z),float(yt*cl-xt*sl),float(yp*cl-xp*sl)};
}
float mltcalc(float qlon,float day,float ut){ // From the quasi-dipole longitude of the anti-solar point.
    const double lat=-std::asin(std::sin((double(day)+double(ut)/24.-80.)*qdDtor)*0.39781868)/qdDtor,lon=-double(ut)*15.;
    double sh[61];harmonics(alfbasis(8,4,(90.-lat)*qdDtor),lon*qdDtor,sh);
    const float antisolar=float(std::atan2(dot61(sh,qdCoefficients[1]),dot61(sh,qdCoefficients[0]))/qdDtor);
    return (qlon-antisolar)/15.0f;
}
Wind disturbanceQd(float mlt,float mlat,float kp){
    constexpr double dtor=double(3.1415926535897932f)/180.; // The Fortran's pi here is a single precision literal.
    const Legendre d=alfbasis(10,3,(90.-double(mlat))*dtor);const double phi=double(mlt)*dtor*15.;double t[4][2];
    for(int m=0;m<=3;++m){t[m][0]=std::cos(m*phi);t[m][1]=std::sin(m*phi);}
    float vsh[2][128];int k=0;
    for(int n=1;n<=10;++n){
        vsh[0][k]=-float(d.v[n][0]*t[0][0]);vsh[0][k+1]=float(d.w[n][0]*t[0][0]);vsh[1][k]=-vsh[0][k+1];vsh[1][k+1]=vsh[0][k];k+=2;
        for(int m=1;m<=std::min(3,n);++m){
            vsh[0][k]=-float(d.v[n][m]*t[m][0]);vsh[0][k+1]=float(d.v[n][m]*t[m][1]);vsh[0][k+2]=float(d.w[n][m]*t[m][1]);vsh[0][k+3]=float(d.w[n][m]*t[m][0]);
            vsh[1][k]=-vsh[0][k+2];vsh[1][k+1]=-vsh[0][k+3];vsh[1][k+2]=vsh[0][k];vsh[1][k+3]=vsh[0][k+1];k+=4;}
    }
    float kpt[3];kpspl3(kp,kpt);const float mask=latwgt2(mlat,mlt,kp,dwmWidth);float mer=0,zon=0;
    for(int i=0;i<300;++i){float a=1,b=1;const auto& term=dwmTerms[i];
        if(term[0]!=999){a*=vsh[0][term[0]];b*=vsh[1][term[0]];}
        if(term[1]!=999){a*=kpt[term[1]];b*=kpt[term[1]];}
        if(term[2]!=999){a*=mask;b*=mask;}
        mer+=dwmCoefficients[i]*a;zon+=dwmCoefficients[i]*b;}
    return{mer,zon};
}
Wind disturbance(int day,float sec,float alt,float glat,float glon,float ap){
    const float kp=ap2kp(ap);const QuasiDipole q=gd2qd(glat,glon);const float ut=sec/3600.0f;
    const Wind m=disturbanceQd(mltcalc(q.lon,float(day),ut),q.lat,kp);const float h=1+std::exp(-(alt-125.0f)/dwmWidth);
    return{(q.f2n*m.north+q.f1n*m.east)/h,(q.f2e*m.north+q.f1e*m.east)/h};
}
bool finite(double a,double b,double c,double d,double e){return std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c)&&std::isfinite(d)&&std::isfinite(e);}
}
HorizontalWind hwm14(int day,double sec,double alt,double lat,double lon,double ap){
    if(!finite(sec,alt,lat,lon,ap))return{};
    Wind w=quiet(day,float(sec),float(alt),float(lat),float(lon));
    if(ap>=0){const Wind d=disturbance(day,float(sec),float(alt),float(lat),float(lon),float(ap));w.north+=d.north;w.east+=d.east;}
    return{w.north,w.east};
}
HorizontalWind dwm07(int day,double sec,double alt,double lat,double lon,double ap){
    if(!finite(sec,alt,lat,lon,ap))return{};
    const Wind d=disturbance(day,float(sec),float(alt),float(lat),float(lon),float(ap));return{d.north,d.east};
}
HorizontalWind dwm07b(double mlt,double mlat,double kp){
    if(!finite(mlt,mlat,kp,0,0))return{};
    const Wind d=disturbanceQd(float(mlt),float(mlat),float(kp));return{d.north,d.east};
}
}
