#pragma once
#include "physics/acoustics.hpp"
#include <optional>
#include "core/bridge.hpp"
#include "air_sources.hpp"
#include "albert/v1/engine.grpc.pb.h"
#include <functional>
#include <memory>
namespace albert::rpc {
// Lockstep flight API over physics::Flight, any number of flights by id; mirrors each vehicle into the scene.
class FlightServices final:public ::albert::v1::FlightService::Service {
public:
    explicit FlightServices(core::Bridge& bridge,AirSources sources={});
    ~FlightServices() override;
    grpc::Status CreateFlight(grpc::ServerContext*,const ::albert::v1::CreateFlightRequest*,::albert::v1::FlightSample*) override;
    grpc::Status GetVehicle(grpc::ServerContext*,const ::albert::v1::VehicleRequest*,::albert::v1::VehicleDefinition*) override;
    grpc::Status Step(grpc::ServerContext*,const ::albert::v1::StepRequest*,::albert::v1::FlightSample*) override;
    grpc::Status GetFlight(grpc::ServerContext*,const ::albert::v1::FlightRequest*,::albert::v1::FlightSample*) override;
    grpc::Status EndFlight(grpc::ServerContext*,const ::albert::v1::FlightRequest*,::albert::v1::Applied*) override;
    grpc::Status ListFlights(grpc::ServerContext*,const ::albert::v1::Empty*,::albert::v1::FlightList*) override;
    grpc::Status ExportFlight(grpc::ServerContext*,const ::albert::v1::FlightRequest*,grpc::ServerWriter<::albert::v1::FlightLogChunk>*) override;
    grpc::Status ReplayFlight(grpc::ServerContext*,grpc::ServerReader<::albert::v1::FlightLogChunk>*,::albert::v1::FlightSample*) override;
    grpc::Status StartRealtime(grpc::ServerContext*,const ::albert::v1::RealtimeRequest*,::albert::v1::RealtimeStatus*) override;
    grpc::Status StopRealtime(grpc::ServerContext*,const ::albert::v1::FlightRequest*,::albert::v1::RealtimeStatus*) override;
    // Each flight on the Earth as a sound source (its rotating frame, the plume along -body x, d_e of its exit area, its flow
    // and booms), by its scene object's id; none off the Earth (the acoustics are Earth air's).
    struct Sound {std::string id;double utc=0;physics::acoustics::Emission emission;physics::Planet planet;std::shared_ptr<const physics::AirModel> air;};
    std::vector<Sound> sounds()const;
    // Each flight as the broadcast shows it: its stage's name, its first instant and now (UTC), speed over the ground (m/s),
    // height (m), the share of the stage's propellant left, each engine (where its nozzle is off the axis and how wide, body
    // y and z in metres; lit, its throttle; failed) and the mission's events it was given (StepRequest.milestones).
    struct Gauge {std::string id,label;double start=0,zero=0,utc=0,speed=0,height=0,propellant=0,stop=0;bool stopped=false; // zero: the mission's T-0 (StepRequest.clock_zero; else start); stop: the UTC its clock stopped at, when stopped (stop_clock, a breakup).
        struct Engine {float y=0,z=0,radius=0,throttle=0;bool on=false,failed=false;};std::vector<Engine> engines;std::vector<std::string> surfaces; // The stage's, by name.
        struct Milestone {std::string name;double utc=0;bool planned=false;};std::vector<Milestone> milestones;};
    std::vector<Gauge> gauges()const;
    void physicsDefaults(const ::albert::v1::PhysicsSettings& defaults); // For flights that leave switches unset (the Física menu).
    // The operator's interference (the app's panel): a command (an engine's failure, a gust, a force, a sensor's fault, the
    // termination, an uplink) applied ahead of the flight program's at that flight's next step; the flights by id.
    void inject(const std::string& id,const ::albert::v1::FlightCommand& command);
    std::vector<std::string> flights()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
