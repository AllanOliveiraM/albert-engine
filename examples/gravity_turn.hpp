#pragma once
// Ascent guidance of the example flight computer, for the Earth, the Moon or Mars: vertical
// rise, pitch kick, zero-incidence gravity turn on the first stage, vertical-rate steering on
// the second (or, off an airless body, a short rise and the steering at once), cutoff once the
// perigee reaches the target. It only reads what Albert's flight API reports and only issues
// its commands.
#include <algorithm>
#include <cmath>
#include <vector>
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
    double iw=1,ix=0,iy=0,iz=0;        // Body to GCRF.
    Vec fixedPosition,fixedVelocity;   // The central body's rotating frame (m, m/s).
    bool contact=false;                // A leg on the ground.
    int stage=0,stages=1;bool burning=false,clamped=false;double propellant=0,mass=0; // kg.
};
struct Commands {bool ignite=false,separate=false,cutoff=false;double gimbalPitch=0,gimbalYaw=0,throttle=-1;Vec torque;}; // throttle < 0: unchanged; separate before ignite.
// The central body: gravitational parameter (m³/s²) and the radius perigees are counted from (m).
struct Body {double mu,radius;};
constexpr Body earth{3.986004418e14,6378137},moon{4.9028001e12,1737400},mars{4.282837e13,3396190};
class GravityTurn {
public:
    enum class Phase {pad,vertical,kick,turn,coast,separated,upper,orbit,toApoapsis,circularize};
    // steered: no gravity turn, for airless or thin air (the Moon, Mars): a 50 m vertical rise, then
    // the vertical-rate steering from liftoff, staging when a stage burns out (an Apollo LM ascent).
    // apoapsis: keep burning past the perigee target until the apoapsis reaches it (an Apollo-like
    // insertion above circular speed, 17 x 88 km off the Moon); 0: cutoff at the perigee target.
    // climb: the vertical-rate steering's climb rate toward `height` over this time constant (s; at
    // most maxClimb m/s), which levels a steep ascent (Mars) off; 0: ballistic-like from its start.
    // circularize: two burns (a Mars ascent): cutoff once the apoapsis reaches its target, coast to it,
    // then burn level until the perigee reaches its target.
    explicit GravityTurn(double heading,double kick=.14,double height=200e3,double perigee=180e3,Body body=earth,bool steered=false,double apoapsis=0,double climb=0,double maxClimb=1e9,bool circularize=false)
        :heading_(heading),kick_(kick),target_(height),perigee_(perigee),apoapsis_(apoapsis),climb_(climb),maxClimb_(maxClimb),body_(body),steered_(steered),circularize_(circularize){}
    static double apoapsis(Vec r,Vec v,Body b=earth){ // Above the body's radius (m); infinite when unbound.
        const double rn=norm(r),energy=dot(v,v)/2-b.mu/rn;if(energy>=0)return 1e300;
        const double a=-b.mu/(2*energy);return 2*a-perigee(r,v,b)-2*b.radius;
    }
    double kickRate=0; // The pitch kick's rate (rad/s; 0: at once).
    std::vector<std::pair<double,double>> pitch; // After the kick: the flight path angle (rad) by speed over the ground (m/s), as flown; empty: a gravity turn (no angle of attack).
    bool insertion=false;double exhaust=3600; // The upper stage onto its target height climbing nought as it reaches circular speed there (explicit guidance), not by the height alone; its engines' exhaust speed (m/s).
    Phase phase()const{return phase_;}
    void upper(const Reading& r){phase_=Phase::upper;h0_=r.height;rise0_=-r.nedVelocity.z;} // Steering from here (an upper stage lit by the client).
    static double perigee(Vec r,Vec v,Body b=earth){ // Above the body's radius (m).
        const double rn=norm(r),energy=dot(v,v)/2-b.mu/rn,h=norm(cross(r,v));
        const double a=-b.mu/(2*energy),e=std::sqrt(std::max(0.,1+2*energy*h*h/(b.mu*b.mu)));return a*(1-e)-b.radius;
    }
    Commands step(const Reading& r){
        Commands c;const double speed=norm(r.nedVelocity);const Vec up{0,0,-1};
        const Vec along=speed>1?r.nedVelocity*(1/speed):up;Vec target=up;
        switch(phase_){
        case Phase::pad:c.ignite=true;phase_=Phase::vertical;pad_=r.height;break;
        case Phase::vertical:
            if(steered_&&r.height-pad_>50){phase_=Phase::upper;h0_=r.height;rise0_=-r.nedVelocity.z;}
            else if(!steered_&&speed>60){phase_=Phase::kick;mark_=r.elapsed;}break;
        case Phase::kick:{ // At once, or eased over at kickRate (rad/s) as the real pitch-overs are.
            const double k=kickRate>0?std::min(kick_,kickRate*(r.elapsed-mark_)):kick_;
            target={std::cos(heading_)*std::sin(k),std::sin(heading_)*std::sin(k),-std::cos(k)};
            if(std::acos(std::clamp(-along.z,-1.,1.))>kick_||r.elapsed-mark_>(kickRate>0?kick_/kickRate+5:15))phase_=Phase::turn;break;}
        case Phase::turn:target=along;
            if(!pitch.empty()&&speed>1){ // A flown pitch program: the flight path angle by speed, reached within a few degrees of attack (2° below 25 km: the thick air, max-Q).
                const double gamma=std::asin(std::clamp(-along.z,-1.,1.)),most=(r.height<25e3?2:4)*.0174533;double want=pitch.back().second;
                for(std::size_t i=0;i+1<pitch.size();++i)if(speed<=pitch[i+1].first){const auto [v0,g0]=pitch[i];const auto [v1,g1]=pitch[i+1];want=speed<=v0?g0:g0+(g1-g0)*(speed-v0)/(v1-v0);break;}
                const double g=std::clamp(want,gamma-most,gamma+most),psi=std::hypot(along.x,along.y)>1e-6?std::atan2(along.y,along.x):heading_;target={std::cos(g)*std::cos(psi),std::cos(g)*std::sin(psi),-std::sin(g)};}
            if(!r.burning&&r.propellant<=0){phase_=Phase::coast;mark_=r.elapsed;}break;
        case Phase::coast:target=along;if(r.elapsed-mark_>2){c.separate=true;phase_=Phase::separated;mark_=r.elapsed;}break;
        case Phase::separated:target=along;if(r.elapsed-mark_>1){c.ignite=true;phase_=Phase::upper;h0_=r.height;rise0_=-r.nedVelocity.z;}break;
        case Phase::upper:{
            const double rn=norm(r.position),rise=-r.nedVelocity.z,v2=dot(r.velocity,r.velocity);
            // Wanted climb rate: toward the target altitude over the climb time constant, else
            // ballistic-like from the upper stage's start.
            const double wanted=climb_>0?std::clamp((target_-r.height)/climb_,0.,maxClimb_):h0_<target_?rise0_*std::sqrt(std::clamp((target_-r.height)/(target_-h0_),0.,1.)):0.;
            const double thrust=std::max(norm(r.specificForce),1e-3);double vertical=body_.mu/(rn*rn)-(v2-rise*rise)/rn+.05*(wanted-rise);
            if(insertion){ // Explicit (PEG-like): the time to go from the speed across still short of circular at the target height, at the
                // thrust's acceleration now and the exhaust's speed (the rocket equation); the vertical channel by E-guidance onto that
                // height, climbing nought, at that time.
                const double vc=std::sqrt(body_.mu/(body_.radius+target_)),dv=std::max(vc-std::sqrt(std::max(v2-rise*rise,0.)),0.),tgo=std::max(exhaust/thrust*(1-std::exp(-dv/exhaust)),5.); // The rocket equation: lighter, it gets there sooner.
                vertical=body_.mu/(rn*rn)-(v2-rise*rise)/rn+6*(target_-r.height-rise*tgo)/(tgo*tgo)+2*rise/tgo;
                if(tgo<10&&held_>-2)vertical=held_*thrust;else held_=std::clamp(vertical/thrust,-.3,.9);} // Its last 10 s at the attitude it had (PEG's: no chasing a vanishing time to go).
            const double horizontal=std::hypot(r.nedVelocity.x,r.nedVelocity.y);
            const double s=std::clamp(vertical/thrust,-.3,.9),cp=std::sqrt(1-s*s),psi=horizontal<10?heading_:std::atan2(r.nedVelocity.y,r.nedVelocity.x);
            target={cp*std::cos(psi),cp*std::sin(psi),-s};
            if(circularize_&&apoapsis(r.position,r.velocity,body_)>=apoapsis_){c.cutoff=true;phase_=Phase::toApoapsis;}
            else if(!circularize_&&perigee(r.position,r.velocity,body_)>=perigee_&&apoapsis(r.position,r.velocity,body_)>=apoapsis_){c.cutoff=true;phase_=Phase::orbit;}
            else if(!r.burning&&r.propellant<=0){if(r.stage+1<r.stages){phase_=Phase::coast;mark_=r.elapsed;}else{c.cutoff=true;phase_=Phase::orbit;}} // Staging, then steering on.
            break;}
        case Phase::orbit:target=along;break;
        case Phase::toApoapsis:target=along;if(-r.nedVelocity.z<=0){c.ignite=true;phase_=Phase::circularize;}break; // Coast up to the apoapsis.
        case Phase::circularize:{ // Level, prograde, until the perigee is raised.
            const double rn=norm(r.position),rise=-r.nedVelocity.z,v2=dot(r.velocity,r.velocity),thrust=std::max(norm(r.specificForce),1e-3);
            const double s=std::clamp((body_.mu/(rn*rn)-(v2-rise*rise)/rn-.2*rise)/thrust,-.3,.9),cp=std::sqrt(1-s*s),psi=std::atan2(r.nedVelocity.y,r.nedVelocity.x);
            target={cp*std::cos(psi),cp*std::sin(psi),-s};
            if(perigee(r.position,r.velocity,body_)>=perigee_||(!r.burning&&r.propellant<=0)){c.cutoff=true;phase_=Phase::orbit;}
            break;}
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
    Phase phase_=Phase::pad;double held_=-9,heading_,kick_,target_,perigee_,apoapsis_,climb_,maxClimb_,mark_=0,h0_=0,rise0_=0,pad_=0;Body body_;bool steered_,circularize_;
};
}
