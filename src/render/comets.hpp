#pragma once
#include "core/comets.hpp"
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace albert {
// Dust coma and dust tail of the active comets, from their measured activity (Afρ).
// Coma: steady 1/b column inside the radiation-pressure (fountain) envelope. Tail:
// Finson-Probstein grains drawn as a sheet whose albedo cross-section per area sets
// its brightness. Physical brightness by default, as in short exposures, where both
// are nearly invisible; the optional stretch uses an asinh scale, as astrophotography
// does, so the tail, orders of magnitude fainter, shows with the coma. Ion tail: CO+
// fluorescence along the aberrated solar-wind axis (core::ionColumn), as a ribbon.
class CometVisuals {
public:
    explicit CometVisuals(threepp::Scene& scene);
    ~CometVisuals();
    double afrho(const std::string& id) const; // Current Afρ (m); 0 inactive or unknown.
    double distanceAu(const std::string& id) const; // Heliocentric, at the last update.
    bool known(const std::string& id) const{return comets_.contains(id);}
    bool enhanced(const std::string& id) const;
    void enhance(const std::string& id,bool enhanced);
    // Nuclei and the Sun in render space (Earth radii, floating origin); heliocentric
    // states of the nuclei, from tables or elements, drive activity and the grains.
    void update(const threepp::PerspectiveCamera& camera,const threepp::Vector3& sun,
                const std::map<std::string,threepp::Vector3>& nuclei,
                const std::map<std::string,core::StateVector>& heliocentric,double jdTDB,float seconds);
private:
    struct Comet;
    threepp::Scene& scene_;
    std::shared_ptr<threepp::Texture> phase_;
    std::map<std::string,std::unique_ptr<Comet>> comets_;
};
}
