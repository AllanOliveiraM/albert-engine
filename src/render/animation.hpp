#pragma once
#include <algorithm>
#include <cmath>

namespace albert {
// About 95% of the change in 240 ms, independent of frame rate. A reversal
// starts from the displayed value; physical states never pass through this.
inline float animateValue(float current, float target, float seconds) {
    const float next=std::lerp(current,target,-std::expm1(-std::max(0.f,seconds)/.08f));
    return std::abs(next-target)<.0005f ? target : next;
}
}
