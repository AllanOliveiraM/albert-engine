#pragma once
#include "physics/acoustics.hpp"
#include <memory>
namespace albert {
// What the camera hears: white noise through the acoustics' one-third-octave bands from 20 Hz up, each at the heard level,
// on miniaudio's default playback device (none, or ALBERT_AUDIO=0: silent but still rendered on demand). Levels above
// 95 dB SPL come down over ~1.5 s, as hearing protects itself, and 95 dB plays at -14 dBFS: a launch at the pad and one
// 30 km away both sit in range, the nearer louder.
class Sound {
public:
    static constexpr unsigned rate=48000;
    explicit Sound(bool device=true);
    ~Sound();
    Sound(const Sound&)=delete;Sound& operator=(const Sound&)=delete;
    // From the main thread, every frame: the heard levels (dB re 20 µPa; audible false: silence), the source's side
    // (-1 left to 1 right) and the seconds since the last call.
    void hear(const physics::acoustics::Spectrum& levels,bool audible,float pan,double seconds);
    // Interleaved float frames at `rate`: the device's callback, and offline for tests.
    void render(float* out,unsigned frames,unsigned channels);
    bool playing()const; // A device is open.
    double adaptation()const; // dB taken off for loudness.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
