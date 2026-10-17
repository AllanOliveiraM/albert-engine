#pragma once
#include "asset_pack.hpp"
#include <chrono>
#include <condition_variable>
#include <thread>

namespace albert {
// Background download of the catalogued containers: essential first, then the
// catalogue order, with the focused body's next level ahead of the rest. Each
// container resumes from its .part file, is verified (size and SHA-256) and
// renamed atomically before joining the library. Failures retry with backoff.
class AssetDownloads {
public:
    struct Options {
        std::string baseUrl;                         // .../releases/download
        std::vector<std::chrono::milliseconds> backoff{std::chrono::seconds(5),std::chrono::seconds(15),std::chrono::seconds(45),std::chrono::minutes(2),std::chrono::minutes(5)};
        std::uint64_t reserveBytes=256ull*1024*1024;  // Keep this much disk free.
        long stallSeconds=30;
    };
    struct Item {std::string file,group;int level=0;bool essential=false,done=false,active=false;std::uint64_t bytes=0,received=0;int finished=-1;}; // finished: completion order.
    struct Status {
        std::vector<Item> items;std::uint64_t totalBytes=0,doneBytes=0;
        bool essentialReady=false,complete=false;std::string error;int retrySeconds=0;std::string focus;
    };
    AssetDownloads(AssetCatalog catalog,Options options);
    ~AssetDownloads();
    void start();
    void prioritize(std::string group); // Catalogue group of the focus: body id, or "moons".
    void retry();                      // Retry now, resetting the backoff.
    Status status() const;
    static std::string defaultBaseUrl();
private:
    void run();
    int next() const;
    std::filesystem::path destination() const;
    AssetCatalog catalog_;Options options_;
    mutable std::mutex mutex_;std::condition_variable wake_;
    std::vector<Item> items_;std::string focus_,error_;std::size_t failures_=0;int active_=-1,finished_=0;
    std::chrono::steady_clock::time_point retryAt_{};bool stop_=false,retryNow_=true,preempted_=false;
    std::shared_ptr<DownloadProgress> current_;std::thread worker_;
};
}
