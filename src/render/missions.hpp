#pragma once
#include <mutex>
#include <string>
#include <thread>

namespace albert::missions {
struct StarshipOptions {bool followBooster=false,accelerateOrbit=true;};
enum class State {Idle,Starting,Running,Completed,Stopping,Failed};
struct Status {State state=State::Idle;std::string message;bool active=false;};
enum class Phase {Countdown,Ascent,Separated,BoosterLanding,Orbit,Entry,ShipLanding,Finished};
struct Playback {double speed=1;Phase phase=Phase::Countdown;int target=-1;double resumeSpeed=1;};

// Owns the flight computer, compiled into Albert. All API work and cleanup run
// off the render thread; stop() only requests cancellation.
class Runner {
public:
    static constexpr double maximumSpeed=60.;
    ~Runner();
    bool startStarship(unsigned port,StarshipOptions options={});
    void stop();
    Status status()const;
    Playback playback()const;
    void speed(double multiplier); // 0 pauses; explicit speeds cancel an advance.
    void resume(); // Restores the last nonzero manual speed; advances resume at 1x.
    void advanceTo(Phase phase); // Forward only; real physics, stopping at the milestone.
private:
    mutable std::mutex mutex_;
    Status status_;
    Playback playback_; // One snapshot: pause/resume cannot race a milestone's reset.
    std::jthread worker_;
};
}
