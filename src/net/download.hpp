#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace albert {

struct DownloadSnapshot {
    std::string stage = "Preparando downloads";
    int completed = 0;
    std::int64_t received = 0;
    std::int64_t total = 0;
};

class DownloadProgress {
public:
    std::atomic_bool cancelled{false};

    void stage(std::string name, int completed);
    void transfer(std::int64_t received, std::int64_t total);
    [[nodiscard]] DownloadSnapshot snapshot() const;
    void checkCancelled() const;

private:
    mutable std::mutex mutex_;
    DownloadSnapshot snapshot_;
};

// Verifies TLS, checks HTTP status, supports cancellation, and reports byte
// progress when the server sends Content-Length. Callbacks never touch ImGui.
struct HttpOptions {std::size_t maximumBytes=128*1024*1024;long timeoutSeconds=90;std::string range;}; // range: "first-last" bytes; the answer must be partial.
std::string httpGet(const std::string& url, DownloadProgress* progress = nullptr,HttpOptions options={});
// Streams into `partial`, resuming with an HTTP Range request from its current
// size. A server that ignores the range restarts the file from zero; the result
// never exceeds `expectedBytes`. No overall timeout: stalled transfers fail after
// `stallSeconds` below 1 KiB/s. Returns the size of `partial` afterwards.
std::uint64_t httpDownloadFile(const std::string& url,const std::filesystem::path& partial,std::uint64_t expectedBytes,DownloadProgress* progress=nullptr,long stallSeconds=30);

} // namespace albert
