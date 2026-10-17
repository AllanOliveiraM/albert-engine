#include "visuals.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
        // Proper motion follows the simulated date: Toliman (Alpha Centauri B, ~3.7 arcsec
        // a year) moves about 6.2 arcmin by 1900,
        // from its catalogue row, against the attributes and epoch the shader uses.
        std::ifstream csv(argv[1]);std::string line;std::getline(csv,line);size_t index=0;std::vector<double> row;
        for(;std::getline(csv,line);++index)if(line.find(",Toliman,")!=std::string::npos){std::istringstream fields(line);std::string field;while(std::getline(fields,field,','))row.push_back(field.empty()?0:std::atof(field.c_str()));break;}
        if(row.size()!=10)throw std::runtime_error("Toliman ausente do catalogo");
        const double jd1900=2415020.5,years=(jd1900-2451545.)/365.25;
        albert::setStarEpoch(*stars,jd1900);
        const auto motion=stars->geometry()->getAttribute<float>("motion");
        const float shaderYears=stars->material()->as<threepp::ShaderMaterial>()->uniforms.at("years").value<float>();
        if(std::abs(shaderYears-years)>1e-4)throw std::runtime_error("Epoca estelar nao aplicada");
        auto direction1900=[&](double x,double y,double z){const double r=std::sqrt(x*x+y*y+z*z);return std::array<double,3>{x/r,y/r,z/r};};
        const auto expected=direction1900(row[4]+row[7]*years,row[6]+row[9]*years,-(row[5]+row[8]*years));
        const auto drawn=direction1900(position->getX(index)+motion->getX(index)*shaderYears,position->getY(index)+motion->getY(index)*shaderYears,position->getZ(index)+motion->getZ(index)*shaderYears);
        const auto j2000=direction1900(position->getX(index),position->getY(index),position->getZ(index));
        const double error=std::acos(std::min(1.,expected[0]*drawn[0]+expected[1]*drawn[1]+expected[2]*drawn[2]))*180/3.14159265358979*60;
        const double moved=std::acos(std::min(1.,j2000[0]*drawn[0]+j2000[1]*drawn[1]+j2000[2]*drawn[2]))*180/3.14159265358979*60;
        if(error>.01||moved<5.9||moved>6.4)throw std::runtime_error("Movimento proprio estelar incorreto");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
