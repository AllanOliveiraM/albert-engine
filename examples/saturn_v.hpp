#pragma once
// The flight program of Apollo 11's Saturn V (AS-506) for Albert's `saturnV` vehicle, from the flight evaluation
// report (MPR-SAT-FE-69-9): the event times of its instrument unit, the S-IC time-tilt pitch program in the inertial
// launch frame (Apollo 12's planned chi table: AS-506's is unpublished) toward the 72.058° flight azimuth, tilt arrest at
// 160 s, and from 204.1 s, in the place of the iterative guidance mode, a linear-sine steering solved every cycle so
// the remaining burns (by the rocket equation) end at the insertion height with no climb rate, in the plane of the
// 32.521° inclination; cutoff once the orbit's semi-major axis reaches the parking orbit's (186.0 x 183.2 km).
// No parameter is fitted to the flight. Commands use the flight API only.
#include "gravity_turn.hpp"
#include <array>
namespace albert::example {
class SaturnV {
public:
    enum class Phase {pad,ascent,orbit};
    static constexpr double release=.3; // Range time at holddown release (s).
    Phase phase()const{return phase_;}
    double range(const Reading& r)const{return r.elapsed+start_;}
    // Takes over in flight at range time `at` (the reading's elapsed 0), holding the GCRF direction `hold` until the
    // steering starts; `pole`: Earth's axis in GCRF.
    void resume(double at,Vec hold,Vec pole){phase_=Phase::ascent;start_=last_=at;hold_=hold;pole_=pole;resumed_=true;}
    Commands step(const Reading& r){
        Commands c;const double t=range(r);
        const Vec x=column(r,0),position=r.position,velocity=r.velocity;
        if(phase_==Phase::pad){c.ignite=true;phase_=Phase::ascent;up_=x;
            // Downrange at the flight azimuth, in the inertial frame frozen at launch: NED to GCRF from both attitudes.
            const Vec ned{std::cos(azimuth),std::sin(azimuth),0};down_=rotateNedToInertial(r,ned);
            pole_=rotateNedToInertial(r,{std::cos(latitude),0,-std::sin(latitude)});} // Earth's axis in the pad's NED.
        // Instrument unit events (range time, s).
        auto at=[&](double when){return t>=when&&last_<when;};
        if(at(135.2))c.throttle=.8;                                 // S-IC centre engine cutoff.
        if(at(162.3))c.separate=true;                                // S-IC/S-II separation.
        if(at(165.5))c.ignite=true;                                  // S-II at full thrust (engine start at 164.0, mainstage at 166.2).
        if(at(192.3)||at(197.9)){c.separate=true;c.ignite=true;}    // Aft interstage, launch escape tower.
        if(at(460.62))c.throttle=4093107./5141516;                   // S-II centre engine cutoff.
        if(at(498.0))c.throttle=3082769./5141516;                    // Mixture-ratio shift.
        if(at(549.0))c.separate=true;                                // S-II/S-IVB separation.
        if(at(553.75))c.ignite=true;                                 // S-IVB at full thrust (ignition at 552.2, mainstage at 554.7).
        last_=t;
        Vec target=x;
        if(t<13.2)target=up_;
        else if(t<204.1)target=resumed_?hold_:tilt(std::min(t,160.));
        else if(phase_==Phase::ascent){
            const double rn=norm(position),vv=dot(position,velocity)/rn;const Vec rh=position*(1/rn);
            // The target plane: through the position at the start of the steering, at the target inclination (the nearer of
            // the two); out-of-plane position and velocity nulled at cutoff by the same linear-sine form in yaw.
            if(plane_.x==0&&plane_.y==0&&plane_.z==0){const Vec north=unit(pole_+rh*(-dot(pole_,rh))),west=cross(rh,north);
                const double c0=std::clamp(std::cos(inclination)/norm(cross(rh,pole_)),-1.,1.),s0=std::sqrt(1-c0*c0),now=dot(unit(cross(position,velocity)),west);
                plane_=north*c0+west*(now>=0?s0:-s0);}
            const Vec h=unit(cross(plane_,position));const double vh=dot(velocity,h),z=dot(position,plane_),vz=dot(velocity,plane_);
            const double goal=rn+(height-r.height),need=std::sqrt(earth.mu*(2/goal-1/axis));
            if(semiMajor(position,velocity)>=axis){c.cutoff=true;phase_=Phase::orbit;}
            else{const Plan p=plan(r,std::sqrt((need-vh)*(need-vh)+vv*vv+vz*vz),t);
                if(p.tgo>8){ // Steering frozen over the last seconds, as the instrument unit's chi freeze.
                    // sin(pitch) = A + B τ meets both: ∫a·s dτ = Δḣ + ∫g', ∫(T-τ)a·s dτ = Δh - ḣT + ∫(T-τ)g', with the
                    // gravity less the centrifugal term g' linear from now to the circular insertion (0); likewise sin(yaw) for z.
                    const double T=p.tgo,g0=earth.mu/(rn*rn)-vh*vh/rn,a11=p.j0,a12=p.j1,a21=T*p.j0-p.j1,a22=T*p.j1-p.j2,det=a11*a22-a12*a21;
                    auto solve=[&](double d1,double d2){return (d1*a22-a12*d2)/det;};
                    const double s=std::clamp(solve(-vv+T*g0/2,height-r.height-vv*T+T*T*g0/3),-.5,.9),y=std::clamp(solve(-vz,-z-vz*T),-.2,.2);
                    steer_=rh*s+(h*std::sqrt(1-y*y)+plane_*y)*std::sqrt(1-s*s);}}
            target=steer_.x==0&&steer_.y==0&&steer_.z==0?x:steer_;
        }
        else target=unit(velocity);
        // Inertial attitude toward the target by ideal torque, scaled by the stage's pitch inertia.
        const Vec e=cross(x,target),eb{dot(column(r,0),e),dot(column(r,1),e),dot(column(r,2),e)};
        const double k=inertia[static_cast<std::size_t>(std::clamp(r.stage,0,4))];
        c.torque=Vec{-2.3*r.rate.x,2*eb.y-2.3*r.rate.y,2*eb.z-2.3*r.rate.z}*k;
        return c;
    }
    static double semiMajor(Vec r,Vec v){return 1/(2/norm(r)-dot(v,v)/earth.mu);}
    // Pitch inertia of each stage's stack (kg m²), for the attitude loop's gain.
    std::array<double,5> inertia{1.1e9,1.8e8,1.75e8,1.6e8,1.8e7};
private:
    static constexpr double pi=3.14159265358979323846,deg=pi/180,azimuth=72.058*deg,height=191.1e3,latitude=28.6084*deg,inclination=32.521*deg;
    static constexpr double axis=6378165+(186.0e3+183.2e3)/2; // The parking orbit's semi-major axis (MPR Table 4-5).
    static Vec unit(Vec a){return a*(1/norm(a));}
    static Vec column(const Reading& r,int i){ // Body axis i in GCRF.
        const double w=r.iw,x=r.ix,y=r.iy,z=r.iz;
        if(i==0)return{1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)};
        if(i==1)return{2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)};
        return{2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)};
    }
    static Vec rotateNedToInertial(const Reading& r,Vec ned){ // Through the body: NED → body (transpose of body → NED) → GCRF.
        const double w=r.qw,x=r.qx,y=r.qy,z=r.qz;
        const Vec n0{1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)},n1{2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)},n2{2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)};
        const Vec body{dot(n0,ned),dot(n1,ned),dot(n2,ned)};
        return column(r,0)*body.x+column(r,1)*body.y+column(r,2)*body.z;
    }
    // Apollo 12's chi commands (Boeing D5-15551(I)-7 Table 4-III, s from liftoff signal, degrees from the vertical).
    Vec tilt(double t)const{
        static constexpr double table[][2]{{0,0},{11.852,-.29},{20.602,-1.64},{28.477,-4.19},{38.977,-9.64},{49.477,-15.96},{59.977,-22.54},{70.477,-29.11},{80.977,-35.77},
            {92.352,-43.58},{102.852,-49.16},{115.1,-54.37},{125.6,-57.98},{136.977,-61.28},{147.477,-64.02},{157.102,-66.47}};
        constexpr int n=sizeof table/sizeof table[0];const double s=t-.63;int i=1;while(i<n-1&&table[i][0]<s)++i;
        const double chi=(table[i-1][1]+(table[i][1]-table[i-1][1])*(s-table[i-1][0])/(table[i][0]-table[i-1][0]))*deg; // Extrapolated past the last entry.
        return up_*std::cos(chi)+down_*std::sin(-chi);
    }
    // The burns left to gain `dv` (m/s): the current S-II thrust levels as scheduled, then the S-IVB, by the rocket
    // equation; time to go and the moments ∫a τ^k dτ (k = 0, 1, 2) of the thrust acceleration over it.
    struct Plan {double tgo=0,j0=0,j1=0,j2=0;};
    Plan plan(const Reading& r,double dv,double t)const{
        struct Burn {double until,thrust,exhaust;};
        static constexpr double ii=423.2*9.80665,ivb=428.7*9.80665,full=5102547; // S-II: Albert's AS-506 stage.
        static constexpr Burn burns[]{{460.62,full,ii},{498.,full*4093107/5141516,ii},{1e9,full*3082769/5141516,ii}};
        Plan p;double m=r.mass,left=r.propellant;
        auto burn=[&](double span,double thrust,double exhaust){ // Steps of 0.5 s at most; false once dv is reached.
            const double flow=thrust/exhaust;
            for(double done=0;done<span;){const double h=std::min(.5,span-done),gain=exhaust*std::log(m/(m-flow*h));
                const double step=gain>=dv?m/flow*(1-std::exp(-dv/exhaust)):h,a=thrust/(m-flow*step/2),mid=p.tgo+step/2;
                p.j0+=a*step;p.j1+=a*mid*step;p.j2+=a*mid*mid*step;p.tgo+=step;m-=flow*step;dv-=std::min(gain,dv);done+=step;
                if(dv<=0)return false;}
            return true;};
        if(r.stage<=3){for(const Burn& b:burns){if(t>=b.until)continue;const double flow=b.thrust/b.exhaust,span=std::min(b.until-t,left/flow);
                if(!burn(span,b.thrust,b.exhaust))return p;left-=flow*span;t=b.until;if(left<=1e-6)break;}
            m=166571;p.tgo+=5.5;} // Staging gap to the S-IVB at full thrust.
        burn((r.stage<=3?107095.:r.propellant)*ivb/901223,901223,ivb);return p; // Past its propellant: the time to burnout.
    }
    Phase phase_=Phase::pad;Vec up_,down_,pole_,steer_,hold_,plane_;double last_=-1,start_=release;bool resumed_=false;
};
}
