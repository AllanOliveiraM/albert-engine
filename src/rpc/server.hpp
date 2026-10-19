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
    std::optional<FlightServices::Sound> flightSound()const; // See FlightServices::sound.
    void physicsDefaults(const ::albert::v1::PhysicsSettings& defaults); // See FlightServices::physicsDefaults.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
