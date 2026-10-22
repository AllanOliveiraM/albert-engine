#pragma once
#include "physics/atmosphere.hpp"
#include "physics/earth.hpp"
#include "physics/gravity.hpp"
#include "physics/thermal.hpp"
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
// Trilinear in Mach, angle (degrees) and deflection (degrees), clamped at the edges.
struct Table3 {
    std::vector<double> mach{0},angle{0},deflection{0},values{0}; // values[(m*angle.size()+a)*deflection.size()+d].
    double operator()(double m,double a,double d)const;
};
// A control surface (flap, fin, elevon): its deflection's limit and actuator rate (rad, rad/s; 0: instant) and the
// coefficient increments it adds to the stage's (same references) against Mach, the angle of attack (sideslip for side
// force and yaw) and the deflection. placed: its lift, drag and side force act at position (x m aft of the nose, y, z), their
// moment taken from there (a flap's drag then yaws and rolls the vehicle by where it is); otherwise at the stage's reference.
// normal (unit, body axes; with normalSet): its lift acts along it instead of square to the air in the pitch plane (a fin's
// normal force).
struct Surface {std::string name;double limit=0,rate=0;Table3 lift,drag,side,roll,pitch,yaw;V3 position;bool placed=false;V3 normal;bool normalSet=false;};
// A reaction-control thruster on the stage's propellant: where (as engines), the direction it pushes the vehicle, thrust
// (N) and exhaust velocity (m/s); pulse: its valve opens and shuts whole, each time for this long (s; the duty asked met on
// average, as a pulse-width modulation does), 0: thrust as smooth as the duty.
struct Thruster {std::string name;V3 position,direction{1,0,0};double thrust=0,exhaust=0,pulse=0;};
// One engine: the centre of its nozzle exit (x m aft of the nose, y right, z down) and the direction it pushes the vehicle at
// zero gimbal (body axes; normalised); vacuum thrust (N), effective exhaust velocity (m/s), nozzle exit area (m²: thrust
// falls by the ambient pressure on it); deepest throttle; gimbal limit (rad, pitch about body y, yaw about body z) and the
// actuators' rate (rad/s, 0: instant).
struct Engine {std::string name;V3 position,direction{1,0,0};double thrust=0,exhaust=0,exitArea=0,minThrottle=1,gimbal=0,gimbalRate=0,start=0; // start: kg an ignition costs.
    double relight=0;unsigned starts=0; // relight: s it stays off before it can start again (0: at once); starts: most ignitions a flight (0: any).
    // Air-breathing (a jet): thrust at full throttle and fuel per newton-second (kg/N/s) from tables of Mach and altitude (km,
    // the tables' angle axis), none without air; the spool follows the throttle with this time constant (s). thrust, exhaust
    // and exitArea are then unused.
    bool breathing=false;Table thrustTable,fuelTable;double spool=0;}; // spool on a rocket: its start and shutdown transient's time constant.
