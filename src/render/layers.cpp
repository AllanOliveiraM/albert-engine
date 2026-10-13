#include "layers.hpp"
#include "paths.hpp"
#include "animation.hpp"
using namespace threepp;
namespace {
std::shared_ptr<ShaderMaterial> style(bool points,bool surface){
    auto m=albert::borderMaterial();
    if(!surface){auto& shader=m->vertexShader;const auto start=shader.find("        vec3 surface(");const auto end=shader.find("        void main()",start);shader.replace(start,end-start,"        vec3 surface(vec3 point){return point;}\n");}
    if(points){
        auto& shader=m->vertexShader;shader="attribute vec2 markerCorner; noperspective varying vec2 vMarker;\n"+shader;shader.resize(shader.find("        void main()"));shader+=R"(
        void main(){vMarker=markerCorner;vec4 p=modelViewMatrix*vec4(surface(position),1.0);gl_Position=projectionMatrix*p;
            gl_Position.xy+=markerCorner*widthPixels/viewport*gl_Position.w;
            if(p.z>-nearClip)gl_Position=vec4(2,2,2,1);
        })";
        m->fragmentShader=R"(
        uniform vec3 lineColor;uniform float visibility;noperspective varying vec2 vMarker;
        void main(){float radius=length(vMarker);float edge=max(fwidth(radius),0.01);float coverage=1.0-smoothstep(1.0-edge,1.0,radius);if(coverage<=0.0)discard;
            gl_FragColor=vec4(lineColor,coverage*visibility);
            #include <encodings_fragment>
        })";
    }
    return m;
}
}
void albert::VectorLayers::release(Visual& v){scene_.remove(*v.mesh);v.mesh->geometry()->dispose();v.material->dispose();}
albert::VectorLayers::~VectorLayers(){for(auto& [id,v]:visuals_){(void)id;release(v);}}
void albert::VectorLayers::update(const core::Scene& registry,const SceneObjects& objects,core::Vector origin,Vector2 viewport,float nearClip,float dt,const std::function<void(ShaderMaterial&)>& earthTerrain){
    for(auto i=visuals_.begin();i!=visuals_.end();)if(registry.layers().contains(i->first)&&registry.layers().at(i->first).instance!=i->second.layer.instance){release(i->second);i=visuals_.erase(i);}else ++i;
    for(const auto& [id,l]:registry.layers())if(!visuals_.contains(id)){
        const bool points=l.kind==core::LayerKind::Points;const auto count=l.data->positions.size();auto geometry=BufferGeometry::create();
        std::vector<float> positions((points?count:count-1)*18),next(positions.size()),side(positions.size()/3),time(side.size()),nextTime(side.size()),corners(side.size()*2);
        constexpr int ends[]{0,1,0,0,1,1};constexpr float signs[]{-1,-1,1,1,-1,1};constexpr float marker[]{-1,-1,1,-1,-1,1,-1,1,1,-1,1,1};
        for(std::size_t s=0;s<side.size()/6;++s)for(int j=0;j<6;++j){side[s*6+j]=signs[j]*(ends[j]?-1.f:1.f);time[s*6+j]=float(ends[j]);nextTime[s*6+j]=float(1-ends[j]);corners[(s*6+j)*2]=marker[j*2];corners[(s*6+j)*2+1]=marker[j*2+1];}
        geometry->setAttribute("position",FloatBufferAttribute::create(std::move(positions),3));geometry->setAttribute("nextPosition",FloatBufferAttribute::create(std::move(next),3));geometry->setAttribute("lineSide",FloatBufferAttribute::create(std::move(side),1));geometry->setAttribute("orbitTime",FloatBufferAttribute::create(std::move(time),1));geometry->setAttribute("nextTime",FloatBufferAttribute::create(std::move(nextTime),1));if(points)geometry->setAttribute("markerCorner",FloatBufferAttribute::create(std::move(corners),2));
        geometry->getAttribute<float>("position")->setUsage(DrawUsage::Dynamic);geometry->getAttribute<float>("nextPosition")->setUsage(DrawUsage::Dynamic);
        auto material=style(points,l.frame==core::LayerFrame::EarthSurface);material->uniforms.at("clampSurface").setValue(l.frame==core::LayerFrame::EarthSurface?1.f:0.f);material->uniforms.at("surfaceOffset").setValue(static_cast<float>(l.clearanceMeters/core::earthRadiusMeters));material->uniforms.at("widthPixels").setValue(l.widthPixels);material->uniforms.at("lineColor").setValue(Color(l.color));
        auto mesh=Mesh::create(geometry,material);mesh->frustumCulled=false;mesh->renderOrder=2;scene_.add(mesh);visuals_.emplace(id,Visual{l,mesh,material,0});visuals_.at(id).anchorInstance=l.anchor.empty()?0:registry.get(l.anchor).instance;
    }
    for(auto i=visuals_.begin();i!=visuals_.end();){auto& v=i->second;const auto found=registry.layers().find(i->first);const bool alive=found!=registry.layers().end();bool visible=alive&&found->second.visible;
        if(alive)v.layer=found->second;
        {const auto& l=v.layer;bool physicalAxes=!l.anchor.empty()&&registry.objects().contains(l.anchor)&&registry.get(l.anchor).builtin;
            if(!l.anchor.empty()){
                if(!objects.nodes().contains(l.anchor)||!registry.objects().contains(l.anchor)||registry.get(l.anchor).instance!=v.anchorInstance){visible=false;}else{const auto& anchor=objects.nodes().at(l.anchor);v.mesh->position.copy(anchor->position);v.mesh->quaternion.copy(anchor->quaternion);visible=visible&&anchor->visible;}
            }
            const auto& coordinates=l.data->positions;auto* p=v.mesh->geometry()->getAttribute<float>("position");auto* q=v.mesh->geometry()->getAttribute<float>("nextPosition");const bool point=l.kind==core::LayerKind::Points;const std::size_t count=point?coordinates.size():coordinates.size()-1;constexpr int ends[]{0,1,0,0,1,1};
            auto local=[&](core::Vector value){if(l.frame==core::LayerFrame::GeocentricICRF)return renderPosition(value,origin);if(physicalAxes)return renderPosition(value);return Vector3(static_cast<float>(value.x/core::earthRadiusMeters),static_cast<float>(value.y/core::earthRadiusMeters),static_cast<float>(value.z/core::earthRadiusMeters));};
            if(!v.geometryReady||(l.frame==core::LayerFrame::GeocentricICRF&&(v.origin.x!=origin.x||v.origin.y!=origin.y||v.origin.z!=origin.z))){
            for(std::size_t s=0;s<count;++s){const Vector3 a=local(coordinates[s]),b=local(coordinates[point?s:s+1]);for(int j=0;j<6;++j){const auto& x=ends[j]?b:a;const auto& y=ends[j]?a:b;p->setXYZ(s*6+j,x.x,x.y,x.z);q->setXYZ(s*6+j,y.x,y.y,y.z);}}p->needsUpdate();q->needsUpdate();v.geometryReady=true;v.origin=origin;
            }
            if(l.frame==core::LayerFrame::EarthSurface){earthTerrain(*v.material);v.material->uniforms.at("surfaceOffset").setValue(static_cast<float>(l.clearanceMeters/core::earthRadiusMeters));}
        }
        v.opacity=animateValue(v.opacity,visible?1.f:0.f,dt);v.mesh->visible=v.opacity>0;
        v.material->uniforms.at("visibility").setValue(v.opacity);v.material->uniforms.at("viewport").setValue(viewport);v.material->uniforms.at("nearClip").setValue(nearClip);
        if(!alive&&v.opacity==0){release(v);i=visuals_.erase(i);}else ++i;
    }
}
