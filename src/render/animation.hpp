#pragma once
#include <algorithm>
#include <cmath>

namespace albert {
// Minimum-jerk flight: zero velocity and acceleration at both endpoints.
inline float flightEase(float progress) {
    const float t=std::clamp(progress,0.f,1.f);
    return t*t*t*(10.f+t*(-15.f+6.f*t));
}
// Interpolate apparent size (inverse distance), instead of distance itself.
// A planet therefore grows gradually even across very different zoom scales.
inline float framingDistance(float from,float to,float progress) {
    if(progress<=0.f)return from;
    if(progress>=1.f)return to;
    return 1.f/std::lerp(1.f/std::max(from,1e-12f),1.f/std::max(to,1e-12f),progress);
}
inline float flightDuration(float scaleChange,float travelInRadii,bool reset=false) {
    if(reset)return std::clamp(1.1f+.10f*scaleChange,1.1f,1.8f);
    return std::clamp(2.2f+.13f*scaleChange+.14f*std::log1p(std::max(0.f,travelInRadii)),2.2f,4.8f);
}
// About 95% of the change in 240 ms, independent of frame rate. A reversal
// starts from the displayed value; physical states never pass through this.
inline float animateValue(float current, float target, float seconds) {
    const float next=std::lerp(current,target,-std::expm1(-std::max(0.f,seconds)/.08f));
    return std::abs(next-target)<.0005f ? target : next;
}
}