// A wheel (landing gear): its contact point (as legs), strut stiffness and damping, rolling resistance and braking (μ at full
// brake) along its heading, cornering (μ across it), and its steering limit (rad about body z, + to the right; 0: fixed).
struct Wheel {std::string name;V3 position;double stiffness=0,damping=0,rolling=.015,braking=.5,cornering=.8,steer=0;};
// Mass properties of the whole stack while this stage is the lowest, linear in its
// remaining propellant between full and empty. Positions are metres aft of the nose.
struct Stage {
    std::string name;
    double propellant=0,massFull=0,massEmpty=0,cgFull=0,cgEmpty=0,reference=0;
    double header=0,headerAt=0; // Header tanks (kg) holding the last of the propellant, their centre (m aft of the nose).
    V3 inertiaFull,inertiaEmpty;                      // Principal moments about the CG (kg m²).
    std::vector<Engine> engines;                      // All drawing on the stage's propellant; empty: the one engine below.
    double thrust=0,exhaust=0,exitArea=0;             // Shorthand for one engine on the axis: vacuum N, effective exhaust m/s, exit m²,
    double nozzle=0,gimbal=0,minThrottle=1;           // its pivot (m aft), max deflection (rad) and deepest throttle.
    double length=1;                                  // Stack length (m), for display.
    Aerodynamics aero;double noseRadius=1; // Stagnation-point radius for heating (m).
    // Landing legs: contact points (x m aft of the nose, y, z) on spring-dampers against the
    // ground (N/m and N s/m per leg) with regularised Coulomb friction. Without legs touchdown
    // is detected at the CG and the vehicle stops.
    std::vector<V3> legs;double legStiffness=0,legDamping=0,friction=.6;
    std::vector<Wheel> wheels;
    std::vector<V3> hardPoints; // Points (as legs) that strike the environment's structures: catch pins, hooks.
    std::vector<Surface> surfaces;
    // With thrusters the torque command fires them (duty cycles by bounded least squares on the least propellant, preferring
    // couples; they burn propellant and leave whatever force results); without, it is the vehicle's ideal torque.
    std::vector<Thruster> rcs;
    std::vector<ThermalNode> thermal; // Thermal-protection nodes, heated by the flow as the vehicle flies.
    // boiloff: the share of the propellant left lost a day (vented: no thrust, always); residual: what the engines cannot draw
    // (kg: they starve at it); gas: the thrusters' own supply (kg; 0: they burn the propellant).
    double boiloff=0,residual=0,gas=0;
};
// torque: ideal attitude torque limit (N m per axis). inertiaRate scales the İω term and
// jetDamping enables the exhaust momentum flux: 1/true is the complete variable-mass model.
// sunArea, reflectivity: cannonball solar radiation pressure (m², Cr); 0 area disables it.
// crashSpeed: with it the hull is a solid (a capsule from nose to tail, diameter wide) that strikes the ground and the
// structures, breaking up when faster (m/s, along the contact's normal); 0: as before, only legs, wheels and hard points touch.
struct Vehicle {std::string name;std::vector<Stage> stages;double torque=0,inertiaRate=1;bool jetDamping=true;double diameter=1,sunArea=0,reflectivity=1.3,crashSpeed=0;};
// Fraction of the solar disc seen from r past a spherical body of the given radius at the
// origin (conical umbra and penumbra, Montenbruck and Gill 3.4.2), or a sphere just under r
// when r is inside it; sun relative to the body.
double sunlight(V3 r,V3 sun,double bodyRadius);

