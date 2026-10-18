#pragma once
#include "core/bridge.hpp"
#include "air_sources.hpp"
#include "albert/v1/engine.grpc.pb.h"
#include <functional>
#include <memory>
namespace albert::rpc {
// Lockstep flight API over physics::Flight; mirrors the vehicle into the scene.
class FlightServices final:public ::albert::v1::FlightService::Service {
public:
    explicit FlightServices(core::Bridge& bridge,AirSources sources={});
    ~FlightServices() override;
    grpc::Status CreateFlight(grpc::ServerContext*,const ::albert::v1::CreateFlightRequest*,::albert::v1::FlightSample*) override;
    grpc::Status Step(grpc::ServerContext*,const ::albert::v1::StepRequest*,::albert::v1::FlightSample*) override;
    grpc::Status GetFlight(grpc::ServerContext*,const ::albert::v1::Empty*,::albert::v1::FlightSample*) override;
    grpc::Status EndFlight(grpc::ServerContext*,const ::albert::v1::Empty*,::albert::v1::Applied*) override;
    grpc::Status ExportFlight(grpc::ServerContext*,const ::albert::v1::Empty*,grpc::ServerWriter<::albert::v1::FlightLogChunk>*) override;
    grpc::Status ReplayFlight(grpc::ServerContext*,grpc::ServerReader<::albert::v1::FlightLogChunk>*,::albert::v1::FlightSample*) override;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
