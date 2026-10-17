#pragma once
#include "physics/atmosphere.hpp"
#include "physics/earth.hpp"
#include <cstdint>
#include <limits>
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
};
// torque: ideal attitude torque limit (N m per axis). inertiaRate scales the İω term and
// jetDamping enables the exhaust momentum flux: 1/true is the complete variable-mass model.
struct Vehicle {std::string name;std::vector<Stage> stages;double torque=0,inertiaRate=1;bool jetDamping=true;double diameter=1;};

struct Environment {
    Planet planet=earth;
    Rotation rotation=Rotation::iau(69.184);
    bool atmosphere=true,sunMoon=true;
    double ground=-std::numeric_limits<double>::infinity(); // Ellipsoidal height of the surface (m).
};
// Inertial frame: GCRF (or the check-case frame aligned with Earth at the epoch).
struct State {
    Tt time=0;
    V3 position,velocity;      // CG (m, m/s).
    Q attitude;                // Body (x forward, y right, z down) to inertial.
    V3 rate;                   // Body angular velocity relative to inertial (rad/s).
    double propellant=0;       // Remaining in the active stage (kg).
    int stage=0;
    bool burning=false,clamped=false,landed=false;
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
    // At rest on the ground, clamped until thrust exceeds weight or release().
    static State pad(const Environment& e,const Vehicle& v,Tt time,Geodetic site,double heading,double pitch,double roll=0);
    // Free state from geodetic position, NED velocity relative to Earth and NED Euler angles;
    // body rates relative to Earth (rad/s).
    static State free(const Environment& e,const Vehicle& v,Tt time,Geodetic g,V3 nedVelocity,V3 euler,V3 earthRelativeRate={});
    // Fixed-step RK4 over dt; stops right after propellant depletion or touchdown and
    // returns the time advanced.
    Tt advance(Tt dt);
    void ignite();void cutoff();void separate();void release();
    void throttle(double t);void gimbal(double pitch,double yaw);void torque(V3 t);
    const State& state()const{return s_;}
    const Vehicle& vehicle()const{return vehicle_;}
    const Environment& environment()const{return env_;}
    Sample sample()const;
    double massFlow()const; // Current propellant consumption (kg/s).
    std::uint64_t hash()const;
private:
    struct Rate {V3 v,a;Q q;V3 w;double m;};
    Rate rate(const State& s,Sample* out)const;
    void integrate(Tt h);
    void hold(State& s)const; // Clamped pose rides the rotating Earth.
    V3 tidal(const State& s)const;
    Vehicle vehicle_;Environment env_;State s_;
    V3 padPosition_;Q padAttitude_; // ECEF and body→ECEF while clamped/landed.
    mutable std::int64_t perturbed_=std::numeric_limits<std::int64_t>::min();mutable Perturbers p0_{},p1_{};
};
}