// Dryden turbulence (MIL-F-8785C): gusts along the air-relative velocity (u), across it horizontally (v) and the third axis
// (w); u a first-order process, v and w the two-pole Dryden filter, driven by seeded white noise. sigma: the vertical gust's
// intensity (m/s; 0: none); scale lengths and the u, v intensities from the height over the ground: below 1000 ft
// L_w = h, L_u = L_v = h / (0.177 + 0.000823 h)^1.2 and σ_u = σ_v = σ / (0.177 + 0.000823 h)^0.4 (h in ft), above 2000 ft
// all 1750 ft (533 m) and σ, linear between.
class Turbulence {
public:
    explicit Turbulence(double sigma=0,std::uint64_t seed=1);
    V3 step(double dt,double airspeed,double height); // Advances dt (s) and returns the gust (u, v, w; m/s).
    V3 gust()const{return{u_,v_[1]+1.7320508075688772*(v_[0]-v_[1]),w_[1]+1.7320508075688772*(w_[0]-w_[1])};}
    double sigma()const{return sigma_;}
    static void scales(double height,double sigma,double& lu,double& lw,double& su,double& sw); // MIL-F-8785C: m, m/s.
private:
    double sigma_;std::uint64_t rng_[4];double u_=0,v_[2]{},w_[2]{};double normal();
};
struct Environment {
    Planet planet=earth;
    Rotation rotation=Rotation::iau(69.184);
    Center center=Center::earth;
    bool atmosphere=true,sunMoon=true; // sunMoon: third bodies of the centre (see thirdBodies).
    bool relativity=true;             // Schwarzschild term of the central body (IERS 2010, 10.12).
    bool wind=true,solarPressure=true; // The air's wind (off: at rest with the ground); radiation pressure on Vehicle::sunArea.
    double turbulence=0;std::uint64_t turbulenceSeed=1; // Dryden gusts' vertical intensity (m/s; 0: none) and their seed.
    std::shared_ptr<const Gravity> field; // Owns planet.field.
    std::shared_ptr<const AirModel> air; // Replaces the standard atmosphere when set.
    double ground=-std::numeric_limits<double>::infinity(); // Ellipsoidal height of the surface (m).
    bool holdDown=false; // The pad's clamps hold until release(), however much it pushes (else they let go once thrust beats weight).
    // water: that surface is water (sea water, 1025 kg/m³): the stack floats by its slices' buoyancy and drag; legs and wheels
    // do not touch it. structures: segments fixed to the body (ECEF ends, m) with a radius, struck by the stages' hard points;
    // a hinged one turns about its axis through its pivot (body-fixed) by an angle from that pose (rad), commanded within
    // [low, high] (Flight::swing) and reached at its rate (rad/s; 0: at once), from start.
    bool water=false;
    struct Structure {std::string name;V3 a,b;double radius=.5,stiffness=1e7,damping=1e5,friction=.5;bool hinged=false;V3 pivot,axis{0,0,1};double rate=0,low=0,high=0,start=0;};
    std::vector<Structure> structures;
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
    double gas=0;              // The thrusters' own gas left (kg), when the stage has a supply.
    double heatLoad=0;         // Stagnation-point heat absorbed since the start (J/m²).
    int stage=0;
    bool burning=false,clamped=false,landed=false,contact=false; // burning: an engine on; contact: a leg on the ground.
    double throttle=1,gimbalPitch=0,gimbalYaw=0; // The last command to every engine. Gimbal + pitch: nose up; + yaw: nose right.
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
    V3 rcsForce,rcsMoment;     // The thrusters' (body N, N m about the CG).
    V3 contactForce;           // From the ground, water and structures (body N).
    double heatFlux=0;         // Convective stagnation-point heating, Sutton-Graves (W/m²).
    V3 airBody;                // The flow: the air's velocity relative to the vehicle, body axes (m/s).
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
    // Engine commands: engine -1 for every engine of the stage, else its index. A failed engine stays off (engine out).
    // silent: it fails but reports itself healthy (its thrust drops all the same).
    void ignite(int engine=-1);void cutoff(int engine=-1);void fail(int engine,bool silent=false);void separate();void release();
    void stick(int engine=-1);void stickSurface(int index=-1); // An actuator jammed where it is: the engine's gimbal, the surface; -1 each one.
    void throttle(double t,int engine=-1);void gimbal(double pitch,double yaw,int engine=-1);void torque(V3 t);
    void surface(double deflection,int index=-1); // rad, clamped to each surface's limit; reached at its rate.
    void thruster(double duty,int index=-1);      // A thruster's duty cycle (0-1) directly, until the next torque command.
    void brake(double fraction,int index=-1);     // Wheel brakes, 0-1.
    void steer(double angle,int index=-1);        // Wheel steering (rad), clamped to each wheel's limit.
    void swing(double angle,int index=-1);        // A hinged structure's angle (rad), clamped to its range; reached at its rate.
    // Interference, until changed: wind added to the weather's at the vehicle (NED, m/s; in air only); an outside force on it
    // (NED, N, at the centre of mass: what its accelerometers feel). terminate: the flight termination system (breaks it up).
    void gust(V3 ned){wind_=ned;}
    void push(V3 ned){push_=ned;}
    void terminate();
    void vent(double rate); // Main tanks vented at `rate` kg/s (0: shut) down to the header tanks' load (or the residuals).
    V3 gust()const{return wind_;}
    V3 push()const{return push_;}
    struct WheelState {double brake=0,steer=0;};
    const std::vector<WheelState>& wheels()const{return wheels_;}
    // Each engine of the stage: on, failed, throttle, gimbal as moved (rad) and as commanded.
    // spool: a jet's (0-1), a rocket's start (0-1); silent: failed, reported healthy; claims: what a silent one says it is
    // doing (lit as commanded); stuck: its gimbal frozen where it is.
    struct EngineState {bool on=false,failed=false;double throttle=1,pitch=0,yaw=0,targetPitch=0,targetYaw=0,spool=0;bool silent=false,claims=false,stuck=false;bool stopped=false;Tt offAt=0;unsigned started=0;}; // stopped: cut off once (offAt then).
    const std::vector<EngineState>& engines()const{return engines_;}
    struct SurfaceState {double deflection=0,target=0;bool stuck=false;}; // stuck: frozen where it is.
    const std::vector<SurfaceState>& surfaces()const{return surfaces_;}
    struct StructureState {double angle=0,target=0;};
    const std::vector<StructureState>& structures()const{return structures_;} // By Environment::structures (fixed ones at 0).
    const std::vector<double>& thrusters()const{return duty_;} // Duty cycles.
    std::vector<double> firing()const; // Each thruster's jet now: its valve (1 open, 0 shut) when it pulses, else its duty.
    double centreOfMass()const; // m aft of the nose, as the propellant left places it.
    bool ready(std::size_t engine)const; // Engine `engine` (0-based) could be lit now: working, starts left, off long enough.
    const std::vector<double>& temperatures()const{return temperatures_;} // Thermal nodes (K), the stage's.
    // A breakup: why (none; the hull struck the ground or a structure; a thermal node past its failure), when and how fast the
    // strike was (m/s). From then on the engines are out and commands do nothing: the pieces fall as the stage did.
    enum class Breakup {none,ground,structure,heat,terminated};
    Breakup breakup()const{return broken_;}
    Tt breakupTime()const{return brokenAt_;}
    double breakupSpeed()const{return brokenSpeed_;}
    std::vector<double> engineThrust()const; // Each engine's thrust now (N, after the ambient pressure).
    const State& state()const{return s_;}
    V3 sunFromVehicle()const{return sun(s_.time)-s_.position;} // Inertial (m).
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
    struct Rate {V3 v,a;Q q;V3 w;double m;V3 f;double heat,gas=0;}; // f: specific force, body axes; heat: W/m²; gas: the thrusters' (kg/s).
    Rate rate(const State& s,Sample* out)const;
    void integrate(Tt h);
    void hold(State& s)const; // Clamped pose rides the rotating Earth.
    V3 tidal(const State& s)const;
    V3 sun(Tt t)const; // Relative to the centre, from the third-body cache.
    double bodies(Tt t)const;
    void slew(double seconds); // Gimbals toward their commands at their rates; the gusts advance.
    Turbulence turbulence_;V3 gustNed_,wind_,push_; // The gust over the step, the added wind (NED, m/s), the outside force (NED, N).
    double flowOf(std::size_t i)const; // Engine i's propellant flow (kg/s).
    void allocate(); // Thruster duties for the torque command.
    void valves(); // The pulsing thrusters' valves opened or shut for their next pulse where one is due.
    void shut(); // Every thruster off: duties and valves.
    double jet(std::size_t i)const{return vehicle_.stages[s_.stage].rcs[i].pulse>0?static_cast<double>(open_[i]):duty_[i];} // Thruster i's thrust share now.
    Vehicle vehicle_;Environment env_;State s_;std::vector<EngineState> engines_;std::vector<SurfaceState> surfaces_;std::vector<double> duty_,temperatures_,credit_;std::vector<char> open_;std::vector<Tt> until_;std::vector<WheelState> wheels_; // credit_: each valve's on-time owed (s); until_: its pulse's end.
    std::vector<StructureState> structures_;struct Segment {V3 a,b,pivot,spin;};std::vector<Segment> segments_; // Where the structures are now (spin: rad/s, body-fixed).
    Tt step_=0,coast_=0;Increments increments_;double vent_=0; // vent_: kg/s.
    Breakup broken_=Breakup::none;Tt brokenAt_=0;double brokenSpeed_=0;
    void breakUp(Breakup why,double speed); // Engines out, thrusters off: what is left falls.
    void strike(); // The hull against the ground and the structures, after a step.
    void heat(double seconds); // The thermal nodes over a step, from its start's flow.
    double structureGap(V3 fixed)const;
    void boil(double seconds); // The stage's cryogens boiling off.
    void place(const std::vector<double>& before={},double seconds=0); // The structures where their angles put them; spin: from `before` over `seconds`.
    V3 padPosition_;Q padAttitude_; // ECEF and body→ECEF while clamped/landed.
    mutable std::int64_t perturbed_=std::numeric_limits<std::int64_t>::min();mutable ThirdBody p0_[2]{},p1_[2]{};mutable int bodies_=0;
};
}
