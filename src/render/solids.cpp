#include "solids.hpp"
#include "animation.hpp"
#include "objects.hpp"
#include "threepp/textures/DataTexture.hpp"
#include <numbers>
#include <chrono>
#include <stdexcept>
using namespace threepp;
namespace {
std::shared_ptr<Texture> colorTexture(Image image){std::vector<Image> images;images.emplace_back(std::move(image));auto t=Texture::create(std::move(images));t->colorSpace=ColorSpace::NoColorSpace;t->wrapS=TextureWrapping::Repeat;t->anisotropy=8;t->needsUpdate();return t;}
std::shared_ptr<Texture> heightTexture(Image image,bool global=false){auto t=DataTexture::create(ImageData(std::move(image.data<float>())),image.width(),image.height());t->type=Type::Float;t->minFilter=t->magFilter=Filter::Linear;t->colorSpace=ColorSpace::NoColorSpace;if(global)t->wrapS=TextureWrapping::Repeat;return t;}
std::shared_ptr<ShaderMaterial> material(Texture* day,Texture* height,Texture* mask){
    auto m=ShaderMaterial::create();
    m->vertexShader=R"(
        uniform sampler2D heightMap,baseHeightMap;
        uniform float displacement,isPatch,baseRadius,detailBlend;
        uniform vec4 bounds;
        varying vec2 vUv,vGlobalUv;
        varying vec3 vNormal,vPosition;
        void main(){
            vUv=uv;vGlobalUv=bounds.xy+uv*bounds.zw;
            vec3 radial=normalize(position);
            vec3 p=position+radial*texture2D(heightMap,uv).r*displacement;
            if(isPatch>0.5){float coarse=baseRadius+(texture2D(baseHeightMap,vGlobalUv).r+20.0)/6378137.0;p=radial*mix(coarse,length(position),detailBlend);}
            vec4 view=modelViewMatrix*vec4(p,1.0);
            vNormal=normalize(normalMatrix*radial);vPosition=view.xyz;
            gl_Position=projectionMatrix*view;
        }
    )";
    m->fragmentShader=R"(
        uniform sampler2D dayMap,baseMap,heightMap,baseHeightMap,surfaceMask;
        uniform vec3 solarDirection;
        uniform float intensity,angularRadius,masked,detailBlend,auxiliaryStrength,cloudSurface;
        varying vec2 vUv,vGlobalUv;
        varying vec3 vNormal,vPosition;
        const float PI=3.141592653589793;
        vec3 linearColor(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}
        float terrain(vec2 local,vec2 global){return mix(texture2D(baseHeightMap,global).r,texture2D(heightMap,local).r,detailBlend)/6378137.0;}
        void main(){
            if(masked>0.5&&texture2D(surfaceMask,vGlobalUv).g<0.5)discard;
            vec4 sampleColor=texture2D(dayMap,vUv);
            vec2 edge=min(vUv,1.0-vUv);
            float blend=detailBlend*smoothstep(0.0,0.025,min(edge.x,edge.y));
            vec3 albedo=linearColor(mix(texture2D(baseMap,vGlobalUv).rgb,sampleColor.rgb,blend));
            vec3 normal=normalize(vNormal),sigmaX=dFdx(vPosition),sigmaY=dFdy(vPosition);
            float height=terrain(vUv,vGlobalUv);
            vec2 gradient=vec2(terrain(vUv+dFdx(vUv),vGlobalUv+dFdx(vGlobalUv))-height,
                terrain(vUv+dFdy(vUv),vGlobalUv+dFdy(vGlobalUv))-height);
            vec3 r1=cross(sigmaY,normal),r2=cross(normal,sigmaX);float determinant=dot(sigmaX,r1);
            normal=normalize(abs(determinant)*normal-sign(determinant)*(gradient.x*r1+gradient.y*r2));
            vec3 light=normalize(solarDirection),view=normalize(-vPosition);
            float mu=dot(normal,light),nl=max(mu,0.0),nv=max(dot(normal,view),0.0);
            // Finite solar disk at the geometric terminator (uniform disk).
            float a=max(angularRadius,0.00001),x=clamp(mu/a,-1.0,1.0),s=sqrt(max(0.0,1.0-x*x));
            float irradiance=mu>=a?mu:mu<=-a?0.0:mu*(0.5+(asin(x)+x*s)/PI)+2.0*a*s*s*s/(3.0*PI);
            // Oren-Nayar diffuse; optional didactic fill is separate from solar irradiance.
            vec3 lp=light-normal*mu,vp=view-normal*nv;
            float gamma=max(0.0,dot(lp,vp)/max(length(lp)*length(vp),0.00001));
            float lo=min(nl,nv),hi=max(nl,nv);
            float rough=0.70122+0.38017*gamma*sqrt(max(0.0,1.0-lo*lo))*sqrt(max(0.0,1.0-hi*hi))/max(hi,0.02);
            float aid=auxiliaryStrength*(intensity/2.4)*(1.0-smoothstep(-0.10,0.0,dot(normalize(vNormal),light)));
            gl_FragColor=vec4(albedo*(intensity*max(irradiance,0.0)*mix(rough,1.0,cloudSurface)/PI+aid),1.0);
            #include <tonemapping_fragment>
            #include <encodings_fragment>
        }
    )";
    m->uniforms.emplace("dayMap",Uniform(day));m->uniforms.emplace("baseMap",Uniform(day));m->uniforms.emplace("heightMap",Uniform(height));m->uniforms.emplace("baseHeightMap",Uniform(height));m->uniforms.emplace("isPatch",Uniform(0.f));m->uniforms.emplace("baseRadius",Uniform(0.f));m->uniforms.emplace("surfaceMask",Uniform(mask));
    m->uniforms.emplace("bounds",Uniform(Vector4(0,0,1,1)));m->uniforms.emplace("solarDirection",Uniform(Vector3(1,0,0)));
    m->uniforms.emplace("cloudSurface",Uniform(0.f));m->uniforms.emplace("intensity",Uniform(2.4f));m->uniforms.emplace("angularRadius",Uniform(.00465f));
    m->uniforms.emplace("auxiliaryStrength",Uniform(0.f));m->uniforms.emplace("masked",Uniform(0.f));m->uniforms.emplace("detailBlend",Uniform(0.f));m->uniforms.emplace("displacement",Uniform(1.f/6378137.f));
    return m;
}
std::shared_ptr<ShaderMaterial> rings(Texture* profile,float radius){
    auto m=ShaderMaterial::create();m->side=Side::Double;m->transparent=true;m->depthWrite=false;
    m->uniforms={{"profile",Uniform(profile)},{"radius",Uniform(radius)},{"localSun",Uniform(Vector3(1,0,0))},{"irradiance",Uniform(1.f)},{"incidence",Uniform(1.f)}};
    m->vertexShader=R"(varying vec3 point;void main(){point=position;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);})";
    m->fragmentShader=R"(
      uniform sampler2D profile;uniform float radius,irradiance,incidence;uniform vec3 localSun;varying vec3 point;
      void main(){vec3 p=point/radius;float u=(length(p.xz)-1.23)/(2.3309-1.23);vec4 map=texture2D(profile,vec2(u,.5));
      vec3 l=normalize(localSun);float b=dot(p,l),c=dot(p,p)-1.0;float disc=b*b-c;
      float edge=max(fwidth(disc),.00001);float shadow=1.0-step(0.0,-b)*smoothstep(-edge,edge,disc);
      vec3 albedo=mix(map.rgb/12.92,pow((map.rgb+.055)/1.055,vec3(2.4)),step(vec3(.04045),map.rgb));
      gl_FragColor=vec4(albedo*irradiance*(.08+.92*sqrt(incidence))*shadow/3.14159265,map.a);
      #include <tonemapping_fragment>
      #include <encodings_fragment>
      })";return m;
}

}
struct albert::SolidBodies::Surface {
    std::shared_ptr<Mesh> mesh,patch,ring;
    std::shared_ptr<Texture> ringTexture;std::shared_ptr<ShaderMaterial> ringMaterial;
    std::shared_ptr<ShaderMaterial> material,patchMaterial;
    std::shared_ptr<Texture> day,height,mask;
    std::array<std::shared_ptr<Texture>,5> region;
    float blend=0,inactive=0,fill=0;
    std::chrono::steady_clock::time_point updated=std::chrono::steady_clock::now();
    void release(){
        material->uniforms.at("masked").setValue(0.f);material->uniforms.at("surfaceMask").setValue(mask.get());
        if(patch){mesh->remove(*patch);patch->geometry()->dispose();patchMaterial->dispose();patch.reset();patchMaterial.reset();}
        for(auto& t:region)if(t){t->dispose();t.reset();}
    }
    ~Surface(){if(ring){ring->geometry()->dispose();ringMaterial->dispose();ringTexture->dispose();}release();day->dispose();height->dispose();mask->dispose();material->dispose();mesh->geometry()->dispose();}
};
albert::SolidBodies::SolidBodies(Scene& scene,std::vector<SurfaceAssets> assets){
    for(auto& asset:assets){
        auto s=std::make_unique<Surface>();s->day=colorTexture(std::move(asset.day));s->height=heightTexture(std::move(asset.height),true);s->mask=colorTexture(Image(std::vector<unsigned char>(4,255),1,1));
        s->material=material(s->day.get(),s->height.get(),s->mask.get());
        s->mesh=Mesh::create(SphereGeometry::create(static_cast<float>(core::surfaceRadius(asset.body)/core::earthRadiusMeters),192,96),s->material);
        const auto* definition=core::naturalBody(asset.body);
        if(definition->polarRadiusMeters>0)s->mesh->scale.y=static_cast<float>(definition->polarRadiusMeters/definition->radiusMeters);
        if(asset.ring){
            s->ringTexture=colorTexture(std::move(*asset.ring));const float r=static_cast<float>(definition->radiusMeters/core::earthRadiusMeters);
            s->ringMaterial=rings(s->ringTexture.get(),r);auto geometry=RingGeometry::create(r*1.23f,r*2.3309f,256);
            geometry->rotateX(-std::numbers::pi_v<float>*.5f);s->ring=Mesh::create(geometry,s->ringMaterial);s->mesh->add(s->ring);
        }
        nodes_[asset.body]=s->mesh;scene.add(s->mesh);surfaces_[asset.body]=std::move(s);
    }
}
albert::SolidBodies::~SolidBodies()=default;
void albert::SolidBodies::stage(RegionalImages images){
    if(uploads_.active()||!surfaces_.contains(images.body)||!images.day||!images.height)return;
    if(!images.geometry||!images.surfaceMask)throw std::invalid_argument("Solid terrain must be prepared on the worker");
    images.gpu[0]=colorTexture(std::move(*images.day));images.gpu[3]=heightTexture(std::move(*images.height));images.gpu[4]=colorTexture(std::move(*images.surfaceMask));
    for(const auto& t:images.gpu)if(t)uploads_.add(t);
    pending_=std::move(images);
}
void albert::SolidBodies::update(GLRenderer& renderer,PerspectiveCamera& camera,const Vector3& sun,double jd,const std::string& focus,bool details,float dt,const AuxiliaryLights& auxiliary){
    uploads_.tick(renderer);
    if(pending_&&!uploads_.active()){
        auto images=std::move(*pending_);pending_.reset();auto& s=*surfaces_.at(images.body);s.release();s.inactive=0;s.region=std::move(images.gpu);
        s.patchMaterial=material(s.region[0].get(),s.region[3].get(),s.mask.get());s.patchMaterial->uniforms.at("baseMap").setValue(s.day.get());s.patchMaterial->uniforms.at("baseHeightMap").setValue(s.height.get());s.patchMaterial->uniforms.at("isPatch").setValue(1.f);s.patchMaterial->uniforms.at("baseRadius").setValue(static_cast<float>(core::surfaceRadius(images.body)/core::earthRadiusMeters));
        const auto& r=images.region;s.patchMaterial->uniforms.at("bounds").setValue(Vector4(static_cast<float>((r.west+180.)/360.),static_cast<float>((r.south+90.)/180.),static_cast<float>((r.east-r.west)/360.),static_cast<float>((r.north-r.south)/180.)));
        s.patchMaterial->uniforms.at("displacement").setValue(0.f);s.patch=Mesh::create(std::move(images.geometry),s.patchMaterial);s.mesh->add(s.patch);s.blend=0;
        s.material->uniforms.at("surfaceMask").setValue(s.region[4].get());s.material->uniforms.at("masked").setValue(1.f);
    }
    camera.updateMatrixWorld();
    for(auto& [id,entry]:surfaces_){auto& s=*entry;const auto frame=core::bodyFrame(*core::naturalBody(id),jd);
        auto convert=[](core::Vector v){return Vector3(static_cast<float>(v.x),static_cast<float>(v.z),static_cast<float>(-v.y));};
        Matrix4 orientation;orientation.makeBasis(convert(frame.prime),convert(frame.pole),convert(frame.east*(-1.)));s.mesh->setRotationFromMatrix(orientation);
        Vector3 light=sun;light.sub(s.mesh->position);const double distance=light.length()*core::earthRadiusMeters;
        if(s.ringMaterial){
            s.mesh->updateMatrixWorld();Matrix4 inverse=*s.mesh->matrixWorld;inverse.invert();Vector3 local=light;local.transformDirection(inverse);
            const Vector3 pole=convert(frame.pole);const float incidence=std::abs(light.clone().normalize().dot(pole));
            s.ringMaterial->uniforms.at("localSun").setValue(local);s.ringMaterial->uniforms.at("incidence").setValue(incidence);
            s.ringMaterial->uniforms.at("irradiance").setValue(static_cast<float>(2.4*std::pow(149597870700./distance,2)));
        }
        light.transformDirection(camera.matrixWorldInverse);
        const float intensity=static_cast<float>(2.4*std::pow(149597870700./distance,2)),angular=static_cast<float>(std::asin(std::min(1.,695700000./distance)));
        const auto aid=auxiliary.find(id);s.fill=animateValue(s.fill,aid!=auxiliary.end()&&aid->second.enabled?std::clamp(aid->second.intensity,0.f,1.5f):0.f,dt);
        for(auto* m:{s.material.get(),s.patchMaterial.get()})if(m){m->uniforms.at("cloudSurface").setValue(core::naturalBody(id)->cloudSurface?1.f:0.f);m->uniforms.at("auxiliaryStrength").setValue(s.fill);m->uniforms.at("solarDirection").setValue(light);m->uniforms.at("intensity").setValue(intensity);m->uniforms.at("angularRadius").setValue(angular);}
        s.blend=animateValue(s.blend,1.f,dt);if(s.patchMaterial)s.patchMaterial->uniforms.at("detailBlend").setValue(s.blend);
        const bool nearby=camera.position.distanceTo(s.mesh->position)<static_cast<float>(core::surfaceRadius(id)/core::earthRadiusMeters)*2.4f;
        const auto now=std::chrono::steady_clock::now();
        const float residencySeconds=std::max(dt,std::chrono::duration<float>(now-s.updated).count());s.updated=now;
        s.inactive=(focus==id&&details&&nearby)?0.f:s.inactive+residencySeconds;if(s.inactive>3.f&&s.patch){s.release();evicted_=true;}
    }
}
