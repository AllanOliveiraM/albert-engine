#include "physics/gravity.hpp"
#include <cmath>
#include <stdexcept>
namespace albert::physics {
namespace {
constexpr std::size_t packed(int n,int m){return static_cast<std::size_t>(n*(n+1)/2+m);}
}
Gravity::Gravity(std::shared_ptr<const GravityField> f):field_(std::move(f)){
    const int top=field_->degree+1;const std::size_t size=packed(top+1,0);
    if(field_->degree<0||field_->c.size()<packed(field_->degree+1,0)||field_->s.size()<packed(field_->degree+1,0)||!(field_->gm>0)||!(field_->radius>0))throw std::invalid_argument("Incomplete gravity field");
    alpha_.assign(size,0);beta_.assign(size,0);plus_.assign(size,0);minus_.assign(size,0);zero_.assign(size,0);
    for(int n=0;n<=top;++n)for(int m=0;m<=n;++m){
        const std::size_t i=packed(n,m);const double dn=n,dm=m;
        if(n>=m+2){alpha_[i]=std::sqrt((2*dn+1)*(2*dn-1)/((dn-dm)*(dn+dm)));beta_[i]=std::sqrt((2*dn+1)*(dn+dm-1)*(dn-dm-1)/((2*dn-3)*(dn+dm)*(dn-dm)));}
        // Output ratios N̄nm/N̄(n+1,m±1) and N̄nm/N̄(n+1,m) with the Montenbruck-Gill factors folded in.
        const double q=(2*dn+1)/(2*dn+3);
        plus_[i]=std::sqrt((m?1.:.5)*q*(dn+dm+1)*(dn+dm+2));
        minus_[i]=m?std::sqrt((m==1?2.:1.)*q*(dn-dm+2)*(dn-dm+1)):0.;
        zero_[i]=std::sqrt(q*(dn-dm+1)*(dn+dm+1));
    }
}
V3 Gravity::acceleration(int degree,V3 r)const{
    const auto& f=*field_;if(degree<0||degree>f.degree)throw std::invalid_argument("Gravity degree out of range");
    const int top=degree+1;const double a=f.radius,r2=dot(r,r),rho=a*a/r2,x0=a*r.x/r2,y0=a*r.y/r2,z0=a*r.z/r2;
    thread_local std::vector<double> v,w;const std::size_t size=packed(top+1,0);if(v.size()<size){v.resize(size);w.resize(size);}
    v[0]=a/std::sqrt(r2);w[0]=0; // V̄00 = a/r.
    for(int m=0;m<=top;++m){
        if(m){const double k=m==1?std::sqrt(3.):std::sqrt((2*m+1)/(2.*m)),vm=v[packed(m-1,m-1)],wm=w[packed(m-1,m-1)];
            v[packed(m,m)]=k*(x0*vm-y0*wm);w[packed(m,m)]=k*(x0*wm+y0*vm);}
        if(m<top){const double k=std::sqrt(2*m+3.);v[packed(m+1,m)]=k*z0*v[packed(m,m)];w[packed(m+1,m)]=k*z0*w[packed(m,m)];}
        for(int n=m+2;n<=top;++n){const std::size_t i=packed(n,m),i1=packed(n-1,m),i2=packed(n-2,m);const double al=alpha_[i]*z0,be=beta_[i]*rho;
            v[i]=al*v[i1]-be*v[i2];w[i]=al*w[i1]-be*w[i2];}
    }
    V3 acc;
    for(int n=degree;n>=0;--n)for(int m=n;m>=0;--m){
        const std::size_t i=packed(n,m),up=packed(n+1,m+1),same=packed(n+1,m);
        const double c=n==0?1.:f.c[i],s=n==0?0.:f.s[i];
        if(m==0){acc.x-=c*plus_[i]*v[up];acc.y-=c*plus_[i]*w[up];}
        else{const std::size_t down=packed(n+1,m-1);
            acc.x+=.5*(-plus_[i]*(c*v[up]+s*w[up])+minus_[i]*(c*v[down]+s*w[down]));
            acc.y+=.5*(-plus_[i]*(c*w[up]-s*v[up])+minus_[i]*(-c*w[down]+s*v[down]));}
        acc.z-=zero_[i]*(c*v[same]+s*w[same]);
    }
    return acc*(f.gm/(a*a));
}
}
