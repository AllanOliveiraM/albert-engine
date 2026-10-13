#include "visuals.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        // Independent reference values from CSPICE N0067 pxform:
        // IAU_MOON -> J2000, pck00011.tpc; then basis x,z,-y on both sides.
        constexpr double dates[] = {2451545., 2461322.5};
        constexpr float references[][9] = {
            {.7842270521f,-.0226086714f,.6200619153f,.2716514861f,.9109797786f,-.3103567513f,-.5578471125f,.4118309009f,.7205566655f},
            {.9851280433f,.0151201297f,-.1711552511f,-.0783630823f,.9260274705f,-.3692321103f,.1529116268f,.3771531594f,.9134404900f}
        };
        for (size_t d = 0; d < 2; ++d) {
            const auto orientation = albert::moonOrientation(dates[d]);
            for (size_t row = 0; row < 3; ++row) {
                for (size_t col = 0; col < 3; ++col) {
                    if (std::abs(orientation.elements[col * 4 + row] - references[d][row * 3 + col]) > 2e-6f) {
                        throw std::runtime_error("Orientacao lunar difere da referencia NASA/NAIF");
                    }
                }
            }
        }
        if (argc != 2) throw std::runtime_error("Catalogo nao fornecido");
        auto stars = albert::makeStarField(argv[1], 2451545.);
        const auto position = stars->geometry()->getAttribute<float>("position");
        const auto size = stars->geometry()->getAttribute<float>("pointSize");
        if (position->count() != 5070) throw std::runtime_error("Selecao de estrelas incompleta");
        // Sirius is brightest in this subset; the sky should preserve its
        // J2000 direction RA=6h45m, Dec=-16d43m and ICRF graphical transform.
        size_t brightest = 0;
        for (size_t i = 1; i < size->count(); ++i) {
            if (size->getX(i) > size->getX(brightest)) brightest = i;
        }
        threepp::Vector3 direction(position->getX(brightest), position->getY(brightest), position->getZ(brightest));
        direction.normalize();
        const float dec = std::asin(direction.y) * 180.f / 3.14159265359f;
        float ra = std::atan2(-direction.z, direction.x) * 12.f / 3.14159265359f;
        if (ra < 0) ra += 24;
        if (std::abs(ra - 6.75248f) > .001f || std::abs(dec + 16.7161f) > .01f) {
            throw std::runtime_error("Direcao de Sirius incorreta");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
