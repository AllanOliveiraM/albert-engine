#include "solids.hpp"
#include "animation.hpp"
#include "objects.hpp"
#include "ring_systems.hpp"
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
        uniform float displacement,isPatch,baseRadius,detailBlend,shapeSurface;
        uniform vec4 bounds;
        varying vec2 vUv,vGlobalUv;
        varying vec3 vNormal,vPosition;
        void main(){
            vUv=uv;vGlobalUv=shapeSurface>0.5?uv:bounds.xy+uv*bounds.zw;
            vec3 radial=normalize(position);
            vec3 p=position+radial*texture2D(heightMap,uv).r*displacement;
            if(isPatch>0.5){float coarse=baseRadius+(texture2D(baseHeightMap,vGlobalUv).r+20.0)/6378137.0;p=radial*mix(coarse,length(position),detailBlend);}
            vec4 view=modelViewMatrix*vec4(p,1.0);
            vNormal=normalize(normalMatrix*(shapeSurface>0.5?normal:radial));vPosition=view.xyz;
            gl_Position=projectionMatrix*view;
        }
    )";
    m->fragmentShader=R"(
        uniform sampler2D dayMap,baseMap,heightMap,baseHeightMap,surfaceMask;
        uniform vec3 solarDirection;
        uniform float intensity,angularRadius,masked,detailBlend,auxiliaryStrength,cloudSurface,shapeSurface,haze;
        uniform vec3 hazeAlbedo;
        uniform vec4 bounds;
        varying vec2 vUv,vGlobalUv;
        varying vec3 vNormal,vPosition;
        const float PI=3.141592653589793;
        vec3 linearColor(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}
        float terrain(vec2 local,vec2 global){return mix(texture2D(baseHeightMap,global).r,texture2D(heightMap,local).r,detailBlend)/6378137.0;}
        void main(){
            if(masked>0.5&&texture2D(surfaceMask,vGlobalUv).g<0.5)discard;
            vec2 localUv=shapeSurface>0.5?vec2(fract(vUv.x-bounds.x),vUv.y-bounds.y)/bounds.zw:vUv;
            vec4 sampleColor=texture2D(dayMap,localUv);
            vec2 edge=min(localUv,1.0-localUv);
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
            // Opaque haze (Titan in visible light): its own colour, and scattering
            // carries light past the geometric terminator.
            float hazeLight=pow(clamp((dot(normalize(vNormal),light)+0.35)/1.35,0.0,1.0),1.3);
            float direct=mix(max(irradiance,0.0)*mix(rough,1.0,cloudSurface),hazeLight,haze);
            gl_FragColor=vec4(mix(albedo,hazeAlbedo,haze)*(intensity*direct/PI+aid),1.0);
            #include <tonemapping_fragment>
            #include <encodings_fragment>
        }
    )";
    m->uniforms.emplace("dayMap",Uniform(day));m->uniforms.emplace("baseMap",Uniform(day));m->uniforms.emplace("heightMap",Uniform(height));m->uniforms.emplace("baseHeightMap",Uniform(height));m->uniforms.emplace("isPatch",Uniform(0.f));m->uniforms.emplace("baseRadius",Uniform(0.f));m->uniforms.emplace("surfaceMask",Uniform(mask));
    m->uniforms.emplace("bounds",Uniform(Vector4(0,0,1,1)));m->uniforms.emplace("solarDirection",Uniform(Vector3(1,0,0)));
    m->uniforms.emplace("cloudSurface",Uniform(0.f));m->uniforms.emplace("intensity",Uniform(2.4f));m->uniforms.emplace("angularRadius",Uniform(.00465f));
    m->uniforms.emplace("auxiliaryStrength",Uniform(0.f));m->uniforms.emplace("masked",Uniform(0.f));m->uniforms.emplace("detailBlend",Uniform(0.f));m->uniforms.emplace("displacement",Uniform(1.f/6378137.f));
    m->uniforms.emplace("shapeSurface",Uniform(0.f));m->uniforms.emplace("haze",Uniform(0.f));m->uniforms.emplace("hazeAlbedo",Uniform(Vector3()));
    return m;
}
// Translucent haze around an opaque hazy body, integrated along each view ray from
// its closest approach: main haze with a scale height, a thin detached layer above,
// lit where sunlight reaches and scattered forward (Henyey-Greenstein).
std::shared_ptr<ShaderMaterial> hazeShell(){
    auto m=ShaderMaterial::create();m->transparent=true;m->depthWrite=false;m->side=Side::Double;m->blending=Blending::Additive;
    m->uniforms={{"centre",Uniform(Vector3())},{"radius",Uniform(1.f)},{"altitude",Uniform(.1f)},{"scale",Uniform(.02f)},{"layer",Uniform(.01f)},
        {"solarDirection",Uniform(Vector3(1,0,0))},{"intensity",Uniform(2.4f)},{"visibility",Uniform(1.f)},{"inside",Uniform(0.f)},{"hazeAlbedo",Uniform(Vector3())}};
    m->vertexShader=R"(varying vec3 vPosition;void main(){vec4 view=modelViewMatrix*vec4(position,1.0);vPosition=view.xyz;gl_Position=projectionMatrix*view;})";
    m->fragmentShader=R"(
        uniform vec3 centre,solarDirection,hazeAlbedo;uniform float radius,altitude,scale,layer,intensity,visibility,inside;varying vec3 vPosition;
        const float PI=3.141592653589793,G=0.65;
        float phase(float c){float d=1.0+G*G-2.0*G*c;return (1.0-G*G)/(4.0*PI*d*sqrt(d));}
        void main(){
            if((inside<0.5)!=gl_FrontFacing)discard; // One surface per ray, inside or outside.
            vec3 d=normalize(vPosition),l=normalize(solarDirection);float t0=dot(centre,d);vec3 q=t0*d-centre;
            float b=length(q),h=b-radius;
            // Column in scale heights: grazing (Chapman) above the limb, one-sided over the disc.
            float mu=sqrt(max(0.0,1.0-(b/radius)*(b/radius)));
            float main=h>=0.0?exp(-h/scale)*sqrt(2.0*PI*(radius+h)/scale):1.0/max(mu,0.05);
            float top=exp(-pow((h-altitude)/layer,2.0))*sqrt(2.0*PI*(radius+max(h,0.0))/layer);
            float lit=smoothstep(-0.25,0.05,dot(b>0.0?q/b:-d,l));
            // Forward peak normalised to 1, plus a small isotropic part: faint at moderate
            // phase (thin bluish limb), bright only when backlit.
            float forward=0.15+0.85*phase(dot(l,d))/phase(1.0);
            vec3 colour=hazeAlbedo*(1.0-exp(-0.025*main))+vec3(0.10,0.17,0.36)*(1.0-exp(-0.02*top));
            gl_FragColor=vec4(colour*intensity*visibility*lit*forward/PI,1.0);
            #include <tonemapping_fragment>
            #include <encodings_fragment>
        })";
    return m;
}
// Faint ring systems (Jupiter, Uranus, Neptune): an optically thin slab in single
// scattering, seen from the lit or the unlit side, with dark macroscopic particles
// (Lambert-sphere phase) and forward-scattering dust (Henyey-Greenstein), the planet's
// shadow, and premultiplied composition: scattered light plus the background times the
// direct transmission exp(-tau/mu) (Uranus' epsilon ring dims what lies behind it).
// Profile texels hold band-averaged normal optical depths.
std::shared_ptr<ShaderMaterial> thinRings(Texture* profile,float inner,float outer,float radius,const albert::RingSystem& system){
    auto m=ShaderMaterial::create();m->side=Side::Double;m->transparent=true;m->depthWrite=false;
    m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;
    m->uniforms={{"profile",Uniform(profile)},{"inner",Uniform(inner)},{"outer",Uniform(outer)},{"radius",Uniform(radius)},{"localSun",Uniform(Vector3(1,0,0))},
        {"localCamera",Uniform(Vector3(0,1,0))},{"irradiance",Uniform(1.f)},{"incidence",Uniform(1.f)},{"gain",Uniform(1.f)},
        {"particleAlbedo",Uniform(system.particleAlbedo)},{"dustAlbedo",Uniform(system.dustAlbedo)},{"dustG",Uniform(system.dustAsymmetry)}};
    m->vertexShader=R"(varying vec3 point;void main(){point=position;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);})";
    m->fragmentShader=R"(
      uniform sampler2D profile;uniform float inner,outer,radius,irradiance,gain,particleAlbedo,dustAlbedo,dustG;uniform vec3 localSun,localCamera;varying vec3 point;
      const float PI=3.141592653589793;
      float lambert(float a){return (sin(a)+(PI-a)*cos(a))/PI;}
      float henyey(float c){float d=1.0+dustG*dustG-2.0*dustG*c;return (1.0-dustG*dustG)/(d*sqrt(d));}
      // I/F of a slab per unit (single-scattering albedo x phase / 4).
      float slab(float tau,float mu,float mu0,bool lit){
        if(tau<=0.0)return 0.0;
        if(lit)return mu0/(mu+mu0)*(1.0-exp(-tau*(1.0/mu+1.0/mu0)));
        return abs(mu-mu0)<1e-4?tau/mu*exp(-tau/mu):mu0/(mu-mu0)*(exp(-tau/mu)-exp(-tau/mu0));
      }
      void main(){
        float u=(length(point.xz)-inner)/(outer-inner);if(u<0.0||u>1.0)discard;
        vec2 tau=texture2D(profile,vec2(u,0.5)).rg;
        vec3 l=normalize(localSun),v=normalize(localCamera-point);
        float mu0=max(abs(l.y),1e-3),mu=max(abs(v.y),1e-3);bool lit=l.y*v.y>0.0;
        float c=clamp(dot(l,v),-1.0,1.0),alpha=acos(c);
        vec3 p=point/radius;float b=dot(p,l),q=dot(p,p)-1.0,disc=b*b-q,edge=max(fwidth(disc),1e-5);
        float shadow=1.0-step(0.0,-b)*smoothstep(-edge,edge,disc);
        float reflectance=particleAlbedo*lambert(alpha)*slab(tau.r,mu,mu0,lit)+dustAlbedo*henyey(-c)/4.0*slab(tau.g,mu,mu0,lit);
        gl_FragColor=vec4(vec3(reflectance*shadow*gain*irradiance/PI),1.0);
        #include <tonemapping_fragment>
        #include <encodings_fragment>
        // The display stretch scales scattered light only; transmission stays physical.
        gl_FragColor.a=1.0-exp(-(tau.r+tau.g)/mu);
      })";
    return m;
}
std::shared_ptr<Texture> ringProfileTexture(const albert::RingProfile& profile){
    std::vector<float> texels;texels.reserve(profile.tau.size()*2);
    for(size_t i=0;i<profile.tau.size();i+=2)texels.insert(texels.end(),{profile.tau[i],profile.tau[i+1],0.f,1.f});
    const auto width=static_cast<unsigned>(profile.tau.size()/2);auto t=DataTexture::create(ImageData(std::move(texels)),width,1);
    t->type=Type::Float;t->magFilter=Filter::Linear;t->minFilter=Filter::LinearMipmapLinear;t->generateMipmaps=true;t->colorSpace=ColorSpace::NoColorSpace;t->needsUpdate();return t;
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
    std::shared_ptr<Mesh> mesh,patch,ring,shell;std::shared_ptr<ShaderMaterial> shellMaterial;float haze=0,hazeTarget=0;
    std::shared_ptr<Texture> ringTexture;std::shared_ptr<ShaderMaterial> ringMaterial;float ringStretch=0,ringGain=1,ringGainTarget=1;
    std::shared_ptr<ShaderMaterial> material,patchMaterial;
    std::shared_ptr<Texture> day,height,mask;
    std::array<std::shared_ptr<Texture>,5> region;
    std::shared_ptr<BVH> picker;
    float blend=0,inactive=0,fill=0;
    std::chrono::steady_clock::time_point updated=std::chrono::steady_clock::now();
    void release(){
        material->uniforms.at("masked").setValue(0.f);material->uniforms.at("surfaceMask").setValue(mask.get());
        if(picker){material->uniforms.at("dayMap").setValue(day.get());material->uniforms.at("detailBlend").setValue(0.f);material->uniforms.at("bounds").setValue(Vector4(0,0,1,1));}
        if(patch){mesh->remove(*patch);patch->geometry()->dispose();patchMaterial->dispose();patch.reset();patchMaterial.reset();}
        for(auto& t:region)if(t){t->dispose();t.reset();}
    }
    ~Surface(){if(ring){ring->geometry()->dispose();ringMaterial->dispose();ringTexture->dispose();}if(shell){shell->geometry()->dispose();shellMaterial->dispose();}release();day->dispose();height->dispose();mask->dispose();material->dispose();mesh->geometry()->dispose();}
};
albert::SolidBodies::SolidBodies(Scene& scene,std::vector<SurfaceAssets> assets){
    for(auto& asset:assets){
        if(bool(asset.shape.geometry)!=bool(asset.shape.picker))throw std::invalid_argument("Incomplete measured shape");
        auto s=std::make_unique<Surface>();s->day=colorTexture(std::move(asset.day));s->height=heightTexture(std::move(asset.height),true);s->mask=colorTexture(Image(std::vector<unsigned char>(4,255),1,1));
        s->material=material(s->day.get(),s->height.get(),s->mask.get());
        s->picker=std::move(asset.shape.picker);
        s->mesh=Mesh::create(asset.shape.geometry?std::move(asset.shape.geometry):SphereGeometry::create(static_cast<float>(core::surfaceRadius(asset.body)/core::earthRadiusMeters),192,96),s->material);
        if(s->picker){s->material->uniforms.at("shapeSurface").setValue(1.f);s->material->uniforms.at("displacement").setValue(0.f);}
        const auto* definition=core::surfaceBody(asset.body);
        if(s->picker){} // DSK positions already carry physical dimensions.
        else if(definition->axesMeters[0]>0){const auto& axes=definition->axesMeters;const double r=definition->radiusMeters;s->mesh->scale.set(static_cast<float>(axes[0]/r),static_cast<float>(axes[2]/r),static_cast<float>(axes[1]/r));}
        else if(definition->polarRadiusMeters>0)s->mesh->scale.y=static_cast<float>(definition->polarRadiusMeters/definition->radiusMeters);
        if(asset.ring){
            s->ringTexture=colorTexture(std::move(*asset.ring));const float r=static_cast<float>(definition->radiusMeters/core::earthRadiusMeters);
            s->ringMaterial=rings(s->ringTexture.get(),r);auto geometry=RingGeometry::create(r*1.23f,r*2.3309f,256);
            geometry->rotateX(-std::numbers::pi_v<float>*.5f);s->ring=Mesh::create(geometry,s->ringMaterial);s->mesh->add(s->ring);
        }else if(const auto* system=ringSystem(asset.body)){
            const auto profile=ringProfile(*system,8192);const float km=static_cast<float>(1000./core::earthRadiusMeters);
            const float inner=static_cast<float>(profile.innerKm)*km,outer=static_cast<float>(profile.outerKm)*km;
            s->ringTexture=ringProfileTexture(profile);s->ringMaterial=thinRings(s->ringTexture.get(),inner,outer,static_cast<float>(definition->radiusMeters/core::earthRadiusMeters),*system);
            auto geometry=RingGeometry::create(inner,outer,512);geometry->rotateX(-std::numbers::pi_v<float>*.5f);
            s->ring=Mesh::create(geometry,s->ringMaterial);s->ring->renderOrder=2;s->mesh->add(s->ring);
            // Display stretch bringing the brightest band to I/F 0.25, only when enhanced.
            s->ringStretch=.25f/std::max(ringReferenceReflectance(*system,profile),1e-12f);
        }
        if(definition->hazeAltitudeMeters>0){
            // Visible-light haze starts on; the measured surface map is an opt-in view.
            const auto& a=definition->hazeAlbedo;s->haze=s->hazeTarget=1.f;s->shellMaterial=hazeShell();
            const double r=definition->radiusMeters;s->shellMaterial->uniforms.at("hazeAlbedo").setValue(Vector3(a[0],a[1],a[2]));
            s->shell=Mesh::create(SphereGeometry::create(static_cast<float>((r+definition->hazeAltitudeMeters*1.25)/r*core::surfaceRadius(asset.body)/core::earthRadiusMeters),96,48),s->shellMaterial);
            s->shell->renderOrder=2;s->mesh->add(s->shell);
            s->material->uniforms.at("hazeAlbedo").setValue(Vector3(a[0],a[1],a[2]));
        }
        nodes_[asset.body]=s->mesh;scene.add(s->mesh);surfaces_[asset.body]=std::move(s);
    }
}
albert::SolidBodies::~SolidBodies()=default;
bool albert::SolidBodies::measuredShape(const std::string& id) const{return surfaces_.contains(id)&&bool(surfaces_.at(id)->picker);}
bool albert::SolidBodies::faintRings(const std::string& id) const{return surfaces_.contains(id)&&surfaces_.at(id)->ringStretch>0;}
bool albert::SolidBodies::ringsEnhanced(const std::string& id) const{return faintRings(id)&&surfaces_.at(id)->ringGainTarget>1;}
void albert::SolidBodies::enhanceRings(const std::string& id,bool enhanced){if(faintRings(id))surfaces_.at(id)->ringGainTarget=enhanced?surfaces_.at(id)->ringStretch:1.f;}
bool albert::SolidBodies::hazy(const std::string& id) const{return surfaces_.contains(id)&&bool(surfaces_.at(id)->shell);}
bool albert::SolidBodies::surfaceView(const std::string& id) const{return hazy(id)&&surfaces_.at(id)->hazeTarget<.5f;}
void albert::SolidBodies::showSurface(const std::string& id,bool surface){if(hazy(id))surfaces_.at(id)->hazeTarget=surface?0.f:1.f;}
std::shared_ptr<const BVH> albert::SolidBodies::picker(const std::string& id) const{return surfaces_.at(id)->picker;}
void albert::SolidBodies::stage(RegionalImages images){
    if(uploads_.active()||!surfaces_.contains(images.body)||!images.day)return;
    const bool shape=measuredShape(images.body);
    if(!shape&&(!images.geometry||!images.surfaceMask||!images.height))throw std::invalid_argument("Solid terrain must be prepared on the worker");
    images.gpu[0]=colorTexture(std::move(*images.day));if(!shape){images.gpu[3]=heightTexture(std::move(*images.height));images.gpu[4]=colorTexture(std::move(*images.surfaceMask));}
    for(const auto& t:images.gpu)if(t)uploads_.add(t);
    pending_=std::move(images);
}
void albert::SolidBodies::update(GLRenderer& renderer,PerspectiveCamera& camera,const Vector3& sun,double jd,const std::string& focus,bool details,float dt,const AuxiliaryLights& auxiliary){
    uploads_.tick(renderer);
    if(pending_&&!uploads_.active()){
        auto images=std::move(*pending_);pending_.reset();auto& s=*surfaces_.at(images.body);s.release();s.inactive=0;s.region=std::move(images.gpu);
        if(s.picker){const auto& r=images.region;s.material->uniforms.at("dayMap").setValue(s.region[0].get());s.material->uniforms.at("bounds").setValue(Vector4(static_cast<float>((r.west+180.)/360.),static_cast<float>((r.south+90.)/180.),static_cast<float>((r.east-r.west)/360.),static_cast<float>((r.north-r.south)/180.)));s.blend=0;}
        else {
        s.patchMaterial=material(s.region[0].get(),s.region[3].get(),s.mask.get());s.patchMaterial->uniforms.at("baseMap").setValue(s.day.get());s.patchMaterial->uniforms.at("baseHeightMap").setValue(s.height.get());s.patchMaterial->uniforms.at("isPatch").setValue(1.f);s.patchMaterial->uniforms.at("baseRadius").setValue(static_cast<float>(core::surfaceRadius(images.body)/core::earthRadiusMeters));
        const auto& r=images.region;s.patchMaterial->uniforms.at("bounds").setValue(Vector4(static_cast<float>((r.west+180.)/360.),static_cast<float>((r.south+90.)/180.),static_cast<float>((r.east-r.west)/360.),static_cast<float>((r.north-r.south)/180.)));
        s.patchMaterial->uniforms.at("displacement").setValue(0.f);s.patch=Mesh::create(std::move(images.geometry),s.patchMaterial);s.mesh->add(s.patch);s.blend=0;
        s.material->uniforms.at("surfaceMask").setValue(s.region[4].get());s.material->uniforms.at("masked").setValue(1.f);
        }
    }
    camera.updateMatrixWorld();
    for(auto& [id,entry]:surfaces_){auto& s=*entry;const auto frame=core::bodyFrame(*core::surfaceBody(id),jd);
        auto convert=[](core::Vector v){return Vector3(static_cast<float>(v.x),static_cast<float>(v.z),static_cast<float>(-v.y));};
        Matrix4 orientation;orientation.makeBasis(convert(frame.prime),convert(frame.pole),convert(frame.east*(-1.)));s.mesh->setRotationFromMatrix(orientation);
        Vector3 light=sun;light.sub(s.mesh->position);const double distance=light.length()*core::earthRadiusMeters;
        if(s.ringMaterial){
            s.mesh->updateMatrixWorld();Matrix4 inverse=*s.mesh->matrixWorld;inverse.invert();Vector3 local=light;local.transformDirection(inverse);
            const Vector3 pole=convert(frame.pole);const float incidence=std::abs(light.clone().normalize().dot(pole));
            s.ringMaterial->uniforms.at("localSun").setValue(local);s.ringMaterial->uniforms.at("incidence").setValue(incidence);
            s.ringMaterial->uniforms.at("irradiance").setValue(static_cast<float>(2.4*std::pow(149597870700./distance,2)));
            if(s.ringMaterial->uniforms.contains("localCamera")){Vector3 eye=camera.position;eye.applyMatrix4(inverse);s.ringMaterial->uniforms.at("localCamera").setValue(eye);
                // Fade in log space: the stretch spans up to ~1e5.
                s.ringGain=std::exp(animateValue(std::log(s.ringGain),std::log(s.ringGainTarget),dt));s.ringMaterial->uniforms.at("gain").setValue(s.ringGain);}
        }
        light.transformDirection(camera.matrixWorldInverse);
        const float intensity=static_cast<float>(2.4*std::pow(149597870700./distance,2)),angular=static_cast<float>(std::asin(std::min(1.,695700000./distance)));
        const auto aid=auxiliary.find(id);s.fill=animateValue(s.fill,aid!=auxiliary.end()&&aid->second.enabled?std::clamp(aid->second.intensity,0.f,1.5f):0.f,dt);
        if(s.shell){
            s.haze=animateValue(s.haze,s.hazeTarget,dt);s.shell->visible=s.haze>.001f;
            const auto* d=core::surfaceBody(id);const float r=static_cast<float>(core::surfaceRadius(id)/core::earthRadiusMeters),k=static_cast<float>(core::surfaceRadius(id)/d->radiusMeters/core::earthRadiusMeters);
            s.mesh->updateMatrixWorld();Vector3 centre;centre.setFromMatrixPosition(*s.mesh->matrixWorld);centre.applyMatrix4(camera.matrixWorldInverse);
            auto& u=s.shellMaterial->uniforms;u.at("centre").setValue(centre);u.at("radius").setValue(r);u.at("altitude").setValue(static_cast<float>(d->hazeAltitudeMeters)*k);
            u.at("scale").setValue(60000.f*k);u.at("layer").setValue(25000.f*k);u.at("visibility").setValue(s.haze);
            u.at("inside").setValue(centre.length()<r+static_cast<float>(d->hazeAltitudeMeters*1.25)*k?1.f:0.f);
        }
        for(auto* m:{s.material.get(),s.patchMaterial.get()})if(m){m->uniforms.at("haze").setValue(s.haze);m->uniforms.at("hazeAlbedo").setValue(s.material->uniforms.at("hazeAlbedo").value<Vector3>());m->uniforms.at("cloudSurface").setValue(core::surfaceBody(id)->cloudSurface?1.f:0.f);m->uniforms.at("auxiliaryStrength").setValue(s.fill);m->uniforms.at("solarDirection").setValue(light);m->uniforms.at("intensity").setValue(intensity);m->uniforms.at("angularRadius").setValue(angular);}
        if(s.shell){auto& u=s.shellMaterial->uniforms;u.at("solarDirection").setValue(light);u.at("intensity").setValue(intensity);}
        s.blend=animateValue(s.blend,1.f,dt);if(s.patchMaterial)s.patchMaterial->uniforms.at("detailBlend").setValue(s.blend);else if(s.picker&&s.region[0])s.material->uniforms.at("detailBlend").setValue(s.blend);
        const bool nearby=camera.position.distanceTo(s.mesh->position)<static_cast<float>(core::surfaceRadius(id)/core::earthRadiusMeters)*2.4f;
        const auto now=std::chrono::steady_clock::now();
        const float residencySeconds=std::max(dt,std::chrono::duration<float>(now-s.updated).count());s.updated=now;
        s.inactive=(focus==id&&details&&nearby)?0.f:s.inactive+residencySeconds;if(s.inactive>3.f&&(s.patch||(s.picker&&s.region[0]))){s.release();evicted_=true;}
    }
}
