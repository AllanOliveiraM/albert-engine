#include "core/local_system.hpp"
#include "core/bodies.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace albert::core;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double norm(Vector p){return std::hypot(p.x,p.y,p.z);}
int main(){try{
    struct Reference {const char* id;Vector position,velocity;};
    const Reference references[]{
#include "local_orbits_fixture.inc"
    };
    require(localOrbits().size()==33,"Curated local catalogue incomplete");
    for(const auto& reference:references){const auto* orbit=localOrbit(reference.id);require(orbit,"Missing offline descriptor");const auto state=localState(*orbit,orbit->epoch);
        // Independent Horizons vector output, not a value derived by this propagator.
        require(norm(state.positionMeters-reference.position)<std::max(.05,orbit->a*2e-11),"Local frame/epoch differs from Horizons reference");
        require(norm(state.velocityMetersPerSecond-reference.velocity)<.00001,"Local velocity differs from Horizons reference");
        const auto later=localState(*orbit,orbit->epoch+orbit->period/86400.);
        require(norm(state.positionMeters-later.positionMeters)<std::max(10.,norm(state.velocityMetersPerSecond)*.001),"Orbital closure failed");
        for(double fraction:{-.4,-.1,.1,.4}){const auto p=localState(*orbit,orbit->epoch+fraction*orbit->period/86400.).positionMeters;require(norm(p)>=orbit->a*(1-orbit->e)*.999999&&norm(p)<=orbit->a*(1+orbit->e)*1.000001,"Two-body ellipse exceeded physical radial bounds");}
    }
    require(normalizedName("MERC\xc3\x9aRIO / Plut\xc3\xa3o")=="mercurio plutao","Portuguese search normalization failed");
    require(matchScore("euorpa","Europa","europa")>0,"Transposed letters were not matched");
    require(matchScore("tita","Titan","titan")>matchScore("titam","Titan","titan"),"Prefix must precede fuzzy matches");
    require(matchScore("zzzzzz","Terra","earth")==-1,"Unrelated names matched");
    Scene scene;for(const auto& body:naturalBodies())scene.include(body);includeLocalSystem(scene);
    const auto phobosSize=scene.get("phobos").appearance.sizeMeters;
    const auto* phobosSurface=surfaceBody("phobos");
    require(phobosSize.x==phobosSurface->shapeDimensionsMeters[0]&&phobosSize.y==phobosSurface->shapeDimensionsMeters[1]&&phobosSize.z==phobosSurface->shapeDimensionsMeters[2]&&scene.get("phobos").appearance.shape==Shape::Builtin&&scene.get("phobos").radiusMeters==localOrbit("phobos")->radius&&scene.get("phobos").minDistanceMeters>=phobosSurface->shapeRadiusMeters*1.04,"Measured dimensions, nominal datum or camera clearance lost");
    scene.builtinState("saturn",{1,{1e12,2e11,3e10}});scene.builtinState("sun",{1,{1e11,0,0}});
    updateLocalSystem(scene,1,2461329.5);
    const auto* titan=localOrbit("titan");const auto relative=localState(*titan,titan->epoch).positionMeters;
    require(norm(scene.evaluate("titan",1).state.positionMeters-scene.evaluate("saturn",1).state.positionMeters-relative)<.01,"Satellite lost its parent frame");
    require(category("titan")==Category::Moon&&category("ceres")==Category::Dwarf&&category("67p")==Category::Comet,"Category mismatch");
    bool protectedBody=false;try{scene.remove("titan");}catch(const std::exception&){protectedBody=true;}require(protectedBody,"Curated body was removable via external commands");
    require(scene.get("earth").minDistanceMeters<earthRadiusMeters*1.02&&scene.get("moon").minDistanceMeters<1760000.,"Extended terrain zoom missing");
    std::cout<<"Local system: 33 Horizons reference states, elliptical closure, parent frames, fuzzy search and categories OK\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
