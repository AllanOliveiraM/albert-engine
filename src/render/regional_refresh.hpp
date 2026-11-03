#pragma once
#include <chrono>
#include <optional>

namespace albert {
// Wall-clock scheduling only: moving the camera must not erase a failed
// source's retry delay. Quiet time groups small crop changes; the maximum
// delay still allows fresh imagery during continuous movement.
class RegionalRefreshSchedule {
public:
    using Clock=std::chrono::steady_clock;
    using Time=Clock::time_point;

    void changed(Time now) {
        if(!firstChange_)firstChange_=now;
        lastChange_=now;
    }
    bool ready(Time now) const {
        if(next_&&now<*next_)return false;
        return !firstChange_||now-*lastChange_>=std::chrono::milliseconds(120)
            ||now-*firstChange_>=std::chrono::milliseconds(500);
    }
    void finished(Time now,bool failed) {
        next_=now+(failed?std::chrono::milliseconds(120000):std::chrono::milliseconds(80));
    }
    void clearChanges() {firstChange_.reset();lastChange_.reset();}
    void reset() {clearChanges();next_.reset();}

private:
    std::optional<Time> firstChange_,lastChange_,next_;
};
}
