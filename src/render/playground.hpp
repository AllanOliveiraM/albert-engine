#pragma once
// The interference panel ("Interferência"): live faults and disturbances on the flights, through the flight API's own
// commands (rpc::Server::inject), as a test or an operator would cause them:
// - wind gusts and outside forces by two joysticks (north/east) and their vertical sliders;
// - the selected flight's engines by its engine map: a click fails one, fails it silently (it says it is fine) or jams its
//   gimbal, as chosen; a right click fails it silently; its surfaces jammed where they are;
// - each sensor made to fail or lie (dead, stuck, biased, drifting, noisy), each IMU unit apart;
// - the operator's uplinks: HOLD and GO (the countdown), ABORT (the flight software takes its safest way out for its phase),
//   any message, and the flight termination (two clicks).
#include "rpc/server.hpp"
#include <imgui.h>
namespace albert {
void drawPlayground(rpc::Server& server,ImVec2 display,float margin);
}
