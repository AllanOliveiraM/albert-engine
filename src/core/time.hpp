#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace albert::time {
// UTC/POSIX seconds, not TAI/TDB. Monotonic timestamps include system suspend.
double monotonicSeconds();
enum class Quality { Local, Synchronized, Holdover, Expired };
struct Observation {
    std::string source;
    double utcSeconds=0,monotonicSeconds=0,uncertaintySeconds=0,delaySeconds=0;
    unsigned stratum=0;
};
struct Status {
    Quality quality=Quality::Local;
    double utcSeconds=0,uncertaintySeconds=-1,lastSyncUtc=0,ageSeconds=0,delaySeconds=0,correctionSeconds=0;
    std::uint64_t generation=0;
    bool synchronizing=false,authenticated=false;
    std::string source="local",error;
};
// Pure clock discipline; caller supplies both clocks, allowing deterministic tests.
class Discipline {
public:
    Discipline(double localUtc,double monotonic);
    double utcSeconds(double monotonic) const;
    Status read(double monotonic) const;
    bool synchronize(std::span<const Observation> samples,double monotonic);
    void failed(std::string reason);
private:
    double anchorUtc_,anchorMono_,correction_=0,uncertainty_=-1,lastSyncMono_=0,lastSyncUtc_=0,delay_=0;
    std::uint64_t generation_=0;
    bool external_=false,failed_=false;
    std::string source_="local",error_;
};
using Packet=std::array<unsigned char,48>;
Packet request(std::uint64_t nonce);
Observation decode(std::span<const unsigned char> packet,const Packet& sent,
    double sendMono,double receiveMono,std::string source);
class Service {
public:
    Service();
    ~Service();
    Service(const Service&)=delete;
    Service& operator=(const Service&)=delete;
    double utcSeconds() const;
    Status status() const;
    void resynchronize(); // Rate limited, coalesced; never waits for network.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
Service& clock(); // One reference shared by astronomy, providers, UI and RPC.
const char* qualityName(Quality);
}
