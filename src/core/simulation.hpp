#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace albert::time {
// Simulated astronomical instant, in Unix UTC seconds. Live follows the
// disciplined clock; otherwise a chosen instant advances at `rate` simulated
// seconds per monotonic second. The generation changes on discontinuities
// (a chosen date, a return to live), never on a rate change.
struct Simulated {
    double utcSeconds=0,rate=1;
    bool live=true;
    bool limited=false; // Held at the first or last supported instant.
    std::uint64_t generation=0;
};
class Simulation {
public:
    // 1900-01-01 00:00:00 to 2100-12-31 23:59:59 UTC: inside the DE440 planetary
    // ephemerides; before 1960 ERFA treats UTC as uniform time (seconds of error).
    static constexpr double earliestUtc=-2208988800.,latestUtc=4133980799.;
    // One week per second: Mercury and the Moon still fit tables fetched ahead of playback.
    static constexpr double maximumRate=7.*86400.;
    using Now=std::function<double()>;
    // Both clocks are injectable for deterministic tests.
    explicit Simulation(Now utc={},Now monotonic={});
    Simulated read() const;
    void setRate(double simulatedSecondsPerSecond); // Keeps the current instant.
    void jump(double utcSeconds);                   // Keeps the rate; leaves live mode.
    // Both validated before either applies; unset values are kept.
    void choose(std::optional<double> utcSeconds,std::optional<double> rate);
    void stall(double utcSeconds);                  // Holds while data loads; same generation.
    void live();
private:
    Simulated at(double monotonic) const;
    Now utc_,monotonic_;
    mutable std::mutex mutex_;
    double anchorUtc_=0,anchorMonotonic_=0,rate_=1;
    bool live_=true;
    std::uint64_t generation_=0;
};
Simulation& simulation(); // One instant shared by rendering, providers and RPC.

// Proleptic Gregorian civil time; independent of the C library, which rejects
// instants before 1970 on Windows.
struct Civil {int year=1970,month=1,day=1,hour=0,minute=0,second=0;};
Civil civil(double utcSeconds); // Whole seconds, rounded down.
double unixSeconds(const Civil&); // Throws on invalid fields.
// "YYYY-MM-DD", "YYYY-MM-DD HH:MM" or "YYYY-MM-DD HH:MM:SS" ('T' also accepted).
std::optional<double> parseUtc(std::string_view text);
std::string formatUtc(double utcSeconds); // "YYYY-MM-DD HH:MM:SS"
}
