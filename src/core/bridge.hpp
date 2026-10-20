#pragma once
#include "scene.hpp"
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
namespace albert::core {
struct InstallModel {std::shared_ptr<const Model> model;};struct RemoveModel {std::string id;};
struct CreateLayer {Layer layer;};struct RemoveLayer {std::string id;};struct LayerVisibility {std::string id;bool enabled;};
// Create's replace: an existing object's definition in place (Scene::create), the camera staying on it.
struct Create {Object object;bool replace=false;};struct Remove {ObjectId id;};struct Visibility {ObjectId id;bool enabled;};
struct Append {ObjectId id;std::vector<Sample> values;};
struct Replace {ObjectId id;std::vector<Sample> values;};
struct Focus {ObjectId id;bool reset=false;};
struct Orbit {double azimuthRadians=0,elevationRadians=0,distanceScale=1;};
struct SetTime {std::optional<double> utcSeconds,rate;bool live=false,hold=false;}; // Unset fields keep their value; hold moves the instant without a discontinuity.
// Latest state of the external flight, for the on-screen panel; active=false hides it.
struct FlightTelemetry {bool active=false,burning=false,clamped=false,landed=false;std::string vehicle,atmosphere,physics;double elapsed=0,height=0,speed=0,mach=0,dynamicPressure=0,mass=0,propellant=0,windNorth=0,windEast=0;int stage=0,stages=0;};
using Command=std::variant<Create,Remove,Visibility,Append,Replace,Focus,Orbit,InstallModel,RemoveModel,CreateLayer,RemoveLayer,LayerVisibility,SetTime,FlightTelemetry>;
enum class Code {Ok,Invalid,NotFound,Exhausted,Unavailable,Cancelled};
struct Reply {Code code=Code::Ok;std::string message;std::uint64_t revision=0;};
struct Ticket {std::future<Reply> result;std::shared_ptr<std::atomic_bool> cancelled;};
class Bridge {
public:
    static constexpr size_t capacity=2048; // Some seconds of a stalled frame loop (a window out of view).
    Ticket submit(Command command);
    size_t waiting() const; // Commands queued: senders of what only draws can hold back when it is more than half full.
    size_t drain(const std::function<Reply(Command&)>& apply);
    void publish(Snapshot snapshot);
    std::shared_ptr<const Snapshot> snapshot() const;
    void close();
    ~Bridge(){close();}
private:
    struct Pending {Command command;std::promise<Reply> promise;std::shared_ptr<std::atomic_bool> cancelled;};
    std::deque<Pending> queue_;
    mutable std::mutex mutex_;
    bool closed_=false;
    std::atomic<std::shared_ptr<const Snapshot>> snapshot_;
};
}
