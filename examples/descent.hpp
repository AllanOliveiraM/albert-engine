#pragma once
// A powered descent planned as the real landers' are (G-FOLD's convex way: Açıkmeşe and Blackmore): the thrust's acceleration,
// held over each of N equal steps, that brings the vehicle to a point and velocity at a time T, within its engines' throttle
// (a floor on the thrust's upward part, a ceiling on all of it), a lean off the vertical, a glide slope (a cone up from just
// below the target: never low far out) and a speed across the ground that grows with the height (never low and fast); the
// least Σ|u|² Δt (smooth, near the least propellant for these short burns), with a little of its change from step to step and
// of its jump from what the engines give now. A conic quadratic program solved by ADMM (OSQP's splitting with the cones'
// projections, as COSMO does), warm-started from the last plan: 48 variables and ~220 rows, dense, a few milliseconds. The free
// final time is searched about the last plan's: the cheapest that meets everything.
#include "gravity_turn.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace albert::example {
class PoweredDescent {
public:
    static constexpr int N=16,V=3*N;
    // In a local frame (x east, y north, z up; m, m/s, m/s²): the start and the target, gravity and an outside push (constant
    // over the burn), and the thrust's acceleration now; floor: the least upward thrust (m/s²), ceiling: the most of all of it;
    // lean: the tangent of the most off the vertical; slope: the glide slope's rise a metre out (its cone through `radius` m
    // round the target at the target's height); climbRate, speedFloor: the fastest across the ground at a height h over the
    // target, speedFloor + climbRate h (m/s).
    // wall, brake: never closing on the west (-x) faster than it can stop short of x = wall braking at `brake` m/s² (√(2 brake
    // (x - wall)), as four chords under it: linear, and its own speed westward only); brake 0: no such bound.
    // jerk: the most its push across may change a second (m/s³: how fast the attitude can swing the lean); across: the weight of
    // the push across in the cost, over the upward one's (more: gentler ways across, a longer burn).
    // hold: how strongly the first step keeps to the thrust's acceleration now (the attitude cannot jump: a plan that starts
    // from where the engines point is one it can fly).
    // sinkRate: the fastest down at a height h over the target, -vt.z + sinkRate h (m/s; 0: no such bound): never fast near it.
    // wedge, wedgeFloor: at most wedgeFloor + wedge h off the target's line along x (m; wedge 0: none): along that line it may
    // slide in late, across it only while high.
    // lag: the time constant (s) the thrust's acceleration follows what is asked with (the attitude turning the engines onto it):
    // each step's a_m = (1 - β) a_{m-1} + β u_m from a_{-1} = uNow, β = 1 - e^{-dt/lag} (0: none, a_m = u_m). Asked with that
    // in view, the plan leads its own lag instead of chasing it (a plan laid afresh each half second without it overcorrects).
    // low, lowAcross, lowLean: below `low` m over the target (the hull among what it lands in), at most lowAcross off the line
    // along x (m) and leaning at most lowLean (its tangent): only small corrections there. Not convex as such: each step's
    // height taken from the plan it starts from (the last one; else a straight descent), as successive convexification does.
    struct Problem {Vec r0,v0,rt,vt,g{0,0,-9.80665},d,uNow;double lag=0,hold=4,floor=0,ceiling=30,lean=.364,slope=.5,radius=3,climbRate=.3,speedFloor=3,wall=-8,brake=0,jerk=0,across=1,sinkRate=0,wedge=0,wedgeFloor=.5,low=0,lowAcross=1,lowLean=.1;};
    struct Plan {std::vector<Vec> u;double T=0,miss=1e300,cost=1e300;bool ok=false;Problem asked;}; // miss: the worst breach of the target or a limit (m, m/s, m/s²); asked: what it was laid out for.
    // The plan for a time T (s); warm: a plan to start from (its acceleration resampled onto these steps).
    Plan solve(const Problem& p,double T,const Plan* warm=nullptr,double elapsed=0)const{
        const double dt=T/N;Plan out;out.T=T;out.asked=p;
        // The state at node k is affine in the asked accelerations: r_k = R_k + Σ_{j<k} PC_kj u_j, v_k = W_k + Σ_{j<k} VC_kj u_j, the
        // steps' own a_m = c_m uNow + Σ_{j≤m} L_mj u_j through the lag (c_m = (1-β)^{m+1}, L_mj = β (1-β)^{m-j}).
        const Vec G=p.g+p.d;const double beta=p.lag>0?1-std::exp(-dt/p.lag):1;
        std::array<double,N+1> pc0{},vc0{};std::array<std::array<double,N>,N+1> PC{},VC{};
        for(int k=1;k<=N;++k)for(int m=0;m<k;++m){const double cp=dt*dt*(k-m-.5),cv=dt;pc0[k]+=cp*std::pow(1-beta,m+1);vc0[k]+=cv*std::pow(1-beta,m+1);
            for(int j=0;j<=m;++j){const double l=beta*std::pow(1-beta,m-j);PC[k][j]+=cp*l;VC[k][j]+=cv*l;}}
        auto R=[&](int k){const double t=k*dt;return p.r0+p.v0*t+G*(t*t/2)+p.uNow*pc0[k];};auto W=[&](int k){return p.v0+G*(k*dt)+p.uNow*vc0[k];};
        rows_.clear();blocks_.clear();
        const double sp=2/(T*T),sv=1/T; // Row scales: positions and speeds into accelerations.
        auto row=[&](double b){rows_.push_back({});rows_.back().b=b;return &rows_.back();};
        auto posRow=[&](int k,int c,double scale,double b){Row* r=row(b);for(int j=0;j<k;++j)r->a[3*j+c]=-scale*PC[k][j];return r;}; // b - A x = scale (R_k + Σ…)[c] when b = scale R_k[c]
        auto velRow=[&](int k,int c,double scale,double b){Row* r=row(b);for(int j=0;j<k;++j)r->a[3*j+c]=-scale*VC[k][j];return r;};
        auto comp=[](Vec v,int c){return c==0?v.x:c==1?v.y:v.z;};
        // The target (zero cone): r_N = rt, v_N = vt.
        {const std::size_t s0=rows_.size();for(int c=0;c<3;++c){Row* r=posRow(N,c,sp,0);for(double& x:r->a)x=-x;r->b=sp*(comp(p.rt,c)-comp(R(N),c));}blocks_.push_back({Zero,s0,3,sp});}
        {const std::size_t s0=rows_.size();for(int c=0;c<3;++c){Row* r=velRow(N,c,sv,0);for(double& x:r->a)x=-x;r->b=sv*(comp(p.vt,c)-comp(W(N),c));}blocks_.push_back({Zero,s0,3,sv});}
        for(int k=0;k<N;++k){ // The thrust: upward at least its floor; within the lean; within its ceiling.
            {const std::size_t s0=rows_.size();Row* r=row(-p.floor);r->a[3*k+2]=-1;blocks_.push_back({Nonneg,s0,1,1});}
            {const std::size_t s0=rows_.size();Row* r=row(0);r->a[3*k+2]=-p.lean;r=row(0);r->a[3*k]=-1;r=row(0);r->a[3*k+1]=-1;blocks_.push_back({Cone,s0,3,1});}
            {const std::size_t s0=rows_.size();row(p.ceiling);for(int c=0;c<3;++c){Row* r=row(0);r->a[3*k+c]=-1;}blocks_.push_back({Cone,s0,4,1});}}
        if(p.jerk>0)for(int k=0;k+1<N;++k){const std::size_t s0=rows_.size();row(p.jerk*dt); // The push across changing no faster than the attitude swings it.
            for(int c=0;c<2;++c){Row* r=row(0);r->a[3*(k+1)+c]=-1;r->a[3*k+c]=1;}blocks_.push_back({Cone,s0,3,1});}
        if(p.low>0){ // Low steps, as the plan it starts from has them (its state at each step's time; else a straight descent).
            for(int k=0;k<N;++k){double z=p.r0.z+(p.rt.z-p.r0.z)*(k+1.)/N;
                if(warm&&!warm->u.empty()){Vec r,v,u;at(warm->asked,*warm,elapsed+(k+1)*dt,r,v,u);z=r.z;}
                if(z-p.rt.z>=p.low)continue;
                {const std::size_t s0=rows_.size();Row* r=row(0);r->a[3*k+2]=-p.lowLean;r=row(0);r->a[3*k]=-1;r=row(0);r->a[3*k+1]=-1;blocks_.push_back({Cone,s0,3,1});}
                if(k+1<N)for(const double side:{1.,-1.}){const std::size_t s0=rows_.size();Row* r=posRow(k+1,1,sp,sp*(p.lowAcross-side*(R(k+1).y-p.rt.y)));
                    for(double& x:r->a)x*=-side;r->b=sp*(p.lowAcross-side*(R(k+1).y-p.rt.y));blocks_.push_back({Nonneg,s0,1,sp});}}}
        for(int k=1;k<N;++k){ // The glide slope and the speed across by height at each node between.
            const Vec rk=R(k),wk=W(k);const double apex=p.rt.z-p.radius*p.slope;
            {const std::size_t s0=rows_.size();posRow(k,2,sp/p.slope,sp*(rk.z-apex)/p.slope);posRow(k,0,sp,sp*(rk.x-p.rt.x));posRow(k,1,sp,sp*(rk.y-p.rt.y));blocks_.push_back({Cone,s0,3,sp});}
            {const std::size_t s0=rows_.size();posRow(k,2,sv*p.climbRate,sv*(p.climbRate*(rk.z-p.rt.z)+p.speedFloor));velRow(k,0,sv,sv*wk.x);velRow(k,1,sv,sv*wk.y);blocks_.push_back({Cone,s0,3,sv});}
            if(p.wedge>0)for(const double side:{1.,-1.}){const std::size_t s0=rows_.size();Row* r=posRow(k,2,sp*p.wedge,sp*(p.wedgeFloor+p.wedge*(rk.z-p.rt.z)-side*(rk.y-p.rt.y))); // wedgeFloor + wedge (z - rt.z) ∓ (y - rt.y) ≥ 0.
                for(int j=0;j<k;++j)r->a[3*j+1]+=side*sp*PC[k][j];blocks_.push_back({Nonneg,s0,1,sp});}
            if(p.sinkRate>0){const std::size_t s0=rows_.size();Row* r=velRow(k,2,sv,sv*(wk.z-p.vt.z+p.sinkRate*(rk.z-p.rt.z))); // v_z ≥ vt.z - sinkRate (z - rt.z).
                for(int j=0;j<k;++j)r->a[3*j+2]+=-sv*p.sinkRate*PC[k][j];blocks_.push_back({Nonneg,s0,1,sv});}
            if(p.brake>0){constexpr double at[]{0,10,40,100,250}; // -v_x ≤ g_i + s_i (x - wall - at_i) on each chord of g(x) = √(2 brake x).
                for(int i=0;i<4;++i){const double g0=std::sqrt(2*p.brake*at[i]),g1=std::sqrt(2*p.brake*at[i+1]),sl=(g1-g0)/(at[i+1]-at[i]);const std::size_t s0=rows_.size();
                    Row* r=velRow(k,0,sv,sv*(wk.x+g0+sl*(rk.x-p.wall-at[i])));for(int j=0;j<k;++j)r->a[3*j]+=-sv*sl*PC[k][j];blocks_.push_back({Nonneg,s0,1,sv});}}}
        for(auto& bl:blocks_){double most=0; // Each block's rows to a unit norm at most (the cones care only for directions): ADMM's steps alike in every row.
            for(std::size_t i=bl.start;i<bl.start+bl.size;++i){double n=0;for(double x:rows_[i].a)n+=x*x;most=std::max(most,std::sqrt(n));}
            if(most>0){for(std::size_t i=bl.start;i<bl.start+bl.size;++i){for(double& x:rows_[i].a)x/=most;rows_[i].b/=most;}bl.scale/=most;}}
        // The cost: Σ (u_z² + across |u_xy|²) dt, ws Σ|u_{k+1} - u_k|², w0 |u_0 - uNow|².
        const double ws=dt,w0=p.hold*dt;std::array<double,V*V> P{};std::array<double,V> q{};
        for(int i=0;i<V;++i)P[i*V+i]+=2*dt*(i%3==2?1:p.across);
        for(int k=0;k+1<N;++k)for(int c=0;c<3;++c){const int a=3*k+c,b=a+3;P[a*V+a]+=2*ws;P[b*V+b]+=2*ws;P[a*V+b]-=2*ws;P[b*V+a]-=2*ws;}
        for(int c=0;c<3;++c){P[c*V+c]+=2*w0;q[c]-=2*w0*comp(p.uNow,c);}
        // ADMM: (P + σI + Aᵀ R A) x = σ x - q + Aᵀ (R z - y); ẑ = α A x + (1 - α) z; z = Π_{b-K}(ẑ + y/R); y += R (ẑ - z).
        const std::size_t m=rows_.size();constexpr double sigma=1e-6,rho=.5,alpha=1.6;std::vector<double> rr(m,rho);for(const auto& b:blocks_)if(b.kind==Zero)for(std::size_t i=b.start;i<b.start+b.size;++i)rr[i]=rho*1e3;
        std::array<double,V*V> M;auto factor=[&]{M=P;for(int i=0;i<V;++i)M[i*V+i]+=sigma;
            for(std::size_t r=0;r<m;++r){const auto& a=rows_[r].a;for(int i=0;i<V;++i)if(a[i]!=0)for(int j=0;j<V;++j)M[i*V+j]+=rr[r]*a[i]*a[j];}return cholesky(M);};
        if(!factor())return out;
        std::array<double,V> x{};
        if(warm&&!warm->u.empty()){const double wd=warm->T/static_cast<double>(warm->u.size());
            for(int k=0;k<N;++k){const int j=std::clamp(static_cast<int>((elapsed+(k+.5)*dt)/wd),0,static_cast<int>(warm->u.size())-1);const Vec u=warm->u[static_cast<std::size_t>(j)];x[3*k]=u.x;x[3*k+1]=u.y;x[3*k+2]=u.z;}}
        else for(int k=0;k<N;++k)x[3*k+2]=-p.g.z;
        std::vector<double> z(m),y(m,0.),ax(m);auto mulA=[&](const std::array<double,V>& v,std::vector<double>& o){for(std::size_t r=0;r<m;++r){double s=0;const auto& a=rows_[r].a;for(int i=0;i<V;++i)s+=a[i]*v[i];o[r]=s;}};
        mulA(x,z);project(z);
        for(int it=0;it<600;++it){
            std::array<double,V> rhs{};for(int i=0;i<V;++i)rhs[i]=sigma*x[i]-q[i];
            for(std::size_t r=0;r<m;++r){const double w=rr[r]*z[r]-y[r];if(w!=0){const auto& a=rows_[r].a;for(int i=0;i<V;++i)rhs[i]+=a[i]*w;}}
            x=solveWith(M,rhs);mulA(x,ax);
            std::vector<double> zh(m);for(std::size_t r=0;r<m;++r)zh[r]=alpha*ax[r]+(1-alpha)*z[r];
            std::vector<double> w(m);for(std::size_t r=0;r<m;++r)w[r]=zh[r]+y[r]/rr[r];
            z=w;project(z);for(std::size_t r=0;r<m;++r)y[r]+=rr[r]*(zh[r]-z[r]);
            if(it%100==99){ // ρ rebalanced as OSQP does: the primal residual's share over the dual's.
                mulA(x,ax);double rp=0,ap=0,rd=0,ad=0;for(std::size_t r=0;r<m;++r){rp=std::max(rp,std::abs(ax[r]-z[r]));ap=std::max({ap,std::abs(ax[r]),std::abs(z[r])});}
                for(int i=0;i<V;++i){double px=0,aty=0;for(int j=0;j<V;++j)px+=P[i*V+j]*x[j];for(std::size_t r=0;r<m;++r)aty+=rows_[r].a[i]*y[r];rd=std::max(rd,std::abs(px+q[i]+aty));ad=std::max({ad,std::abs(px),std::abs(aty),std::abs(q[i])});}
                const double f=std::sqrt((rp/std::max(ap,1e-9))/std::max(rd/std::max(ad,1e-9),1e-12));
                if(f>5||f<.2){const double k=std::clamp(f,1e-3,1e3);for(double& v:rr)v*=k;if(!factor())return out;}}}
        // How far the plan is from meeting it all (in the rows' own units, back to m, m/s, m/s²).
        mulA(x,ax);double miss=0;
        for(const auto& b:blocks_){std::vector<double> t(b.size);for(std::size_t i=0;i<b.size;++i)t[i]=rows_[b.start+i].b-ax[b.start+i];
            double breach=0;if(b.kind==Zero)for(double v:t)breach=std::max(breach,std::abs(v));else if(b.kind==Nonneg)breach=std::max(-t[0],0.);
            else{double n=0;for(std::size_t i=1;i<b.size;++i)n+=t[i]*t[i];breach=std::max(std::sqrt(n)-t[0],0.);}
            miss=std::max(miss,breach/b.scale);}
        out.miss=miss;out.u.resize(N);for(int k=0;k<N;++k)out.u[static_cast<std::size_t>(k)]={x[3*k],x[3*k+1],x[3*k+2]};
        double cost=0;for(int i=0;i<V;++i){double s=0;for(int j=0;j<V;++j)s+=P[i*V+j]*x[j];cost+=.5*x[i]*s+q[i]*x[i];}out.cost=cost;out.ok=miss<.3;
        return out;
    }
    // Its state t s into the plan, and the acceleration then.
    static void at(const Problem& p,const Plan& q,double t,Vec& r,Vec& v,Vec& u){ // u: what it asks then.
        r=p.r0;v=p.v0;u=q.u.empty()?Vec{}:q.u.back();if(q.u.empty())return;const double dt=q.T/N,beta=p.lag>0?1-std::exp(-dt/p.lag):1;const Vec G=p.g+p.d;double left=std::clamp(t,0.,q.T);Vec a=p.uNow;
        for(std::size_t k=0;k<q.u.size()&&left>0;++k){const double h=std::min(dt,left);a=a*(1-beta)+q.u[k]*beta;r=r+v*h+(a+G)*(h*h/2);v=v+(a+G)*h;left-=h;}
        if(t<q.T)u=q.u[std::min(static_cast<std::size_t>(t/dt),q.u.size()-1)];
    }
private:
    enum Kind {Zero,Nonneg,Cone};
    struct Row {std::array<double,V> a{};double b=0;};
    struct Block {Kind kind;std::size_t start,size;double scale;}; // scale: the rows' units over m, m/s or m/s².
    mutable std::vector<Row> rows_;mutable std::vector<Block> blocks_;
    // z onto b - K, block by block (K's projection of b - z).
    void project(std::vector<double>& z)const{
        for(const auto& bl:blocks_){std::vector<double> t(bl.size);for(std::size_t i=0;i<bl.size;++i)t[i]=rows_[bl.start+i].b-z[bl.start+i];
            if(bl.kind==Zero)std::fill(t.begin(),t.end(),0.);else if(bl.kind==Nonneg)t[0]=std::max(t[0],0.);
            else{double n=0;for(std::size_t i=1;i<bl.size;++i)n+=t[i]*t[i];n=std::sqrt(n);
                if(n<=t[0]){}else if(n<=-t[0])std::fill(t.begin(),t.end(),0.);else{const double a=(t[0]+n)/2;t[0]=a;for(std::size_t i=1;i<bl.size;++i)t[i]*=a/n;}}
            for(std::size_t i=0;i<bl.size;++i)z[bl.start+i]=rows_[bl.start+i].b-t[i];}
    }
    static bool cholesky(std::array<double,V*V>& a){ // In place: the lower triangle L (a = L Lᵀ).
        for(int j=0;j<V;++j){double d=a[j*V+j];for(int k=0;k<j;++k)d-=a[j*V+k]*a[j*V+k];if(!(d>0))return false;d=std::sqrt(d);a[j*V+j]=d;
            for(int i=j+1;i<V;++i){double s=a[i*V+j];for(int k=0;k<j;++k)s-=a[i*V+k]*a[j*V+k];a[i*V+j]=s/d;}}
        return true;
    }
    static std::array<double,V> solveWith(const std::array<double,V*V>& l,std::array<double,V> b){
        for(int i=0;i<V;++i){double s=b[i];for(int k=0;k<i;++k)s-=l[i*V+k]*b[k];b[i]=s/l[i*V+i];}
        for(int i=V-1;i>=0;--i){double s=b[i];for(int k=i+1;k<V;++k)s-=l[k*V+i]*b[k];b[i]=s/l[i*V+i];}
        return b;
    }
};
}
