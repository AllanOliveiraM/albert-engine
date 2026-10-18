#pragma once
#include "core/bridge.hpp"
#include "physics/atmosphere.hpp"
#include <functional>
#include <memory>
namespace albert::catalog {class Manager;}
namespace albert::rpc {
class Server {
public:
    // Live weather and upper/climatological air for flights; defaults: NOAA GFS, NRLMSISE-00.
    struct AirSources {
        std::function<std::shared_ptr<const physics::AirModel>(double latitude,double longitude,double utc)> weather;
        std::function<std::shared_ptr<const physics::AirModel>(double utc)> upper;
    };
    Server(core::Bridge& bridge,unsigned port,catalog::Manager* catalog=nullptr,AirSources air={});
    ~Server();
    unsigned port() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
