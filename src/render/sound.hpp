#pragma once
#include "physics/acoustics.hpp"
#include <cstdint>
#include <memory>
namespace albert {
// What the camera hears: noise through all the acoustics' one-third-octave bands, each at the heard level,
// on miniaudio's default playback device (none, or ALBERT_AUDIO=0: silent but still rendered on demand). Levels above
// 95 dB SPL come down over ~1.5 s, as hearing protects itself, and 95 dB plays at -14 dBFS: a launch at the pad and one
// 30 km away both sit in range, the nearer louder. Crackle borrows band power for asymmetric, clustered pressure shocks;
// their bandwidth follows the surviving spectrum so distant ripping becomes rumble. Booms:
// N-waves (2 ms shocks) at their overpressure, through the same gain.
class Sound {
public:
    static constexpr unsigned rate=48000;
    static constexpr unsigned voices=4;
    struct Voice {std::uint64_t id=0;physics::acoustics::Spectrum levels{};bool audible=false;float pan=0,crackle=0;};
    explicit Sound(bool device=true);
    ~Sound();
    Sound(const Sound&)=delete;Sound& operator=(const Sound&)=delete;
    // From the main thread, every frame: the heard levels (dB re 20 µPa; audible false: silence), the source's side
    // (-1 left to 1 right) and the seconds since the last call.
    void hear(const physics::acoustics::Spectrum& levels,bool audible,float pan,double seconds,float crackle=0);
    void hear(const std::array<Voice,voices>& sources,double seconds); // Heard sources retain their own spectrum and side.
    void boom(double overpressure,double duration,float pan,std::uint64_t voice=0); // Pa, s; source id keeps its boom on its voice.
    // Interleaved float frames at `rate`: the device's callback, and offline for tests.
    void render(float* out,unsigned frames,unsigned channels);
    bool playing()const; // A device is open.
    void setVolume(float volume); // 0 to 1 (a gain on the output, before the soft limit's knee).
    double adaptation()const; // dB taken off for loudness.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
