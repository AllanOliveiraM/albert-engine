// Port of the double-precision NRLMSISE-00 in Orekit (NRLMSISE00.java, Apache License 2.0,
// CS GROUP), itself following the NRL reference (Picone, Hedin, Drob; U.S. Government work).
#include "physics/msis.hpp"
#include "physics/earth.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace albert::physics {
namespace {
#include "msis_data.inc"
constexpr double DGTR=1.74533e-2,DR=1.72142e-2,HR=.2618,SR=7.2722e-5,LAT_REF=45,G_REF=980.616,AMU=1.66e-27,RGAS=831.4,FLUX_REF=150,MIN_TEMP=50;
constexpr double MH=1,MHE=4,MN=14,MN2=28,MO=16,MO2=32,MAR=40;
constexpr double ZN1[5]{123.435,110.0,100.0,90.0,72.5},ZN2[4]{72.5,55.0,45.0,32.5},ZN3[5]{32.5,20.0,15.0,10.0,0.0},ZMIX=62.5;
enum {HE,O,N2,O2,AR,MASS,H,N,ANO};
template<int M>using A=std::array<double,M>;
template<int M>A<M> spline(const A<M>& x,const A<M>& y,double yp1,double ypn){
    A<M> y2{},u{};
    if(yp1<1e30){y2[0]=-.5;u[0]=(3/(x[1]-x[0]))*((y[1]-y[0])/(x[1]-x[0])-yp1);}
    for(int i=1;i<M-1;++i){const double sig=(x[i]-x[i-1])/(x[i+1]-x[i-1]),p=sig*y2[i-1]+2;y2[i]=(sig-1)/p;
        u[i]=(6*((y[i+1]-y[i])/(x[i+1]-x[i])-(y[i]-y[i-1])/(x[i]-x[i-1]))/(x[i+1]-x[i-1])-sig*u[i-1])/p;}
    double qn=0,un=0;if(ypn<1e30){qn=.5;un=(3/(x[M-1]-x[M-2]))*(ypn-(y[M-1]-y[M-2])/(x[M-1]-x[M-2]));}
    y2[M-1]=(un-qn*u[M-2])/(qn*y2[M-2]+1);for(int k=M-2;k>=0;--k)y2[k]=y2[k]*y2[k+1]+u[k];
    return y2;
}
template<int M>double splint(const A<M>& xa,const A<M>& ya,const A<M>& y2a,double x){
    int lo=0,hi=M-1;while(hi-lo>1){const int k=(hi+lo)/2;if(xa[k]>x)hi=k;else lo=k;}
    const double h=xa[hi]-xa[lo],a=(xa[hi]-x)/h,b=(x-xa[lo])/h;
    return a*ya[lo]+b*ya[hi]+((a*a*a-a)*y2a[lo]+(b*b*b-b)*y2a[hi])*h*h/6;
}
template<int M>double splini(const A<M>& xa,const A<M>& ya,const A<M>& y2a,double x){
    double yi=0;
    for(int lo=0,hi=1;x>xa[lo]&&hi<M;++lo,++hi){
        const double xx=hi<M-1?std::min(x,xa[hi]):x,h=xa[hi]-xa[lo],a=(xa[hi]-xx)/h,b=(xx-xa[lo])/h,a2=a*a,b2=b*b;
        yi+=((1-a2)*ya[lo]/2+b2*ya[hi]/2+((-(1+a2*a2)/4+a2/2)*y2a[lo]+(b2*b2/4-b2/2)*y2a[hi])*h*h/6)*h;
    }
    return yi;
}
struct Model {
    int doy;double sec,lat,lon,hl,f107a,f107;A<7> ap;int sw[24],swc[24];
    double glat,rlat,dm28=0,plg[4][8]{},ctloc=0,stloc=0,c2tloc=0,s2tloc=0,c3tloc=0,s3tloc=0,apdf=0,apt=0;
    A<5> tn1{};A<4> tn2{};A<5> tn3{};double tgn1[2]{},tgn2[2]{},tgn3[2]{};A<9> d{};double t[2]{};
    Model(int day,double s,double la,double lo,double lst,const SpaceWeather& w):doy(day),sec(s),lat(la),lon(lo),hl(lst),f107a(w.f107a),f107(w.f107),ap(w.ap){
        std::fill(std::begin(sw),std::end(sw),1);std::fill(std::begin(swc),std::end(swc),1);if(w.history)sw[9]=-1;
        const double c2=std::cos(2*DGTR*lat);glat=G_REF*(1-.0026373*c2);rlat=2*glat/(3.085462e-6+2.27e-9*c2)*1e-5;
        const double c=std::sin(DGTR*lat),s0=std::cos(DGTR*lat);auto& p=plg;
        p[0][1]=c;p[0][2]=(3*c*p[0][1]-1)/2;p[0][3]=(5*c*p[0][2]-2*p[0][1])/3;p[0][4]=(7*c*p[0][3]-3*p[0][2])/4;p[0][5]=(9*c*p[0][4]-4*p[0][3])/5;p[0][6]=(11*c*p[0][5]-5*p[0][4])/6;
        p[1][1]=s0;p[1][2]=3*c*p[1][1];p[1][3]=(5*c*p[1][2]-3*p[1][1])/2;p[1][4]=(7*c*p[1][3]-4*p[1][2])/3;p[1][5]=(9*c*p[1][4]-5*p[1][3])/4;p[1][6]=(11*c*p[1][5]-6*p[1][4])/5;
        p[2][2]=3*s0*p[1][1];p[2][3]=5*c*p[2][2];p[2][4]=(7*c*p[2][3]-5*p[2][2])/2;p[2][5]=(9*c*p[2][4]-6*p[2][3])/3;p[2][6]=(11*c*p[2][5]-7*p[2][4])/4;p[2][7]=(13*c*p[2][6]-8*p[2][5])/5;
        p[3][3]=5*s0*p[2][2];p[3][4]=7*c*p[3][3];p[3][5]=(9*c*p[3][4]-7*p[3][3])/2;p[3][6]=(11*c*p[3][5]-8*p[3][4])/3;
        const double tl=HR*hl;stloc=std::sin(tl);ctloc=std::cos(tl);s2tloc=std::sin(2*tl);c2tloc=std::cos(2*tl);s3tloc=std::sin(3*tl);c3tloc=std::cos(3*tl);
    }
    double galt(double alt)const{const double r=1+alt/rlat;return glat/(r*r);}
    double zeta(double zz,double zl)const{return(zz-zl)*(rlat+zl)/(rlat+zz);}
    double scalh(double alt,double xm,double temp)const{return RGAS*temp/(galt(alt)*xm);}
    static double ccor(double alt,double r,double h1,double zh){const double e=(alt-zh)/h1;return e>70?1.:e<-70?std::exp(r):std::exp(r/(1+std::exp(e)));}
    static double ccor2(double alt,double r,double h1,double zh,double h2){const double e1=(alt-zh)/h1,e2=(alt-zh)/h2;
        if(e1>70||e2>70)return 1;if(e1<-70&&e2<-70)return std::exp(r);return std::exp(r/(1+.5*(std::exp(e1)+std::exp(e2))));}
    static double dnet(double dd,double dm,double zhm,double xmm,double xm){
        if(!(dm>0&&dd>0)){if(dd==0&&dm==0)dd=1;if(dm==0)return dd;if(dd==0)return dm;}
        const double a=zhm/(xmm-xm),y=a*std::log(dm/dd);return y<-10?dd:y>10?dm:dd*std::pow(1+std::exp(y),1/a);
    }
    double g0(double a,double p24,double p25)const{const double m=a-4;return m+(p25-1)*(m+(std::exp(-p24*m)-1)/p24);}
    double sg0(double ex,double p24,double p25)const{
        const double g[6]{g0(ap[1],p24,p25),g0(ap[2],p24,p25),g0(ap[3],p24,p25),g0(ap[4],p24,p25),g0(ap[5],p24,p25),g0(ap[6],p24,p25)};
        const double ex2=ex*ex,ex3=ex*ex2,ex4=ex2*ex2,ex8=ex4*ex4,ex12=ex4*ex8,g234=g[1]*ex+g[2]*ex2+g[3]*ex3,g56=g[4]*ex4+g[5]*ex12,ex19=ex3*ex4*ex12,om=1-ex;
        return(g[0]+(g234+g56*(1-ex8)/om))/(1+(1-ex19)/om*std::sqrt(ex));
    }
    double globe7(const double* p){
        double tt[14]{};const auto& g=plg;
        const double cd32=std::cos(DR*(doy-p[31])),cd18=std::cos(2*DR*(doy-p[17])),cd14=std::cos(DR*(doy-p[13])),cd39=std::cos(2*DR*(doy-p[38]));
        const double df=f107-f107a,dfa=f107a-FLUX_REF;
        tt[0]=p[19]*df*(1+p[59]*dfa)+p[20]*df*df+p[21]*dfa+p[29]*dfa*dfa;
        const double f1=1+(p[47]*dfa+p[19]*df+p[20]*df*df)*swc[1],f2=1+(p[49]*dfa+p[19]*df+p[20]*df*df)*swc[1];
        tt[1]=(p[1]*g[0][2]+p[2]*g[0][4]+p[22]*g[0][6])+(p[14]*g[0][2])*dfa*swc[1]+p[26]*g[0][1];
        tt[2]=p[18]*cd32;tt[3]=(p[15]+p[16]*g[0][2])*cd18;tt[4]=f1*(p[9]*g[0][1]+p[10]*g[0][3])*cd14;tt[5]=p[37]*g[0][1]*cd39;
        if(sw[7]){const double t71=(p[11]*g[1][2])*cd14*swc[5],t72=(p[12]*g[1][2])*cd14*swc[5];
            tt[6]=f2*((p[3]*g[1][1]+p[4]*g[1][3]+p[27]*g[1][5]+t71)*ctloc+(p[6]*g[1][1]+p[7]*g[1][3]+p[28]*g[1][5]+t72)*stloc);}
        if(sw[8]){const double t81=(p[23]*g[2][3]+p[35]*g[2][5])*cd14*swc[5],t82=(p[33]*g[2][3]+p[36]*g[2][5])*cd14*swc[5];
            tt[7]=f2*((p[5]*g[2][2]+p[41]*g[2][4]+t81)*c2tloc+(p[8]*g[2][2]+p[42]*g[2][4]+t82)*s2tloc);}
        if(sw[14])tt[13]=f2*((p[39]*g[3][3]+(p[93]*g[3][4]+p[46]*g[3][6])*cd14*swc[5])*s3tloc+(p[40]*g[3][3]+(p[94]*g[3][4]+p[48]*g[3][6])*cd14*swc[5])*c3tloc);
        if(sw[9]==-1){
            if(p[51]!=0){const double exp1=std::exp(-10800*std::abs(p[51])/(1+p[138]*(LAT_REF-std::abs(lat))));
                apt=sg0(std::min(exp1,.99999),std::max(p[24],1e-4),p[25]);
                tt[8]=apt*(p[50]+p[96]*g[0][2]+p[54]*g[0][4]+(p[125]*g[0][1]+p[126]*g[0][3]+p[127]*g[0][5])*cd14*swc[5]+(p[128]*g[1][1]+p[129]*g[1][3]+p[130]*g[1][5])*swc[7]*std::cos(HR*(hl-p[131])));}
        }else{
            const double apd=ap[0]-4,p44=p[43]<0?1e-5:p[43],p45=p[44];apdf=apd+(p45-1)*(apd+(std::exp(-p44*apd)-1)/p44);
            if(sw[9])tt[8]=apdf*(p[32]+p[45]*g[0][2]+p[34]*g[0][4]+(p[100]*g[0][1]+p[101]*g[0][3]+p[102]*g[0][5])*cd14*swc[5]+(p[121]*g[1][1]+p[122]*g[1][3]+p[123]*g[1][5])*swc[7]*std::cos(HR*(hl-p[124])));
        }
        if(sw[10]){
            const double lonr=DGTR*lon,cl=std::cos(lonr),sl=std::sin(lonr);
            if(sw[11])tt[10]=(1+p[80]*dfa*swc[1])*((p[64]*g[1][2]+p[65]*g[1][4]+p[66]*g[1][6]+p[103]*g[1][1]+p[104]*g[1][3]+p[105]*g[1][5]+(p[109]*g[1][1]+p[110]*g[1][3]+p[111]*g[1][5])*swc[5]*cd14)*cl
                +(p[90]*g[1][2]+p[91]*g[1][4]+p[92]*g[1][6]+p[106]*g[1][1]+p[107]*g[1][3]+p[108]*g[1][5]+(p[112]*g[1][1]+p[113]*g[1][3]+p[114]*g[1][5])*swc[5]*cd14)*sl);
            if(sw[12]){tt[11]=(1+p[95]*g[0][1])*(1+p[81]*dfa*swc[1])*(1+p[119]*g[0][1]*swc[5]*cd14)*(p[68]*g[0][1]+p[69]*g[0][3]+p[70]*g[0][5])*std::cos(SR*(sec-p[71]));
                tt[11]+=swc[11]*(1+p[137]*dfa*swc[1])*(p[76]*g[2][3]+p[77]*g[2][5]+p[78]*g[2][7])*std::cos(SR*(sec-p[79])+2*lonr);}
            if(sw[13]){
                if(sw[9]==-1){if(p[51]!=0)tt[12]=apt*swc[11]*(1+p[132]*g[0][1])*(p[52]*g[1][2]+p[98]*g[1][4]+p[67]*g[1][6])*std::cos(DGTR*(lon-p[97]))
                    +apt*swc[11]*swc[5]*cd14*(p[133]*g[1][1]+p[134]*g[1][3]+p[135]*g[1][5])*std::cos(DGTR*(lon-p[136]))+apt*swc[12]*(p[55]*g[0][1]+p[56]*g[0][3]+p[57]*g[0][5])*std::cos(SR*(sec-p[58]));}
                else tt[12]=apdf*swc[11]*(1+p[120]*g[0][1])*((p[60]*g[1][2]+p[61]*g[1][4]+p[62]*g[1][6])*std::cos(DGTR*(lon-p[63])))
                    +apdf*swc[11]*swc[5]*cd14*(p[115]*g[1][1]+p[116]*g[1][3]+p[117]*g[1][5])*std::cos(DGTR*(lon-p[118]))+apdf*swc[12]*(p[83]*g[0][1]+p[84]*g[0][3]+p[85]*g[0][5])*std::cos(SR*(sec-p[75]));
            }
        }
        double tinf=p[30];for(int i=0;i<14;++i)tinf+=std::abs(sw[i+1])*tt[i];
        return tinf;
    }
    double glob7s(const double* p)const{
        double tt[14]{};const auto& g=plg;
        const double cd32=std::cos(DR*(doy-p[31])),cd18=std::cos(2*DR*(doy-p[17])),cd14=std::cos(DR*(doy-p[13])),cd39=std::cos(2*DR*(doy-p[38]));
        tt[0]=p[21]*(f107a-FLUX_REF);tt[1]=p[1]*g[0][2]+p[2]*g[0][4]+p[22]*g[0][6]+p[26]*g[0][1]+p[14]*g[0][3]+p[59]*g[0][5];
        tt[2]=(p[18]+p[47]*g[0][2]+p[29]*g[0][4])*cd32;tt[3]=(p[15]+p[16]*g[0][2]+p[30]*g[0][4])*cd18;tt[4]=(p[9]*g[0][1]+p[10]*g[0][3]+p[20]*g[0][5])*cd14;tt[5]=(p[37]*g[0][1])*cd39;
        if(sw[7]){const double t71=p[11]*g[1][2]*cd14*swc[5],t72=p[12]*g[1][2]*cd14*swc[5];tt[6]=(p[3]*g[1][1]+p[4]*g[1][3]+t71)*ctloc+(p[6]*g[1][1]+p[7]*g[1][3]+t72)*stloc;}
        if(sw[8]){const double t81=(p[23]*g[2][3]+p[35]*g[2][5])*cd14*swc[5],t82=(p[33]*g[2][3]+p[36]*g[2][5])*cd14*swc[5];tt[7]=(p[5]*g[2][2]+p[41]*g[2][4]+t81)*c2tloc+(p[8]*g[2][2]+p[42]*g[2][4]+t82)*s2tloc;}
        if(sw[14])tt[13]=p[39]*g[3][3]*s3tloc+p[40]*g[3][3]*c3tloc;
        if(sw[9]==1)tt[8]=apdf*(p[32]+p[45]*g[0][2]*swc[2]);else if(sw[9]==-1)tt[8]=apt*(p[50]+p[96]*g[0][2]*swc[2]);
        if(sw[10]&&sw[11]){const double lonr=DGTR*lon;
            tt[10]=(1+g[0][1]*(p[80]*swc[5]*std::cos(DR*(doy-p[81]))+p[85]*swc[6]*std::cos(2*DR*(doy-p[86])))+p[83]*swc[3]*std::cos(DR*(doy-p[84]))+p[87]*swc[4]*std::cos(2*DR*(doy-p[88])))
                *((p[64]*g[1][2]+p[65]*g[1][4]+p[66]*g[1][6]+p[74]*g[1][1]+p[75]*g[1][3]+p[76]*g[1][5])*std::cos(lonr)+(p[90]*g[1][2]+p[91]*g[1][4]+p[92]*g[1][6]+p[77]*g[1][1]+p[78]*g[1][3]+p[79]*g[1][5])*std::sin(lonr));}
        double gl=0;for(int i=0;i<14;++i)gl+=std::abs(sw[i+1])*tt[i];
        return gl;
    }
    template<int M>double profile(const double* zn,A<M>& tn,const double* tgn,double z,double xm,double& density){ // densm segment at height z.
        const double z1=zn[0],z2=zn[M-1],t1=tn[0],t2=tn[M-1],zgdif=zeta(z2,z1);A<M> xs,ys;
        for(int k=0;k<M;++k){xs[k]=zeta(zn[k],z1)/zgdif;ys[k]=1/tn[k];}
        const double q=(rlat+z2)/(rlat+z1);const auto s=spline<M>(xs,ys,-tgn[0]/(t1*t1)*zgdif,-tgn[1]/(t2*t2)*zgdif*q*q);
        const double x=zeta(z,z1)/zgdif,tz=1/splint<M>(xs,ys,s,x);
        if(xm!=0)density*=(t1/tz)*std::exp(-std::min(MIN_TEMP,xm*galt(z1)*zgdif/RGAS*splini<M>(xs,ys,s,x)));
        return tz;
    }
    double densm(double alt,double d0,double xm){
        double density=d0;double tz=profile<4>(ZN2,tn2,tgn2,std::max(alt,ZN2[3]),xm,density);
        if(alt>ZN3[0])return xm==0?tz:density;
        tz=profile<5>(ZN3,tn3,tgn3,alt,xm,density);return xm==0?tz:density;
    }
    double densu(double alt,double dlb,double tinf,double tlb,double xm,double alpha,double zlb,double s2){
        const double zg2=zeta(std::max(alt,ZN1[0]),zlb),tt=tinf-(tinf-tlb)*std::exp(-s2*zg2);double tz=tt;
        A<5> xs{},ys{},y2{};double x=0,zgdif=0;
        if(alt<ZN1[0]){
            const double p=(rlat+zlb)/(rlat+ZN1[0]);tgn1[0]=(tinf-tt)*s2*p*p;tn1[0]=tt;
            const double t1=tn1[0],t2=tn1[4],zg=zeta(std::max(alt,ZN1[4]),ZN1[0]);zgdif=zeta(ZN1[4],ZN1[0]);
            for(int k=0;k<5;++k){xs[k]=zeta(ZN1[k],ZN1[0])/zgdif;ys[k]=1/tn1[k];}
            const double q=(rlat+ZN1[4])/(rlat+ZN1[0]);y2=spline<5>(xs,ys,-tgn1[0]/(t1*t1)*zgdif,-tgn1[1]/(t2*t2)*zgdif*q*q);
            x=zg/zgdif;tz=1/splint<5>(xs,ys,y2,x);
        }
        if(xm==0)return tz;
        double gamma=xm*galt(zlb)/(RGAS*s2*tinf),expl=tt<=0?MIN_TEMP:std::min(MIN_TEMP,std::exp(-s2*gamma*zg2));
        double out=dlb*expl*std::pow(tlb/tt,1+alpha+gamma);
        if(!std::isfinite(out)){if(expl<MIN_TEMP)out=dlb*std::exp(std::log(tlb/tt)*(1+alpha+gamma)-s2*gamma*zg2);else throw std::runtime_error("Infinite NRLMSISE-00 density");}
        if(alt<ZN1[0]){gamma=xm*galt(ZN1[0])*zgdif/RGAS;expl=tz<=0?MIN_TEMP:std::min(MIN_TEMP,gamma*splini<5>(xs,ys,y2,x));out*=std::pow(tn1[0]/tz,1+alpha)*std::exp(-expl);}
        return out;
    }
    double mass()const{return AMU*(MHE*d[HE]+MO*d[O]+MN2*d[N2]+MO2*d[O2]+MAR*d[AR]+MH*d[H]+MN*d[N]);}
    void gts7(double alt){
        constexpr double alpha[9]{-.38,0,0,0,.17,0,-.38,0,0},altl[8]{200,300,160,250,240,450,320,450};
        const double xmm=PDM[2][4];
        double tinf=PTM[0]*PT[0];if(alt>ZN1[0])tinf*=1+sw[16]*globe7(PT);t[0]=tinf;
        double g0v=PTM[3]*PS[0];if(alt>ZN1[4])g0v*=1+sw[19]*globe7(PS);
        double tlb=PTM[1]*PD[3][0];tlb*=1+sw[17]*globe7(PD[3]);const double s=g0v/(tinf-tlb);
        tn1[1]=PTM[6]*PTL[0][0];tn1[2]=PTM[2]*PTL[1][0];tn1[3]=PTM[7]*PTL[2][0];tn1[4]=PTM[4]*PTL[3][0];tgn1[1]=PTM[8]*PMA[8][0];
        if(alt<300){const double r=PTM[4]*PTL[3][0];
            tn1[1]/=1-sw[18]*glob7s(PTL[0]);tn1[2]/=1-sw[18]*glob7s(PTL[1]);tn1[3]/=1-sw[18]*glob7s(PTL[2]);tn1[4]/=1-sw[18]*sw[20]*glob7s(PTL[3]);
            tgn1[1]*=1+sw[18]*sw[20]*glob7s(PMA[8]);tgn1[1]*=tn1[4]*tn1[4]/(r*r);}
        t[1]=densu(alt,1,tinf,tlb,0,0,PTM[5],s);
        const double db28=PDM[2][0]*std::exp(sw[21]*globe7(PD[2]))*PD[2][0];
        d[N2]=densu(alt,db28,tinf,tlb,MN2,alpha[2],PTM[5],s);
        const double zhf=PDL[1][24]*(1+sw[5]*PDL[0][24]*std::sin(DGTR*lat)*std::cos(DR*(doy-PT[13]))),zh28=PDM[2][2]*zhf,zhm28=PDM[2][3]*PDL[1][5];
        const double b28=densu(zh28,db28,tinf,tlb,MN2-xmm,alpha[2]-1,PTM[5],s);
        if(sw[15]&&alt<=altl[2]){dm28=densu(alt,b28,tinf,tlb,xmm,alpha[2],PTM[5],s);d[N2]=dnet(d[N2],dm28,zhm28,xmm,MN2);}
        // Species below their turbopause blend into the mixed profile with chemistry/flux corrections.
        auto species=[&](int i,int pd,int pdm,double m,double a,double limit,auto correct){
            const double db=PDM[pdm][0]*std::exp(sw[21]*globe7(PD[pd]))*PD[pd][0];double dd=densu(alt,db,tinf,tlb,m,a,PTM[5],s);
            if(sw[15]&&alt<=limit){const double b=densu(PDM[pdm][2],db,tinf,tlb,m-xmm,a-1,PTM[5],s),dm=densu(alt,b,tinf,tlb,xmm,0,PTM[5],s);dd=correct(dnet(dd,dm,zhm28,xmm,m),b);}
            d[i]=dd;
        };
        species(HE,0,0,MHE,alpha[0],altl[0],[&](double v,double b){return v*ccor(alt,std::log(b28*PDM[0][1]/b),PDM[0][5]*PDL[1][1],PDM[0][4]*PDL[1][0]);});
        species(O,1,1,MO,alpha[1],altl[1],[&](double v,double){
            v*=ccor2(alt,PDM[1][1]*PDL[1][16]*(1+sw[1]*PDL[0][23]*(f107a-FLUX_REF)),PDM[1][5]*PDL[1][3],PDM[1][4]*PDL[1][2],PDM[1][5]*PDL[1][4]);
            return v*ccor(alt,PDM[1][3]*PDL[1][14],PDM[1][7]*PDL[1][13],PDM[1][6]*PDL[1][12]);});
        { // O2: the second correction applies at every height.
            const double db=PDM[3][0]*std::exp(sw[21]*globe7(PD[4]))*PD[4][0];double dd=densu(alt,db,tinf,tlb,MO2,alpha[3],PTM[5],s);
            if(sw[15]){
                if(alt<=altl[3]){const double b=densu(PDM[3][2],db,tinf,tlb,MO2-xmm,alpha[3]-1,PTM[5],s),dm=densu(alt,b,tinf,tlb,xmm,0,PTM[5],s);
                    dd=dnet(dd,dm,zhm28,xmm,MO2)*ccor(alt,std::log(b28*PDM[3][1]/b),PDM[3][5]*PDL[1][7],PDM[3][4]*PDL[1][6]);}
                dd*=ccor2(alt,PDM[3][3]*PDL[1][23]*(1+sw[1]*PDL[0][23]*(f107a-FLUX_REF)),PDM[3][7]*PDL[1][22],PDM[3][6]*PDL[1][21],PDM[3][7]*PDL[0][22]);
            }
            d[O2]=dd;
        }
        species(AR,5,4,MAR,alpha[4],altl[4],[&](double v,double b){return v*ccor(alt,std::log(b28*PDM[4][1]/b),PDM[4][5]*PDL[1][9],PDM[4][4]*PDL[1][8]);});
        species(H,6,5,MH,alpha[6],altl[6],[&](double v,double b){
            v*=ccor(alt,std::log(b28*PDM[5][1]*std::sqrt(PDL[1][17]*PDL[1][17])/b),PDM[5][5]*PDL[1][11],PDM[5][4]*PDL[1][10]);
            return v*ccor(alt,PDM[5][3]*PDL[1][20],PDM[5][7]*PDL[1][19],PDM[5][6]*PDL[1][18]);});
        species(N,7,6,MN,alpha[7],altl[7],[&](double v,double b){
            v*=ccor(alt,std::log(b28*PDM[6][1]*PDL[0][2]/b),PDM[6][5]*PDL[0][1],PDM[6][4]*PDL[0][0]);
            return v*ccor(alt,PDM[6][3]*PDL[0][5],PDM[6][7]*PDL[0][4],PDM[6][6]*PDL[0][3]);});
        const double db16h=PDM[7][0]*std::exp(sw[21]*globe7(PD[8]))*PD[8][0],tho=PDM[7][9]*PDL[0][6],zsht=PDM[7][5],zmho=PDM[7][4];
        d[ANO]=densu(alt,db16h,tho,tho,MO,alpha[8],PTM[5],s)*std::exp(-zsht/scalh(zmho,MO,tho)*(std::exp((zmho-alt)/zsht)-1));
        for(auto& v:d)v*=1e6;d[MASS]=mass();
    }
    void gtd7(double alt){
        gts7(std::max(alt,ZN2[0]));if(alt>=ZN2[0])return;
        const double r=PMA[2][0]*PAVGM[2];
        tgn2[0]=tgn1[1];tn2[0]=tn1[4];tn2[1]=PMA[0][0]*PAVGM[0]/(1-sw[20]*glob7s(PMA[0]));tn2[2]=PMA[1][0]*PAVGM[1]/(1-sw[20]*glob7s(PMA[1]));
        tn2[3]=PMA[2][0]*PAVGM[2]/(1-sw[20]*sw[22]*glob7s(PMA[2]));tgn2[1]=PMA[9][0]*PAVGM[8]*(1+sw[20]*sw[22]*glob7s(PMA[9]))*tn2[3]*tn2[3]/(r*r);tn3[0]=tn2[3];
        if(alt<=ZN3[0]){const double q=PMA[6][0]*PAVGM[6];tgn3[0]=tgn2[1];
            for(int i=1;i<5;++i)tn3[i]=PMA[i+2][0]*PAVGM[i+2]/(1-sw[22]*glob7s(PMA[i+2]));
            tgn3[1]=PMA[7][0]*PAVGM[7]*(1+sw[22]*glob7s(PMA[7]))*tn3[4]*tn3[4]/(q*q);}
        const double dmc=alt>ZMIX?1-(ZN2[0]-alt)/(ZN2[0]-ZMIX):0.,dz28=d[N2],dm28m=dm28*1e6;
        d[N2]=densm(alt,dm28m,PDM[2][4])*(1+(dz28/dm28m-1)*dmc);
        auto mixed=[&](int i,int k){d[i]=d[N2]*PDM[k][1]*(1+(d[i]/(dz28*PDM[k][1])-1)*dmc);};
        mixed(HE,0);mixed(O2,3);mixed(AR,4);d[O]=d[ANO]=d[H]=d[N]=0;d[MASS]=mass();t[1]=densm(alt,1,0);
    }
};
std::int64_t floorDays(double unix){return static_cast<std::int64_t>(std::floor(unix/86400.));}
int dayOfYear(std::int64_t days){ // Civil from days (Hinnant), then the ordinal day.
    const std::int64_t z=days+719468,era=(z>=0?z:z-146096)/146097,doe=z-era*146097,yoe=(doe-doe/1460+doe/36524-doe/146096)/365,doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
    const int m=static_cast<int>(mp<10?mp+3:mp-9),y=static_cast<int>(yoe+era*400+(m<=2)),d=static_cast<int>(doy-(153*mp+2)/5+1);
    constexpr int before[]{0,31,59,90,120,151,181,212,243,273,304,334};const bool leap=(y%4==0&&y%100!=0)||y%400==0;
    return before[m-1]+d+(leap&&m>2);
}
}
MsisOutput nrlmsise00(int doy,double sec,double alt,double lat,double lon,double lst,const SpaceWeather& w,bool anomalous){
    if(!(alt>=-5&&alt<=1000)||!std::isfinite(lat)||!std::isfinite(lon))throw std::invalid_argument("NRLMSISE-00 input out of range");
    Model m(doy,sec,lat,lon,lst,w);m.gtd7(alt);
    MsisOutput o{m.d,m.t[0],m.t[1]};if(anomalous)o.density[MASS]+=AMU*MO*m.d[ANO];
    return o;
}
SpaceWeatherTable::SpaceWeatherTable(const std::vector<SpaceWeatherDay>& days){
    if(days.empty())return;
    double lo=days[0].day,hi=lo;for(const auto& d:days){lo=std::min(lo,d.day);hi=std::max(hi,d.day);}
    first_=lo;const auto n=static_cast<std::size_t>(hi-lo)+1;days_.resize(n);present_.assign(n,false);
    for(const auto& d:days){const auto i=static_cast<std::size_t>(d.day-lo);days_[i]=d;present_[i]=true;}
}
const SpaceWeatherDay* SpaceWeatherTable::day(double d)const{
    const double i=d-first_;return i>=0&&i<static_cast<double>(days_.size())&&present_[static_cast<std::size_t>(i)]?&days_[static_cast<std::size_t>(i)]:nullptr;
}
std::optional<SpaceWeather> SpaceWeatherTable::at(double utc,std::string* note)const{
    const double d=std::floor(utc/86400.);const auto* today=day(d);const auto* yesterday=day(d-1);
    if(!today||!yesterday)return std::nullopt;
    SpaceWeather w;w.f107=yesterday->f107;w.f107a=today->f107a;w.ap.fill(std::isnan(today->apAverage)?15.:today->apAverage);
    bool history=true;for(int k=0;k<4;++k){const auto* x=day(d-k);if(!x||!x->hasAp)history=false;}
    if(history){ // Interval 0 is the current 3-hour period; older ones count backwards.
        const int current=static_cast<int>((utc-d*86400.)/10800.);
        auto ap=[&](int back){const int k=current-back,offset=k>=0?0:(-k+7)/8;return day(d-offset)->ap[static_cast<std::size_t>(k+8*offset)];};
        w.ap[1]=ap(0);w.ap[2]=ap(1);w.ap[3]=ap(2);w.ap[4]=ap(3);
        double a=0,b=0;for(int i=4;i<12;++i)a+=ap(i);for(int i=12;i<20;++i)b+=ap(i);w.ap[5]=a/8;w.ap[6]=b/8;w.history=true;
    }
    if(note){char s[160];std::snprintf(s,sizeof s,"F10.7 %.1f, 81-day %.1f, Ap %.0f (%s)",w.f107,w.f107a,w.ap[0],today->type.c_str());*note=s;}
    return w;
}
Air Msis::at(std::int64_t tt,double latitude,double longitude,double height)const{
    if(height>1e6)return{};
    const double unix=utcFromTt(tt),sec=unix-86400.*static_cast<double>(floorDays(unix)),lon=longitude*180/3.14159265358979323846;
    double lst=sec/3600+lon/15;lst-=24*std::floor(lst/24);
    const auto now=table_?table_->at(unix):std::nullopt;
    const auto o=nrlmsise00(dayOfYear(floorDays(unix)),sec,std::max(height,-5000.)/1000,latitude*180/3.14159265358979323846,lon,lst,now?*now:weather_);
    double n=0;for(int i:{HE,O,N2,O2,AR,H,N})n+=o.density[static_cast<std::size_t>(i)];
    const double k=1.380649e-23,p=n*k*o.temperature,rho=o.density[MASS],mean=rho/n;
    return{o.temperature,p,rho,std::sqrt(1.4*k*o.temperature/mean),{}};
}
}
