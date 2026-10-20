#pragma once
// The ship's way home after orbital insertion (starship.hpp: its flaps, RCS, heat shield and pins):
// - in orbit, nose forward and belly down by RCS pulses, until the pass that brings it back over the tower (about a day after
//   a launch from Starbase);
// - a deorbit burn on its 3 sea-level Raptors once an entry flown from there would end at the tower;
// - the entry belly-first (angle of attack 65° hypersonic to 80° subsonic), steered by bank angle onto the tower, the flaps
//   and the RCS holding the attitude;
// - the flip and the landing burn on 3 engines, the hover on 2 and the catch on the arms.
#include "starship.hpp"
namespace albert::example::starship {
class ShipReturn:public TowerApproach {
public:
    enum class Phase {orbit,orient,deorbit,glide,entry,flip,landing,hover,descend,caught};
    ShipReturn(Vec site,double latitude,double longitude,double arms,double rest)
        :TowerApproach(site,latitude,longitude,arms,rest,shipLength,shipPinAt,combine(ship(true)),combine(ship(false)),shipPropellant){}
    Phase phase()const{return phase_;}
    double nextStep()const{return next_;} // How far (s) the client may step next: long in orbit, 20 ms otherwise.
    double along()const{return along_;} // The latest predicted landing past the tower (m).
    double beside()const{return cross_;} // And right of it (m).
    double bank()const{return held_;} // Commanded (rad, right positive).
    double dragFactor()const{return drag_;} // The measured over the predicted (drag, lift).
    double liftFactor()const{return lift_;}
    double deorbit=100,crossLimit=150e3; // The burn's Δv (m/s); the farthest a pass may run beside the tower (m).
    Command step(double t,const State& s){
        Command c;next_=.02;const Vec rel=s.position+site_*-1;const double over=dot(rel,up_),height=norm(s.position)-norm(site_);const Vec flat=rel+up_*-over;
        switch(phase_){
        case Phase::orbit: // Where a deorbit burn now would land, each minute (each second within 2000 km of it); nose forward and
            // belly to the Earth, by a half-second pulse of the RCS every 30 s, torque-free between them (long steps).
            if(t>=check_){const Arrival a=plan(s);along_=a.along;cross_=a.cross;const bool pass=a.down&&std::abs(a.cross)<crossLimit&&a.along<0&&a.along>-2e6;
                check_=t+(pass?1.:60.);if(pass&&a.along>-1.2e6)phase_=Phase::orient;}
            c.torqueOn=true;if(t>=pulse_){c.torque=level(s);pulse_=t+30;next_=.5;}else next_=std::max(std::min(check_,pulse_)-t,.001);break;
        case Phase::orient: // Retrograde on the RCS; the burn once the landing it gives reaches the tower, pointed within 3°.
            hold(c,s,unit(inertial(s)*-1));
            if(t>=check_){const Arrival a=plan(s);along_=a.along;cross_=a.cross;check_=t+1;
                if(a.along>=0&&dot(bodyX(s),unit(inertial(s)*-1))>std::cos(3*deg)){burn_.use(c,s,3);c.throttle=.4;phase_=Phase::deorbit;v0_=norm(inertial(s));}}
            break;
        case Phase::deorbit: // Cut once the landing without more of it, at the planned bank, is back at the tower (within the pass's
            // 2000 km: a burn barely begun can come down a lap or more later, as far short).
            aim(c,s,unit(inertial(s)*-1));{const Arrival a=arrive(s.position,s.velocity,s.mass,50*deg);along_=a.along;cross_=a.cross;
                burn_.keep(c,s);if((a.down&&a.along<=0&&a.along>-2e6)||v0_-norm(inertial(s))>=2*deorbit){burn_.use(c,s,0);phase_=Phase::glide;}}break;
        case Phase::glide: // Torque-free down to 140 km, then belly-first at the entry attitude on the RCS.
            if(height>140e3){c.torqueOn=true;c.torque={};next_=std::min(10.,(height-140e3)/std::max(-dot(s.velocity,unit(s.position)),1.));}
            else attitude(c,s,alpha(norm(s.velocity),height),0);
            if(height<100e3){phase_=Phase::entry;sign_=cross_>0?-1:1;check_=t;held_=0;}
            break;
        case Phase::entry:{ // Bank by a predictor: |σ| for the range, its side toward the tower within a corridor.
            const double speed=norm(s.velocity);double al=alpha(speed,height);
            if(speed>300){if(t>=check_){check_=t+1;guide(s);}}else al=glide(s,height);
            held_+=std::clamp(bank_-held_,-4*deg*.02,4*deg*.02);attitude(c,s,al,held_); // The bank at most 4°/s.
            if(speed<150&&range(s)<5e3&&height<flipHeight(s)){phase_=Phase::flip;set_.use(c,s,3);c.throttle=.5;}
            break;}
        case Phase::flip: // Nose up onto the engines: the forward flaps open, the aft ones folded, the gimbals.
            set_.keep(c,s);aim(c,s,up_,set_.centre());c.surfaces={45*deg,45*deg,-45*deg,-45*deg};c.throttle=.5;
            if(dot(bodyX(s),up_)>std::cos(25*deg))phase_=Phase::landing;
            break;
        case Phase::landing:{ // To rest at the hover point on 3 engines (2 once they can carry it), across onto the axis.
            const double above=over-catchHeight(s)-hoverAbove,vz=dot(s.velocity,up_),brake=above>1&&vz<0?vz*vz/(2*above):0,up=brake+norm(gravity(s.position));
            Vec side=toAxis(s,flat);const double limit=std::tan(20*deg)*up;if(norm(side)>limit)side=side*(limit/norm(side));
            set_.keep(c,s);const Vec a=up_*up+side;if(set_.count()==3&&s.mass*norm(a)<.9*2*raptorSea)set_.use(c,s,2);
            c.throttle=std::clamp(s.mass*norm(a)/(std::max(set_.count(),1)*raptorSea),.4,1.);aim(c,s,unit(a),set_.centre());roll(c,s);c.surfaces={-45*deg,-45*deg,-45*deg,-45*deg};
            if(set_.count()<=2&&(above<2||vz>-1))phase_=Phase::hover;
            break;}
        case Phase::hover:case Phase::descend: // Over the tower, then down at 1 m/s until the pins rest on the arms.
            if(phase_==Phase::hover&&armsClosed&&settled(s,flat))phase_=Phase::descend;
            {set_.keep(c,s);const int n=std::max(set_.count(),1);const double demand=overArms(c,s,flat,over,phase_==Phase::descend,n,set_.centre());c.surfaces={-45*deg,-45*deg,-45*deg,-45*deg};
                // As light as it gets, 2 Raptors at their least lift more than its weight: down to 1 (and up again when short, while one works).
                if(n>1&&demand<.45*n*raptorSea)set_.use(c,s,n-1);else if(n<set_.working(s,3)&&demand>.9*n*raptorSea)set_.use(c,s,n+1);}
            if(norm(s.contact)>0){c.throttle=.4;phase_=Phase::caught;caught_=t;}
            break;
        case Phase::caught: // On the arms, the engine at its least while they take the load, then off once it hangs still.
            if(!off_){hang(c,s);c.throttle=.4;}else{c.torqueOn=true;c.torque={};}
            if(!off_&&t-caught_>2&&(std::hypot(s.rate.y,s.rate.z)<.002||t-caught_>15)){set_.use(c,s,0);off_=true;}break;
        }
        return c;
    }
private:
    struct Arrival {double along=0,cross=0,seconds=0;bool down=false;}; // down: it reaches the ground's height (else along, cross: where it gave up).
    // The flaps' drag at their openings now (times |sin α|; their entry trim before any), on the reference area.
    double flapDrag(const State& s)const{
        double d=0;for(std::size_t i=0;i<4;++i)d+=flapCn*flaps[i].area/(pi*radius*radius)*std::sin(45*deg+(s.surfaces.size()==4?s.surfaces[i]:i<2?-30*deg:13*deg));return d;}
    // Hanging on the pins with the engine on: the body upright and the thrust's line through the pins (the pivot), so nothing is
    // left to swing when it cuts.
    void hang(Command& c,const State& s)const{
        const Vec e=mul(s.toBody,cross(bodyX(s),up_)),w=s.rate,o=set_.centre();const double lever=shipLength-shipPinAt,gain=pitchInertia(s.propellant)/std::max(s.thrust*lever,1.);
        c.gimbalPitch=std::clamp(gain*(1.44*e.y-2*w.y)-o.z/lever,-15*deg,15*deg);c.gimbalYaw=std::clamp(gain*(1.44*e.z-2*w.z)+o.y/lever,-15*deg,15*deg);c.torque={};roll(c,s);
    }
    static Vec inertial(const State& s){return s.velocity+cross(Vec{0,0,7.292115e-5},s.position);}
    // The torque that over a half-second pulse brings the body's rates to the local horizontal frame's (nose along the
    // inertial velocity's horizontal, belly down: turning once an orbit) plus a turn toward it at 0.005/s (faster than the
    // gravity gradient's pull off it, √3 n).
    Vec level(const State& s)const{
        const Vec vi=inertial(s),down=unit(s.position)*-1.,x=unit(vi+down*-dot(vi,down)),y=cross(down,x);
        const Vec e=(cross(s.toBody.r0,x)+cross(s.toBody.r1,y)+cross(s.toBody.r2,down))*.5,turn=cross(s.position,vi)*(1/dot(s.position,s.position));
        const Vec dw=mul(s.toBody,turn+e*.005)+s.rate*-1.;const double ix=rollInertia(s.propellant),iy=pitchInertia(s.propellant);
        return {ix*dw.x/.5,iy*dw.y/.5,iy*dw.z/.5};
    }
    // The angle of attack by Mach: 65° hypersonic, 75° at Mach 1, 80° below.
    static double alpha(double speed,double height){const double m=speed/sound(height);return (m>5?65:m>1?65+(5-m)*2.5:80)*deg;}
    // The heading error to the tower (rad, right positive) and the distance to it over the ground (m).
    double heading(const State& s)const{
        const Vec up=unit(s.position),vh=unit(s.velocity+up*-dot(s.velocity,up)),d=site_+s.position*-1,dh=unit(d+up*-dot(d,up));
        return std::atan2(-dot(cross(vh,dh),up),dot(vh,dh));
    }
    double range(const State& s)const{return 6371e3*std::acos(std::clamp(dot(unit(s.position),unit(site_)),-1.,1.));}
    double flipHeight(const State& s)const{
        const double v=norm(s.velocity),a=std::max(set_.working(s,3)*.8*raptorSea/s.mass-g0,2.);return catchHeight(s)+hoverAbove+v*v/(2*a)+4*v+50; // On the engines that work.
    }
    // Body axes (rows, in ECEF) for the angle of attack a along the velocity v at r, banked b (right positive).
    static Mat axes(Vec r,Vec v,double a,double b){
        const Vec u=unit(v),up=unit(r),l0=unit(up+u*-dot(up,u)),l=l0*std::cos(b)+cross(u,l0)*std::sin(b);
        const Vec x=u*std::cos(a)+l*std::sin(a),z=u*std::sin(a)+l*-std::cos(a);return{x,cross(z,x),z};
    }
    // The attitude for (a, b) by PD on the rotation error, in the flow's axes: pitch and the axis across the flow in the x-z plane
    // (what the flaps' drag can turn about) by the flaps at 1 rad/s; about the flow itself (the bank) by the RCS at 0.2 rad/s,
    // critically damped, within what its thrusters give. Out of the air: everything by the RCS at 0.2 rad/s, the flaps at trim.
    void attitude(Command& c,const State& s,double a,double b)const{
        const Mat d=axes(s.position,s.velocity,a,b);const Mat& m=s.toBody;
        const Vec e=mul(m,(cross(m.r0,d.r0)+cross(m.r1,d.r1)+cross(m.r2,d.r2))*.5),w=s.rate;const double ix=rollInertia(s.propellant),iy=pitchInertia(s.propellant);
        c.torqueOn=true;if(s.q<10){c.surfaces={-30*deg,-30*deg,13*deg,13*deg};c.torque={ix*(.04*e.x-.4*w.x),iy*(.04*e.y-.4*w.y),iy*(.04*e.z-.4*w.z)};return;}
        const Vec u=unit(mul(m,s.velocity)),across=unit(Vec{u.z,0,-u.x});auto inertia=[&](Vec n){return ix*n.x*n.x+iy*(n.y*n.y+n.z*n.z);};
        actuate(c,s,Vec{0,iy*(e.y-1.4*w.y),0}+across*(inertia(across)*(dot(e,across)-1.4*dot(w,across)))+u*(inertia(u)*(.04*dot(e,u)-.4*dot(w,u))));
    }
    // A moment (body) from the flaps: pitch past the body's own moment by opening the forward pair against the aft one, the axis
    // across the flow by opening one side against the other; the RCS for the rest.
    void actuate(Command& c,const State& s,Vec want)const{
        const Vec vb=mul(s.toBody,s.velocity);const double al=std::atan2(vb.z,vb.x),sa=std::sin(al),ca=std::cos(al),area=pi*radius*radius,g=cg(s.propellant);
        if(sa<.2){c.surfaces={-30*deg,-30*deg,13*deg,13*deg};c.torque=want;return;}
        const double side=1.2*shipLength*2*radius/area,body=(g-shipLength/2)*(liftOf(shipLength,al)*ca+dragOf(shipLength,al,norm(s.velocity)/sound(norm(s.position)-norm(site_)))*sa)*s.q*area;
        double k[4],p[4];for(int i=0;i<4;++i){k[i]=s.q*flapCn*flaps[i].area*sa;p[i]=(g-flaps[i].x)*sa*k[i];}(void)side;
        const Vec axis{sa,0,-ca}; // A left flap's moment, per newton and metre off the axis.
        const double u=(want.y-body-p[0]-1.2*p[2])/(p[0]-p[2]),fwd=std::clamp(1+u,0.,2.),aft=std::clamp(1.2-u,0.,2.),dif=dot(want,axis)/(radius*(k[0]+k[2]));
        const double f[4]{std::clamp((fwd+dif)/2,0.,1.),std::clamp((fwd-dif)/2,0.,1.),std::clamp((aft+dif)/2,0.,1.),std::clamp((aft-dif)/2,0.,1.)};
        double pitch=0;c.surfaces.resize(4);for(int i=0;i<4;++i){pitch+=p[i]*f[i];c.surfaces[static_cast<std::size_t>(i)]=std::asin(f[i])-45*deg;}
        const Vec turned=axis*(radius*(k[0]*(f[0]-f[1])+k[2]*(f[2]-f[3])));c.torque=Vec{want.x-turned.x,want.y-body-pitch,want.z-turned.z};
    }
    // An entry from (r, v) at mass m banked b throughout to 1 km over the tower's ground (gravity with J2, the Earth's turning,
    // the standard atmosphere, the crossflow lift and drag at the scheduled angle of attack; only the lift's vertical part, cos b,
    // as bank reversals cancel the rest): where it ends past the tower along its final track and right of it (m), and how long it
    // takes (s).
    Arrival arrive(Vec r,Vec v,double m,double b)const{
        const Vec w{0,0,7.292115e-5};const double area=pi*radius*radius,re=6378137,j2=1.08263e-3,base=norm(site_);
        auto acc=[&](Vec p,Vec u){
            const double rn=norm(p),zz=p.z*p.z/(rn*rn),f=1.5*j2*(re/rn)*(re/rn);
            Vec a=Vec{p.x*(1+f*(1-5*zz)),p.y*(1+f*(1-5*zz)),p.z*(1+f*(3-5*zz))}*(-earth.mu/(rn*rn*rn))+cross(w,u)*-2.+cross(w,cross(w,p))*-1.;
            const double h=rn-base,rho=density(h),speed=norm(u);
            if(rho>0&&speed>0){const Vec uh=u*(1/speed),up=p*(1/rn),l=unit(up+uh*-dot(up,uh))*std::cos(b);
                const double al=alpha(speed,h),k=.5*rho*speed*speed*area/m;
                a=a+uh*(-k*drag_*(dragOf(shipLength,al,speed/sound(h))+flaps_*std::sin(al)))+l*(k*lift_*liftOf(shipLength,al));}
            return a;};
        double t=0;bool down=false;
        for(int n=0;n<20000;++n){const double h=norm(r)-base;if(h<1e3&&dot(v,r)<0){down=true;break;}const double dt=h>1.2e5?2.:h>3e4?.5:.25;
            const Vec a1=acc(r,v),v1=v+a1*(dt/2),r1=r+v*(dt/2),a2=acc(r1,v1),v2=v+a2*(dt/2),r2=r+v1*(dt/2),a3=acc(r2,v2),v3=v+a3*dt,r3=r+v2*dt,a4=acc(r3,v3);
            r=r+(v+v1*2+v2*2+v3)*(dt/6);v=v+(a1+a2*2+a3*2+a4)*(dt/6);t+=dt;}
        const Vec n=unit(cross(r,v)),rs=unit(site_),ru=unit(r),p=unit(rs+n*-dot(rs,n));
        return{-6371e3*std::atan2(dot(cross(ru,p),n),dot(ru,p)),6371e3*std::asin(std::clamp(dot(rs,n),-1.,1.)),t,down};
    }
    // A deorbit burn now (impulsive, retrograde): its landing at a 50° bank toward the tower, and how far beside it unbanked.
    Arrival plan(const State& s){
        flaps_=flapDrag(State{}); // Their entry trim.
        const Vec v=s.velocity+unit(inertial(s))*-deorbit;const Arrival flat=arrive(s.position,v,s.mass,0);
        if(!flat.down||std::abs(flat.cross)>2*crossLimit||flat.along<-3e6||flat.along>0)return flat;
        Arrival a=arrive(s.position,v,s.mass,(flat.cross>0?-50:50)*deg);a.cross=flat.cross;return a;
    }
    // The drag and lift measured (IMU) over those predicted, filtered: the predictions' corrections.
    void calibrate(const State& s){
        flaps_=flapDrag(s);const double speed=norm(s.velocity),height=norm(s.position)-norm(site_);if(s.q<500||speed<1)return;
        const Vec u=s.velocity*(1/speed),f=tmul(s.toBody,s.specific),vb=mul(s.toBody,s.velocity);const double al=std::atan2(vb.z,vb.x),k=s.q*pi*radius*radius/s.mass;
        const double drag=-dot(f,u),lift=norm(f+u*drag),d=k*(dragOf(shipLength,al,speed/sound(height))+flaps_*std::abs(std::sin(al))),l=k*std::abs(liftOf(shipLength,al));
        if(d>0)drag_+=.2*(std::clamp(drag/d,.5,2.)-drag_);if(l>0)lift_+=.2*(std::clamp(lift/l,.5,2.)-lift_);
    }
    // Subsonic, the last of the glide: the bank toward the tower (twice the heading error, at most 60°), the angle of attack the
    // glide ratio left to it above the flip asks for (80-55°), or 90° straight down once it is behind.
    double glide(const State& s,double height){
        const Vec up=unit(s.position),d=site_+s.position*-1,dh=d+up*-dot(d,up);const double psi=heading(s),need=norm(dh)/std::max(height-flipHeight(s),100.);
        bank_=std::clamp(2*psi,-60*deg,60*deg);if(std::abs(psi)>90*deg)return 90*deg;
        const double mach=norm(s.velocity)/sound(height);
        for(double a=80;a>55;a-=1){const double r=a*deg;if(liftOf(shipLength,r)/(dragOf(shipLength,r,mach)+flaps_*std::sin(r))>=need)return r;}
        return 55*deg;
    }
    void guide(const State& s){
        calibrate(s);
        // The bank's side: away from where the landing falls beside the tower, kept until that passes a corridor narrowing with
        // the range; within 15 km, toward the tower; short of it even unbanked, gently toward it.
        const double distance=range(s),corridor=3e3+.03*distance;Arrival a=arrive(s.position,s.velocity,s.mass,0);
        if(distance<15e3)sign_=heading(s)>0?1:-1;else if(sign_*a.cross>corridor)sign_=-sign_;
        if(a.along<=0)bank_=std::clamp(2*heading(s),-30*deg,30*deg); // Short even with all the lift up: turned toward the tower (the shortest way left).
        else{double lo=0,hi=120*deg;a=arrive(s.position,s.velocity,s.mass,sign_*hi);
            if(a.along>=0)bank_=sign_*hi;
            else{for(int i=0;i<8;++i){const double mid=(lo+hi)/2;a=arrive(s.position,s.velocity,s.mass,sign_*mid);(a.along>0?lo:hi)=mid;}bank_=sign_*(lo+hi)/2;}}
        along_=a.along;cross_=a.cross;
    }
    Phase phase_=Phase::orbit;double held_=0,drag_=1,lift_=1,flaps_=1.8,next_=.02,check_=0,pulse_=0,along_=0,cross_=0,bank_=0,v0_=0,caught_=0;int sign_=1;bool off_=false;
    // The landing's engines: the sea-level Raptors; the deorbit burn's: those, then the vacuum ones (in vacuum either will do).
    Engines set_{{1,2,3},shipNozzle,3},burn_{{1,2,3,4,5,6},shipNozzle};
};
}
