#pragma once
#include "core/bridge.hpp"
#include "providers/horizons.hpp"
#include "providers/small_bodies.hpp"
#include <span>
namespace albert::catalog {
struct Entry {std::string id,name;int horizonsId;double radiusMeters;std::uint32_t color;};
std::span<const Entry> entries();
const Entry& entry(std::string_view id);
enum class Stage {Queued,Loading,Applying,Tracking,Degraded,Failed,Removed};
struct Import {std::string catalogId,objectId;bool focus=false;std::optional<std::uint32_t> color;};
struct Job {std::uint64_t id=0;std::string catalogId,objectId;Stage stage=Stage::Queued;std::string error;double fetchedUtc=0,validUntilUtc=0;std::uint64_t revision=0;};
class Manager {
public:
    using Now=std::function<double()>;
    Manager(core::Bridge&,providers::Horizons&,Now utcNow={},providers::SmallBodies::Transport discovery={});
    ~Manager();
    std::uint64_t importObject(Import request);
    std::vector<Job> jobs() const;
    Job job(std::uint64_t id) const;
    bool active() const;
    bool busy() const;
    std::vector<Entry> search(const std::string& query,DownloadProgress* progress=nullptr);
    std::uint64_t retry(std::uint64_t id);
    void close();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
const char* stageName(Stage);
}
