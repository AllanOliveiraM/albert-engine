#pragma once
#include "physics/atmosphere.hpp"
#include "physics/earth.hpp"
#include "physics/gravity.hpp"
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>
namespace albert::physics {
// Coefficient table, bilinear in Mach and angle (degrees), clamped at the edges.
struct Table {
    std::vector<double> mach{0},angle{0},values{0}; // values[m*angle.size()+a].
    double operator()(double m,double a)const;
};
// Drag opposes the air velocity, lift is normal to it in the body x-z plane, side
// force acts on body y; moments about the reference point.
// Drag uses the total angle sqrt(α²+β²); damping per radian of (rate·length/2V).
struct Aerodynamics {
    double area=0,length=1,span=1;
    Table lift,drag,side,roll,pitch,yaw;
    double rollDamping=0,pitchDamping=0,yawDamping=0;
};
// Mass properties of the whole stack while this stage is the lowest, linear in its
// remaining propellant between full and empty. Positions are metres aft of the nose.
struct Stage {
    std::string name;
    double propellant=0,massFull=0,massEmpty=0,cgFull=0,cgEmpty=0,reference=0;
    V3 inertiaFull,inertiaEmpty;                      // Principal moments about the CG (kg m²).
    double thrust=0,exhaust=0,exitArea=0;             // Vacuum N, effective exhaust m/s, nozzle exit m².
    double nozzle=0,gimbal=0,minThrottle=1;           // Pivot (m aft), max deflection (rad), deepest throttle.
    double length=1;                                  // Stack length (m), for display.
    Aerodynamics aero;
    // Landing legs: contact points (x m aft of the nose, y, z) on spring-dampers against the
    // ground (N/m and N s/m per leg) with regularised Coulomb friction. Without legs touchdown
    // is detected at the CG and the vehicle stops.
    std::vector<V3> legs;double legStiffness=0,legDamping=0,friction=.6;
};
// torque: ideal attitude torque limit (N m per axis). inertiaRate scales the İω term and
// jetDamping enables the exhaust momentum flux: 1/true is the complete variable-mass model.
// sunArea, reflectivity: cannonball solar radiation pressure (m², Cr); 0 area disables it.
struct Vehicle {std::string name;std::vector<Stage> stages;double torque=0,inertiaRate=1;bool jetDamping=true;double diameter=1,sunArea=0,reflectivity=1.3;};
// Fraction of the solar disc seen from r past a spherical body of the given radius at the
// origin (conical umbra and penumbra, Montenbruck and Gill 3.4.2), or a sphere just under r
// when r is inside it; sun relative to the body.
double sunlight(V3 r,V3 sun,double bodyRadius);

struct Environment {
    Planet planet=earth;
    Rotation rotation=Rotation::iau(69.184);
    Center center=Center::earth;
    bool atmosphere=true,sunMoon=true; // sunMoon: third bodies of the centre (see thirdBodies).
    std::shared_ptr<const Gravity> field; // Owns planet.field.
    std::shared_ptr<const AirModel> air; // Replaces the standard atmosphere when set.
    double ground=-std::numeric_limits<double>::infinity(); // Ellipsoidal height of the surface (m).
};
// Earth (defaults), or the Moon / Mars with their rotation, climatology and a field of the given degree.
Environment environment(Center c,std::shared_ptr<const Gravity> field={},int degree=0);
// Inertial frame: GCRF, or ICRF axes at the Moon or Mars (or the check-case frame aligned with Earth at the epoch).
struct State {
    Tt time=0;
    V3 position,velocity;      // CG (m, m/s).
    Q attitude;                // Body (x forward, y right, z down) to inertial.
    V3 rate;                   // Body angular velocity relative to inertial (rad/s).
    double propellant=0;       // Remaining in the active stage (kg).
    int stage=0;
    bool burning=false,clamped=false,landed=false,contact=false; // contact: a leg on the ground.
    double throttle=1,gimbalPitch=0,gimbalYaw=0; // Gimbal + pitch: nose up; + yaw: nose right.
    V3 torque;                 // Commanded body torque (N m).
};
// Derived quantities at a state; what sensors observe.
struct Sample {
    double mass=0,mach=0,dynamicPressure=0,alpha=0,beta=0,thrust=0,massFlow=0; // α, β rad.
    Air air;Geodetic geodetic{};
    V3 fixedPosition,fixedVelocity,nedVelocity; // ECEF m, m/s; velocity relative to the ground in NED.
    V3 euler;                  // Yaw, pitch, roll relative to local NED (rad, 3-2-1).
    Q nedAttitude;             // Body to local NED.
    V3 specificForce;          // Non-gravitational acceleration, body axes (what an ideal IMU measures).
    V3 aeroForce,aeroMoment,thrustForce,acceleration,gravity; // Body N, N m about the CG; inertial m/s².
};
class Flight {
public:
    Flight(Vehicle vehicle,Environment environment,State state);
    // At rest with the base of the stack on the ground at the site height, clamped until
    // thrust exceeds weight or release(). Touchdown is detected at the CG (no contact model).
    static State pad(const Environment& e,const Vehicle& v,Tt time,Geodetic site,double heading,double pitch,double roll=0);
    // Free state from geodetic position, NED velocity relative to Earth and NED Euler angles;
    // body rates relative to Earth (rad/s).
    static State free(const Environment& e,const Vehicle& v,Tt time,Geodetic g,V3 nedVelocity,V3 euler,V3 earthRelativeRate={});
    // RK4 over dt: one step, or steps of at most `fixed` once steps() is set, lengthened up
    // to `coast` while ballistic and torque-free above the atmosphere. Stops right after release from the
    // pad, propellant depletion or touchdown and returns the time advanced (0 at release).
    Tt advance(Tt dt);
    void steps(Tt fixed,Tt coast=0){step_=fixed;coast_=coast;}
    void ignite();void cutoff();void separate();void release();
    void throttle(double t);void gimbal(double pitch,double yaw);void torque(V3 t);
    const State& state()const{return s_;}
    const Vehicle& vehicle()const{return vehicle_;}
    const Environment& environment()const{return env_;}
    Sample sample()const;
    double massFlow()const; // Current propellant consumption (kg/s).
    std::uint64_t hash()const;
    // Body-axis integrals of specific force and angular rate since the last call (what a
    // strapdown IMU reports as velocity and angle increments), RK4-weighted.
    struct Increments {V3 velocity,angle;double seconds=0;};
    Increments increments(){const Increments r=increments_;increments_={};return r;}
private:
    struct Rate {V3 v,a;Q q;V3 w;double m;V3 f;}; // f: specific force, body axes.
    Rate rate(const State& s,Sample* out)const;
    void integrate(Tt h);
    void hold(State& s)const; // Clamped pose rides the rotating Earth.
    V3 tidal(const State& s)const;
    V3 sun(Tt t)const; // Relative to the centre, from the third-body cache.
    double bodies(Tt t)const;
    Vehicle vehicle_;Environment env_;State s_;Tt step_=0,coast_=0;Increments increments_;
    V3 padPosition_;Q padAttitude_; // ECEF and body→ECEF while clamped/landed.
    mutable std::int64_t perturbed_=std::numeric_limits<std::int64_t>::min();mutable ThirdBody p0_[2]{},p1_[2]{};mutable int bodies_=0;
};
}
