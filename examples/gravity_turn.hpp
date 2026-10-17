#pragma once
// Ascent guidance of the example flight computer for the generic launcher: vertical
// rise, pitch kick, zero-incidence gravity turn on the first stage, vertical-rate
// steering on the second, cutoff once the perigee reaches the target. It only reads
// what Albert's flight API reports and only issues its commands.
#include <algorithm>
#include <cmath>
namespace albert::example {
struct Vec {double x=0,y=0,z=0;};
inline Vec operator+(Vec a,Vec b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec operator*(Vec a,double s){return{a.x*s,a.y*s,a.z*s};}
inline double dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec cross(Vec a,Vec b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double norm(Vec a){return std::sqrt(dot(a,a));}
struct Reading {
    double elapsed=0,height=0;         // s, m above the ellipsoid.
    Vec nedVelocity;                   // Relative to the ground (m/s).
    double qw=1,qx=0,qy=0,qz=0;        // Body to NED.
    Vec rate,specificForce;            // Body axes: IMU (rad/s, m/s²).
    Vec position,velocity;             // GCRF (m, m/s).
    int stage=0;bool burning=false,clamped=false;double propellant=0;
};
struct Commands {bool ignite=false,separate=false,cutoff=false;double gimbalPitch=0,gimbalYaw=0;Vec torque;};
class GravityTurn {
public:
    enum class Phase {pad,vertical,kick,turn,coast,separated,upper,orbit};
    explicit GravityTurn(double heading,double kick=.14,double height=200e3,double perigee=180e3):heading_(heading),kick_(kick),target_(height),perigee_(perigee){}
    Phase phase()const{return phase_;}
    static double perigee(Vec r,Vec v){ // Above the equatorial radius (m).
        constexpr double mu=3.986004418e14;const double rn=norm(r),energy=dot(v,v)/2-mu/rn,h=norm(cross(r,v));
        const double a=-mu/(2*energy),e=std::sqrt(std::max(0.,1+2*energy*h*h/(mu*mu)));return a*(1-e)-6378137.;
    }
    Commands step(const Reading& r){
        Commands c;const double speed=norm(r.nedVelocity);const Vec up{0,0,-1};
        const Vec along=speed>1?r.nedVelocity*(1/speed):up;Vec target=up;
        switch(phase_){
        case Phase::pad:c.ignite=true;phase_=Phase::vertical;break;
        case Phase::vertical:if(speed>60){phase_=Phase::kick;mark_=r.elapsed;}break;
        case Phase::kick:
            target={std::cos(heading_)*std::sin(kick_),std::sin(heading_)*std::sin(kick_),-std::cos(kick_)};
            if(std::acos(std::clamp(-along.z,-1.,1.))>kick_||r.elapsed-mark_>15)phase_=Phase::turn;break;
        case Phase::turn:target=along;if(!r.burning&&r.propellant<=0){phase_=Phase::coast;mark_=r.elapsed;}break;
        case Phase::coast:target=along;if(r.elapsed-mark_>2){c.separate=true;phase_=Phase::separated;mark_=r.elapsed;}break;
        case Phase::separated:target=along;if(r.elapsed-mark_>1){c.ignite=true;phase_=Phase::upper;h0_=r.height;rise0_=-r.nedVelocity.z;}break;
        case Phase::upper:{
            constexpr double mu=3.986004418e14;const double rn=norm(r.position),rise=-r.nedVelocity.z,v2=dot(r.velocity,r.velocity);
            const double wanted=h0_<target_?rise0_*std::sqrt(std::clamp((target_-r.height)/(target_-h0_),0.,1.)):0.; // Ballistic-like.
            const double vertical=mu/(rn*rn)-(v2-rise*rise)/rn+.05*(wanted-rise),thrust=std::max(norm(r.specificForce),1e-3);
            const double s=std::clamp(vertical/thrust,-.3,.9),cp=std::sqrt(1-s*s),psi=std::atan2(r.nedVelocity.y,r.nedVelocity.x);
            target={cp*std::cos(psi),cp*std::sin(psi),-s};
            if(perigee(r.position,r.velocity)>=perigee_||(!r.burning&&r.propellant<=0)){c.cutoff=true;phase_=Phase::orbit;}
            break;}
        case Phase::orbit:target=along;break;
        }
        // PD steering of the body x axis toward the target, by gimbal and attitude torque.
        const double w=r.qw,x=r.qx,y=r.qy,z=r.qz;
        const Vec col0{1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)},col1{2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)},col2{2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)};
        const Vec e=cross(col0,target),eb{dot(col0,e),dot(col1,e),dot(col2,e)};
        constexpr double kp=2,kd=2.3,limit=.0873;
        c.gimbalPitch=std::clamp(kp*eb.y-kd*r.rate.y,-limit,limit);c.gimbalYaw=std::clamp(kp*eb.z-kd*r.rate.z,-limit,limit);
        c.torque=Vec{-r.rate.x,kp*eb.y-kd*r.rate.y,kp*eb.z-kd*r.rate.z}*2e5;
        return c;
    }
private:
    Phase phase_=Phase::pad;double heading_,kick_,target_,perigee_,mark_=0,h0_=0,rise0_=0;
};
}
