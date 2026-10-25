#pragma once
// The broadcast's look over the scene, as a launch webcast shows it:
// - each vehicle (left, or right with `right`): its name, speed over the ground (km/h; m/s below 100 m/s), height (km;
//   m below 1 km), its propellant (one share for both tanks, LOX and CH4) and its engines seen from below (lit white by
//   throttle, off dark, failed red);
// - between them the mission's clock and its events on a line around now (the past dimmed).
#include <string>
#include <vector>
namespace albert {
struct BroadcastVehicle {
    std::string label;double speed=0,height=0,propellant=0;bool right=false;
    struct Engine {float y=0,z=0,radius=0,throttle=0;bool on=false,failed=false;};std::vector<Engine> engines; // Nozzles off the axis (body y, z; m).
};
struct BroadcastEvent {std::string name;double time=0;bool planned=false;}; // time: the mission's seconds (T+).
// The latest decision a vehicle's flight software took and why, for the top-left corner (time: the mission's seconds; none: empty what).
struct BroadcastDecision {std::string vehicle,what,why;double time=0;};
void drawBroadcast(const std::vector<BroadcastVehicle>& vehicles,double missionTime,const std::vector<BroadcastEvent>& events,const BroadcastDecision& decision={});
}
