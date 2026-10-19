#pragma once
#include "physics/acoustics.hpp"
#include <optional>
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
    grpc::Status GetVehicle(grpc::ServerContext*,const ::albert::v1::VehicleRequest*,::albert::v1::VehicleDefinition*) override;
    grpc::Status Step(grpc::ServerContext*,const ::albert::v1::StepRequest*,::albert::v1::FlightSample*) override;
    grpc::Status GetFlight(grpc::ServerContext*,const ::albert::v1::Empty*,::albert::v1::FlightSample*) override;
    grpc::Status EndFlight(grpc::ServerContext*,const ::albert::v1::Empty*,::albert::v1::Applied*) override;
    grpc::Status ExportFlight(grpc::ServerContext*,const ::albert::v1::Empty*,grpc::ServerWriter<::albert::v1::FlightLogChunk>*) override;
    grpc::Status ReplayFlight(grpc::ServerContext*,grpc::ServerReader<::albert::v1::FlightLogChunk>*,::albert::v1::FlightSample*) override;
    grpc::Status StartRealtime(grpc::ServerContext*,const ::albert::v1::RealtimeRequest*,::albert::v1::RealtimeStatus*) override;
    grpc::Status StopRealtime(grpc::ServerContext*,const ::albert::v1::Empty*,::albert::v1::RealtimeStatus*) override;
    // The flight as a sound source (its rotating frame, the plume along -body x, d_e of its exit area): none without a
    // flight or off the Earth (the acoustics are Earth air's).
    struct Sound {double utc=0;physics::acoustics::Emission emission;physics::Planet planet;std::shared_ptr<const physics::AirModel> air;};
    std::optional<Sound> sound()const;
    void physicsDefaults(const ::albert::v1::PhysicsSettings& defaults); // For flights that leave switches unset (the Física menu).
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
