#pragma once

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace albert {

inline std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("Caminho do executavel indisponivel");
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
#else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}

inline std::tm utcTime(std::time_t time) {
    std::tm result{};
#ifdef _WIN32
    if (gmtime_s(&result, &time) != 0) {
#else
    if (!gmtime_r(&time, &result)) {
#endif
        throw std::runtime_error("Data UTC fora do intervalo suportado");
    }
    return result;
}

inline std::filesystem::path environmentPath(const char* name) {
#ifdef _WIN32
    // Windows environment paths are UTF-16; preserve non-ASCII user names.
    const std::wstring wideName(name, name + std::char_traits<char>::length(name));
    const auto* value = _wgetenv(wideName.c_str());
#else
    const auto* value = std::getenv(name);
#endif
    return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
}

inline std::filesystem::path dataDirectory() {
    if (auto path = environmentPath("ALBERT_DATA_DIR"); !path.empty()) return path;
    // Portable builds keep their own dynamic cache beside the executable.
    const auto executable = executableDirectory();
    if (std::filesystem::is_directory(executable / "assets")) return executable / "data";
#ifdef _WIN32
    if (auto path = environmentPath("LOCALAPPDATA"); !path.empty()) {
        return path / "AlbertEngine" / "Cache";
    }
#elif defined(__APPLE__)
    if (auto path = environmentPath("HOME"); !path.empty()) {
        return path / "Library" / "Caches" / "albert-engine";
    }
#else
    if (auto path = environmentPath("XDG_CACHE_HOME"); path.is_absolute()) {
        return path / "albert-engine";
    }
    if (auto path = environmentPath("HOME"); !path.empty()) {
        return path / ".cache" / "albert-engine";
    }
#endif
    throw std::runtime_error("Diretorio de cache indisponivel; defina ALBERT_DATA_DIR");
}

inline std::filesystem::path assetDirectory() {
    if (auto path = environmentPath("ALBERT_ASSET_DIR"); !path.empty()) return path;
    return executableDirectory() / "assets";
}

} // namespace albert
