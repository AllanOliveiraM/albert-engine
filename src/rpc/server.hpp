#pragma once
#include "core/bridge.hpp"
#include "air_sources.hpp"
#include "flight.hpp"
#include <functional>
#include <memory>
namespace albert::catalog {class Manager;}
namespace albert::rpc {
class Server {
public:
    Server(core::Bridge& bridge,unsigned port,catalog::Manager* catalog=nullptr,AirSources air={}); // Flight air; defaults: GFS, NRLMSISE-00.
    ~Server();
    unsigned port() const;
    std::vector<FlightServices::Sound> flightSounds()const; // See FlightServices::sounds.
    std::vector<FlightServices::Gauge> flightGauges()const; // See FlightServices::gauges.
    void physicsDefaults(const ::albert::v1::PhysicsSettings& defaults); // See FlightServices::physicsDefaults.
    void inject(const std::string& id,const ::albert::v1::FlightCommand& command); // See FlightServices::inject.
    std::vector<std::string> flights()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
