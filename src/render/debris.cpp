#include "debris.hpp"
#include "animation.hpp"
#include "core/scene.hpp"
#include <numbers>
#include <random>
using namespace threepp;
namespace {constexpr float au=149597870700./albert::core::earthRadiusMeters;}
albert::DebrisFields::DebrisFields(Scene& scene){
    std::mt19937 random(0x416c6265);std::uniform_real_distribution<float> unit(0,1);
    for(unsigned layer=0;layer<3;++layer){std::vector<float> positions;const unsigned count=layer==0?6000:layer==1?4500:2400;positions.reserve(count*3);
        for(unsigned i=0;i<count;++i){const float angle=unit(random)*2*std::numbers::pi_v<float>;
            float radius,y;
            if(layer<2){const float low=layer==0?2.1f:30.f,high=layer==0?3.3f:50.f;radius=std::sqrt(std::lerp(low*low,high*high,unit(random)));y=(unit(random)+unit(random)-1)*radius*(layer==0?.06f:.16f);}
            else{radius=std::exp(std::lerp(std::log(2000.f),std::log(50000.f),unit(random)));y=(unit(random)*2-1)*radius;radius=std::sqrt(std::max(0.f,radius*radius-y*y));}
            const float x=radius*std::cos(angle),z=radius*std::sin(angle);
            // Mean ecliptic J2000 -> ICRF -> renderer's Y-up convention.
            positions.insert(positions.end(),{x*au,(z*.39777716f+y*.91748206f)*au,-(z*.91748206f-y*.39777716f)*au});
        }
        auto geometry=BufferGeometry::create();geometry->setAttribute("position",FloatBufferAttribute::create(std::move(positions),3));
        auto material=ShaderMaterial::create();material->vertexShader=R"(
            uniform float opacity;varying float alpha;
            void main(){vec4 p=modelViewMatrix*vec4(position,1.0);gl_Position=projectionMatrix*p;gl_PointSize=2.0;alpha=opacity;}
        )";material->fragmentShader=R"(
            varying float alpha;uniform vec3 ink;
            void main(){float d=length(gl_PointCoord-vec2(.5));float coverage=1.0-smoothstep(.12,.5,d);if(coverage<.01)discard;gl_FragColor=vec4(ink,alpha*coverage);}
        )";material->uniforms.emplace("opacity",Uniform(0.f));material->uniforms.emplace("ink",Uniform(layer==0?Vector3(.55f,.48f,.37f):Vector3(.34f,.45f,.54f)));
        material->transparent=true;material->depthWrite=false;material->toneMapped=false;
        materials_[layer]=material;nodes_[layer]=Points::create(geometry,material);nodes_[layer]->frustumCulled=false;scene.add(nodes_[layer]);
    }
}
albert::DebrisFields::~DebrisFields(){for(auto& node:nodes_)node->geometry()->dispose();for(auto& m:materials_)m->dispose();}
void albert::DebrisFields::update(const Vector3& sun,const Vector3& camera,bool enabled,float seconds){const float distance=camera.distanceTo(sun)/au;
    constexpr float reveal[]{2.f,18.f,1800.f};
    for(unsigned i=0;i<3;++i){const float target=enabled?std::clamp((distance-reveal[i])/reveal[i],0.f,1.f)*(i==2?.20f:.36f):0.f;
        opacity_[i]=animateValue(opacity_[i],target,seconds);nodes_[i]->visible=opacity_[i]>0;nodes_[i]->position.copy(sun);materials_[i]->uniforms.at("opacity").setValue(opacity_[i]);}}
