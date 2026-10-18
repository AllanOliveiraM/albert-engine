#pragma once
#include "core/bridge.hpp"
#include "physics/atmosphere.hpp"
#include "albert/v1/engine.grpc.pb.h"
#include <functional>
#include <memory>
namespace albert::rpc {
// Lockstep flight API over physics::Flight; mirrors the vehicle into the scene.
class FlightServices final:public ::albert::v1::FlightService::Service {
public:
    // Live air around a site (degrees) at an instant (Unix UTC), default NOAA GFS; upper or
    // climatological air at an instant, default NRLMSISE-00 with CelesTrak indices (null: none).
    struct AirSources {
        std::function<std::shared_ptr<const physics::AirModel>(double latitude,double longitude,double utc)> weather;
        std::function<std::shared_ptr<const physics::AirModel>(double utc)> upper;
    };
    explicit FlightServices(core::Bridge& bridge,AirSources sources={});
    ~FlightServices() override;
    grpc::Status CreateFlight(grpc::ServerContext*,const ::albert::v1::CreateFlightRequest*,::albert::v1::FlightSample*) override;
    grpc::Status Step(grpc::ServerContext*,const ::albert::v1::StepRequest*,::albert::v1::FlightSample*) override;
    grpc::Status GetFlight(grpc::ServerContext*,const ::albert::v1::Empty*,::albert::v1::FlightSample*) override;
    grpc::Status EndFlight(grpc::ServerContext*,const ::albert::v1::Empty*,::albert::v1::Applied*) override;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
