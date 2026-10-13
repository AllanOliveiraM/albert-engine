#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
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
struct HttpOptions {std::size_t maximumBytes=128*1024*1024;long timeoutSeconds=90;};
std::string httpGet(const std::string& url, DownloadProgress* progress = nullptr,HttpOptions options={});

} // namespace albert
