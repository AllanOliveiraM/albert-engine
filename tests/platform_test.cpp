#include "platform.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        const auto epoch = albert::utcTime(0);
        if (epoch.tm_year != 70 || epoch.tm_mon != 0 || epoch.tm_mday != 1 ||
            epoch.tm_hour != 0 || epoch.tm_min != 0 || epoch.tm_sec != 0) {
            throw std::runtime_error("UTC depende indevidamente do fuso local");
        }
        const auto leapDay = albert::utcTime(1709251199);
        if (leapDay.tm_year != 124 || leapDay.tm_mon != 1 || leapDay.tm_mday != 29 ||
            leapDay.tm_hour != 23 || leapDay.tm_min != 59 || leapDay.tm_sec != 59) {
            throw std::runtime_error("Conversao UTC incorreta em ano bissexto");
        }
        const auto path = albert::dataDirectory();
        if (path.empty() || (argc == 2 && path != std::filesystem::path(argv[1]))) {
            throw std::runtime_error("Diretorio de cache incorreto");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
