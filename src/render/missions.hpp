#pragma once
#include <mutex>
#include <atomic>
#include <string>
#include <thread>

namespace albert::missions {
struct StarshipOptions {bool followBooster=false,accelerateOrbit=true;};
enum class State {Idle,Starting,Running,Completed,Stopping,Failed};
struct Status {State state=State::Idle;std::string message;bool active=false;};
enum class Phase {Countdown,Ascent,Separated,BoosterLanding,Orbit,Entry,ShipLanding,Finished};
struct Playback {double speed=1;Phase phase=Phase::Countdown;int target=-1;};

// Owns the flight computer, compiled into Albert. All API work and cleanup run
// off the render thread; stop() only requests cancellation.
class Runner {
public:
    ~Runner();
    bool startStarship(unsigned port,StarshipOptions options={});
    void stop();
    Status status()const;
    Playback playback()const;
    void speed(double multiplier); // 0 pauses; explicit speeds cancel an advance.
    void advanceTo(Phase phase); // Forward only; real physics, stopping at the milestone.
private:
    mutable std::mutex mutex_;
    Status status_;
    std::atomic<double> speed_{1};
    std::atomic<int> phase_{0},target_{-1};
    std::jthread worker_;
};
}
