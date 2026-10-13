#include "download.hpp"

#include <curl/curl.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <utility>

namespace albert {

void DownloadProgress::stage(std::string name, int completed) {
    checkCancelled();
    std::lock_guard lock(mutex_);
    snapshot_ = {std::move(name), completed, 0, 0};
}

void DownloadProgress::transfer(std::int64_t received, std::int64_t total) {
    std::lock_guard lock(mutex_);
    snapshot_.received = received;
    snapshot_.total = total;
}

DownloadSnapshot DownloadProgress::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

void DownloadProgress::checkCancelled() const {
    if (cancelled.load()) throw std::runtime_error("Download cancelado");
}

namespace {
std::size_t writeString(char* data, std::size_t size, std::size_t count, void* user) noexcept {
    try {
        const auto bytes = size * count;
        static_cast<std::string*>(user)->append(data, bytes);
        return bytes;
    } catch (...) {
        return 0; // Report allocation failures through CURL, never across C ABI.
    }
}

int reportProgress(void* user, curl_off_t total, curl_off_t received,
                   curl_off_t, curl_off_t) noexcept {
    auto& progress = *static_cast<DownloadProgress*>(user);
    if (progress.cancelled.load()) return 1;
    try {
        progress.transfer(received, total);
        return 0;
    } catch (...) {
        return 1;
    }
}
} // namespace

std::string httpGet(const std::string& url, DownloadProgress* progress) {
    if (progress) progress->checkCancelled();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("CURL indisponivel");

    std::string response;
    std::array<char, CURL_ERROR_SIZE> error{};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeString);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error.data());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 100L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "AlbertEngine/0.2");
    if (progress) {
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, reportProgress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, progress);
    }

    const auto code = curl_easy_perform(curl.get());
    if (progress) progress->checkCancelled();
    if (code != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP: ") +
            (error[0] ? error.data() : curl_easy_strerror(code)));
    }
    return response;
}

} // namespace albert
