#pragma once
#include "scene.hpp"
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <variant>
namespace albert::core {
struct InstallModel {std::shared_ptr<const Model> model;};struct RemoveModel {std::string id;};
struct Create {Object object;};struct Remove {ObjectId id;};struct Visibility {ObjectId id;bool enabled;};
struct Append {ObjectId id;std::vector<Sample> values;};
struct Replace {ObjectId id;std::vector<Sample> values;};
struct Focus {ObjectId id;bool reset=false;};
struct Orbit {double azimuthRadians=0,elevationRadians=0,distanceScale=1;};
using Command=std::variant<Create,Remove,Visibility,Append,Replace,Focus,Orbit,InstallModel,RemoveModel>;
enum class Code {Ok,Invalid,NotFound,Exhausted,Unavailable,Cancelled};
struct Reply {Code code=Code::Ok;std::string message;std::uint64_t revision=0;};
struct Ticket {std::future<Reply> result;std::shared_ptr<std::atomic_bool> cancelled;};
class Bridge {
public:
    static constexpr size_t capacity=256;
    Ticket submit(Command command);
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
