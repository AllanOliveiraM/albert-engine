#pragma once
#include "core/bridge.hpp"
#include "physics/atmosphere.hpp"
#include <functional>
#include <memory>
namespace albert::catalog {class Manager;}
namespace albert::rpc {
class Server {
public:
    using Weather=std::function<std::shared_ptr<const physics::AirModel>(double latitude,double longitude,double utc)>; // Default: NOAA GFS.
    Server(core::Bridge& bridge,unsigned port,catalog::Manager* catalog=nullptr,Weather weather={});
    ~Server();
    unsigned port() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
