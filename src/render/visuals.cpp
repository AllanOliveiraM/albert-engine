#include "visuals.hpp"
#include "eclipses.hpp"
#include "platform.hpp"
#include "asset_pack.hpp"
#include "animation.hpp"

#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/objects/Points.hpp"
#include "threepp/textures/DataTexture.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <stdexcept>

using namespace threepp;

namespace {
constexpr double pi = std::numbers::pi;

Image limitResolution(Image image, unsigned maximum) {
    // The bundled maps have power-of-two dimensions. Box-filter once on the
    // worker, then let the GPU's mipmaps handle minification during rendering.
    const unsigned factor = std::max(1u, image.width() / maximum);
    if (factor == 1) return image;
    const unsigned width = image.width() / factor, height = image.height() / factor;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    const auto& source = image.data();
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            for (unsigned c = 0; c < 4; ++c) {
                unsigned sum = 0;
                for (unsigned dy = 0; dy < factor; ++dy) {
                    for (unsigned dx = 0; dx < factor; ++dx) {
                        sum += source[4 * (static_cast<size_t>(y * factor + dy) * image.width() + x * factor + dx) + c];
                    }
                }
                pixels[4 * (static_cast<size_t>(y) * width + x) + c] = static_cast<unsigned char>(sum / (factor * factor));
            }
        }
    }
    return Image(std::move(pixels), width, height);
}

std::shared_ptr<Texture> texture(Image image, ColorSpace colorSpace = ColorSpace::sRGB) {
    std::vector<Image> images;
    images.push_back(std::move(image));
    auto result = Texture::create(std::move(images));
    result->colorSpace = colorSpace;
    result->anisotropy = 8; // The renderer clamps to the supported hardware limit.
    result->wrapS = TextureWrapping::Repeat;
    result->needsUpdate();
    return result;
}

std::shared_ptr<Texture> elevationTexture(Image image) {
    auto result=DataTexture::create(ImageData(std::move(image.data<float>())),image.width(),image.height());
    result->type=Type::Float;
    result->minFilter=result->magFilter=Filter::Linear;
    result->colorSpace=ColorSpace::NoColorSpace;
    return result;
}

// Visual fill follows solar incidence in body coordinates. This tiny mask
// changes slowly, while albedo and terrain retain their original resolution.
void updateFillMask(Texture& texture, Vector3& previous, const Vector3& localSun, bool ellipsoid=false) {
    if(localSun.distanceToSquared(previous)<=1e-8f) return;
    auto& pixels=texture.image().data();
    for(unsigned y=0;y<64;++y) for(unsigned x=0;x<128;++x) {
        const double sourceLatitude=((y+.5)/64.-.5)*pi, phi=(x+.5)/128.*2.*pi;
        const double latitude=ellipsoid ? albert::parametricLatitude(sourceLatitude) : sourceLatitude;
        const Vector3 radial(static_cast<float>(-std::cos(phi)*std::cos(latitude)),
            static_cast<float>(std::sin(latitude)),static_cast<float>(std::sin(phi)*std::cos(latitude)));
        const float t=std::clamp((radial.dot(localSun)+.08f)/.08f,0.f,1.f);
        const auto weight=static_cast<unsigned char>(255.f*(1.f-t*t*(3.f-2.f*t)));
        const size_t at=4*(y*128+x);
        pixels[at]=pixels[at+1]=pixels[at+2]=weight;pixels[at+3]=255;
    }
    texture.needsUpdate();
    previous=localSun;
}

void geodeticEarthUV(Mesh& body) {
    auto* uv=body.geometry()->getAttribute<float>("uv");
    for(int i=0;i<uv->count();++i) {
        const double parametric=(uv->getY(i)-.5)*pi;
        uv->setXY(i,uv->getX(i),static_cast<float>(albert::geodeticLatitude(parametric)/pi+.5));
    }
    uv->needsUpdate();
}

// Optical depth of the exponential atmosphere. A small table replaces the
// nested solar ray march in every fragment; generated once, no network/asset.
std::shared_ptr<Texture> opticalDepthTexture() {
    constexpr unsigned width = 256, height = 128, steps = 128;
    constexpr double radius = 6378137., top = radius + 80000.;
    std::vector<float> pixels(width * height * 4, 0.f);
    for (unsigned y = 0; y < height; ++y) {
        const double altitude = std::pow(static_cast<double>(y) / (height-1), 2) * 80000.;
        const double r = radius + altitude;
        for (unsigned x = 0; x < width; ++x) {
            const double mu = 2. * x / (width-1) - 1.;
            double end = -r*mu + std::sqrt(r*r*mu*mu + top*top - r*r);
            const double ground = r*r*mu*mu + radius*radius - r*r;
            if (mu < 0 && ground >= 0) end = std::min(end, -r*mu - std::sqrt(ground));
            double rayleigh = 0, mie = 0;
            const double step = end / steps;
            for (unsigned i = 0; i < steps; ++i) {
                const double t = (i+.5) * step;
                const double h = std::max(0., std::sqrt(r*r+t*t+2.*r*t*mu) - radius);
                rayleigh += std::exp(-h/8000.) * step;
                mie += std::exp(-h/1200.) * step;
            }
            const auto index = 4 * (y * width + x);
            pixels[index] = static_cast<float>(rayleigh);
            pixels[index+1] = static_cast<float>(mie);
        }
    }
    auto result = DataTexture::create(ImageData(std::move(pixels)), width, height);
    result->type = Type::Float;
    result->minFilter = result->magFilter = Filter::Linear;
    result->colorSpace = ColorSpace::NoColorSpace;
    return result;
}

constexpr auto surfaceVertex = R"(
    varying vec2 vUv;
    varying vec3 vLocalPosition;
    varying vec3 vLocalNormal;
    varying vec3 vWorldNormal;
    varying vec3 vWorldPosition;
    void main() {
        vUv = uv;
        vLocalPosition = position;
        vLocalNormal = normal;
        vWorldPosition = (modelMatrix * vec4(position, 1.0)).xyz;
        vWorldNormal = normalize(mat3(modelMatrix) * normal);
        gl_Position = projectionMatrix * viewMatrix * vec4(vWorldPosition, 1.0);
    }
)";

// A regional mask replaces, rather than stacks on, the global clouds.
// Unknown pixels and the tile boundary retain the last valid global image.
constexpr auto cloudSampling = R"(
    uniform sampler2D cloudMap, detailMap, previousDetailMap;
    uniform vec4 detailBounds, previousDetailBounds;
    uniform float detailEnabled, previousDetailEnabled, detailBlend;
    float cloudCrop(sampler2D image,vec4 bounds,float enabled,vec2 uv,float global) {
        vec2 local = vec2(fract(uv.x-bounds.x), uv.y-bounds.y) / bounds.zw;
        if (enabled < 0.5 || any(lessThan(local,vec2(0.0))) || any(greaterThan(local,vec2(1.0)))) return global;
        vec4 detail = texture2D(image,local);
        vec2 edge = min(local,1.0-local);
        float weight = smoothstep(0.0,0.04,min(edge.x,edge.y))*detail.a;
        return mix(global,detail.g,weight);
    }
    float cloudDensity(vec2 uv) {
        float global=texture2D(cloudMap,uv).g;
        return mix(cloudCrop(previousDetailMap,previousDetailBounds,previousDetailEnabled,uv,global),
            cloudCrop(detailMap,detailBounds,detailEnabled,uv,global),detailBlend);
    }
)";

std::shared_ptr<ShaderMaterial> overlay(const char* fragment) {
    auto material = ShaderMaterial::create();
    material->vertexShader = surfaceVertex;
    material->fragmentShader = fragment;
    material->uniforms.emplace("solarDirection", Uniform(Vector3(1, 0, 0)));
    material->transparent = true;
    material->depthWrite = false;
    material->blending = Blending::Additive;
    return material;
}
} // namespace

albert::DecodedAssets albert::loadVisualAssets(const std::filesystem::path& directory, DownloadProgress& progress) {
    (void)directory;
    constexpr const char* layers[] = {"earth-day", "earth-night", "", "moon-day", "sun", "earth-height"};
    constexpr const char* labels[] = {"Superficie da Terra", "Luzes noturnas", "Nuvens de referencia", "Superficie lunar", "Fotosfera solar", "Relevo terrestre"};
    DecodedAssets result;
    const auto pack=visualAssetPack();
    for (size_t i = 0; i < result.images.size(); ++i) {
        progress.checkCancelled();
        progress.stage(labels[i], static_cast<int>(i));
        if (i==2) result.images[i]=Image(std::vector<unsigned char>(4,0),1,1);
        else result.images[i]=pack->image(layers[i],i==4 ? GeoRegion{0,0,1,1} : GeoRegion{},i==4 ? 2048 : i==5 ? 1024 : 4096,progress);
    }
    result.moonHeight=pack->image("moon-height",GeoRegion{},1024,progress);
    for(const auto& body:core::surfaceBodies()){
        const std::string id(body.id);
        if(pack->contains(id+"-day")){
            SurfaceAssets assets{id,pack->image(id+"-day",{},core::naturalBody(id)?2048:1024,progress),pack->contains(id+"-height")?pack->image(id+"-height",{},512,progress):Image(std::vector<float>{0,0,0,1},1,1),pack->contains(id+"-ring")?std::optional<Image>(pack->image(id+"-ring",{0,0,1,1},4096,progress)):std::nullopt,{}};
            const std::string shape="v1/"+id+"/shape.bin";if(pack->hasPayload(shape)){progress.checkCancelled();assets.shape=decodeShape(pack->payload(shape));progress.checkCancelled();}
            result.surfaces.push_back(std::move(assets));
        }
    }
    // A visual ocean mask inferred from the day map, not elevation or terrain.
    const auto& day = *result.images[0];
    auto pixels = day.data();
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const float water = std::clamp((pixels[i + 2] - pixels[i] * 1.15f) / 12.f, 0.f, 1.f);
        // Dielectric ocean roughness, land stays diffuse. This mask is visual.
        const auto value = static_cast<unsigned char>(220.f - water * 170.f);
        pixels[i] = pixels[i + 1] = pixels[i + 2] = value;
    }
    result.oceans = limitResolution(Image(std::move(pixels), day.width(), day.height()), 2048);
    return result;
}

albert::PlanetVisuals::PlanetVisuals(Scene& scene, DecodedAssets assets) {
    for(auto& fill:surfaceFillTextures_) {
        fill=texture(Image(std::vector<unsigned char>(128*64*4,0),128,64),ColorSpace::NoColorSpace);
        fill->channel=1;
        fill->minFilter=fill->magFilter=Filter::Linear;
        fill->generateMipmaps=false;
    }
    opaqueMask_=texture(Image(std::vector<unsigned char>(4,255),1,1),ColorSpace::NoColorSpace);
    earthMaterial_ = MeshStandardMaterial::create();
    earthMaterial_->metalness = 0.f;
    earthMaterial_->roughness = 1.f;
    earthMaterial_->alphaMap=opaqueMask_;earthMaterial_->alphaTest=.5f;
    // GEBCO source encodes elevations from 0 to 6400 m. No exaggerated relief.
    earthMaterial_->bumpScale = 1.f / 6378137.f;
    earth = Mesh::create(SphereGeometry::create(1, 256, 128), earthMaterial_);
    geodeticEarthUV(*earth);
    earth->geometry()->setAttribute("uv2",FloatBufferAttribute::create(earth->geometry()->getAttribute<float>("uv")->array(),2));
    earthMaterial_->lightMap=surfaceFillTextures_[0];
    earth->scale.y = static_cast<float>(6356.752314245 / 6378.137);
    earth->castShadow = earth->receiveShadow = true;
    scene.add(earth);

    moonMaterial_ = MeshPhongMaterial::create();
    moonMaterial_->specular = Color(0x050505);
    moonMaterial_->shininess = 3;
    moonMaterial_->alphaMap=opaqueMask_;moonMaterial_->alphaTest=.5f;
    moon = Mesh::create(SphereGeometry::create(static_cast<float>(1737.4 / 6378.137), 128, 64), moonMaterial_);
    moon->geometry()->setAttribute("uv2",FloatBufferAttribute::create(moon->geometry()->getAttribute<float>("uv")->array(),2));
    moonMaterial_->lightMap=surfaceFillTextures_[1];
    moon->castShadow = moon->receiveShadow = true;
    scene.add(moon);

    sunMaterial_ = ShaderMaterial::create();
    sunMaterial_->vertexShader = surfaceVertex;
    sunMaterial_->uniforms.emplace("sunMap", Uniform(static_cast<Texture*>(nullptr)));
    sunMaterial_->fragmentShader = R"(
        uniform sampler2D sunMap;
        varying vec3 vLocalNormal;
        varying vec3 vWorldNormal;
        varying vec3 vWorldPosition;
        void main() {
            vec3 n = normalize(vLocalNormal);
            // Orthographic HMI continuum snapshot, limb darkening removed by HMIIF.
            // Only one hemisphere was observed. The far side is neutral quiet Sun.
            vec2 photoUv = vec2(0.5) + n.xy * 0.4625;
            vec3 photo = texture2D(sunMap, photoUv).rgb;
            float intensity = clamp(dot(photo, vec3(0.2126, 0.7152, 0.0722)) / 0.74, 0.05, 1.1);
            intensity = mix(1.0, intensity, smoothstep(0.0, 0.65, n.z));
            float mu = max(0.0, dot(normalize(vWorldNormal), normalize(cameraPosition - vWorldPosition)));
            // Visible continuum, approximately 5772 K; display exposure is visual.
            vec3 surface = vec3(1.0, 0.96, 0.88) * intensity * (0.4 + 0.6 * mu);
            gl_FragColor = vec4(surface, 1.0);
            #include <encodings_fragment>
        }
    )";
    sunMaterial_->toneMapped = false;
    sun = Mesh::create(SphereGeometry::create(static_cast<float>(695700.0 / 6378.137), 256, 128), sunMaterial_);
    scene.add(sun);
    auto coronaMaterial = overlay(R"(
        varying vec3 vWorldNormal;
        varying vec3 vWorldPosition;
        void main() {
            float mu = abs(dot(normalize(vWorldNormal), normalize(cameraPosition - vWorldPosition)));
            float impact = sqrt(max(0.0, 1.0 - mu*mu)) * 1.15;
            float glow = exp(-max(0.0, impact - 1.0) * 24.0) * (1.0 - smoothstep(1.04, 1.15, impact));
            gl_FragColor = vec4(vec3(1.0, 0.94, 0.83), glow * 0.10);
            #include <encodings_fragment>
        }
    )");
    coronaMaterial->side = Side::Back;
    coronaMaterial->toneMapped = false;
    sun->add(Mesh::create(SphereGeometry::create(static_cast<float>(695700.0 / 6378.137 * 1.15), 96, 48), coronaMaterial));

    cloudMaterial_ = ShaderMaterial::create();
    cloudMaterial_->vertexShader = surfaceVertex;
    cloudMaterial_->fragmentShader = std::string(cloudSampling) + eclipseShader() + R"(
        uniform vec3 solarDirection, moonPosition, eclipseSunPosition;
        uniform float eclipseEnabled, visibility;
        varying vec2 vUv;
        varying vec3 vWorldNormal, vWorldPosition;
        void main() {
            float density = cloudDensity(vUv);
            vec3 normal = normalize(vWorldNormal);
            float light = max(0.0,dot(normal,solarDirection));
            float transmission=mix(1.,visibleSun(vWorldPosition,eclipseSunPosition,695700./6378.137,moonPosition,1737.4/6378.137),eclipseEnabled);
            gl_FragColor = vec4(vec3(1.0,0.98,0.95)*(0.003+2.4*light*transmission),density*0.88*visibility);
            #include <tonemapping_fragment>
            #include <encodings_fragment>
        }
    )";
    cloudMaterial_->uniforms.emplace("solarDirection",Uniform(Vector3(1,0,0)));
    cloudMaterial_->uniforms.emplace("moonPosition",Uniform(Vector3(0,0,60)));
    cloudMaterial_->uniforms.emplace("eclipseSunPosition",Uniform(Vector3(0,0,23455)));
    cloudMaterial_->uniforms.emplace("eclipseEnabled",Uniform(1.f));
    cloudMaterial_->uniforms.emplace("visibility",Uniform(0.f));
    cloudMaterial_->transparent = true;
    cloudMaterial_->depthWrite = false;
    clouds_ = Mesh::create(SphereGeometry::create(1.f + 12.f / 6378.137f, 384, 192), cloudMaterial_);
    geodeticEarthUV(*clouds_);
    clouds_->renderOrder = 3;
    earth->add(clouds_);

    nightMaterial_ = overlay(R"(
        uniform sampler2D nightMap, nightDetailMap;
        uniform vec4 nightDetailBounds;
        uniform float nightDetailEnabled;
        uniform float cloudOpacity, visibility;
        uniform vec3 solarDirection;
        varying vec2 vUv;
        varying vec3 vWorldNormal;
        void main() {
            float night = 1.0 - smoothstep(-0.12, 0.015, dot(normalize(vWorldNormal), solarDirection));
            float emission = max(0.0, texture2D(nightMap, vUv).r - 0.008);
            vec2 regionalUv=vec2(fract(vUv.x-nightDetailBounds.x),vUv.y-nightDetailBounds.y)/nightDetailBounds.zw;
            if(nightDetailEnabled>0.5 && all(greaterThanEqual(regionalUv,vec2(0.0))) && all(lessThanEqual(regionalUv,vec2(1.0)))) {
                vec4 sample=texture2D(nightDetailMap,regionalUv);
                // Suppress blue ocean/background; keep the observed city signal.
                float detailed=max(0.0,min(sample.r,sample.g)-sample.b*0.65-0.008)*1.6;
                vec2 edge=min(regionalUv,1.0-regionalUv);
                emission=mix(emission,detailed,smoothstep(0.0,0.04,min(edge.x,edge.y))*sample.a);
            }
            // Black Marble contains lights on black, no blue ocean emission.
            vec3 lights = vec3(1.0, 0.69, 0.32) * pow(emission, 1.3);
            float transmission = exp(-4.5 * cloudDensity(vUv) * cloudOpacity);
            gl_FragColor = vec4(lights * night * transmission * 1.8, visibility);
            #include <tonemapping_fragment>
            #include <encodings_fragment>
        }
    )");
    nightMaterial_->fragmentShader = std::string(cloudSampling) + nightMaterial_->fragmentShader;
    nightMaterial_->uniforms.emplace("nightDetailMap",Uniform(static_cast<Texture*>(nullptr)));
    nightMaterial_->uniforms.emplace("nightDetailBounds",Uniform(Vector4(0,0,1,1)));
    nightMaterial_->uniforms.emplace("nightDetailEnabled",Uniform(0.f));
    nightMaterial_->uniforms.emplace("nightMap", Uniform(static_cast<Texture*>(nullptr)));
    for (auto* material : {nightMaterial_.get(),cloudMaterial_.get()}) {
        material->uniforms.emplace("cloudMap",Uniform(static_cast<Texture*>(nullptr)));
        material->uniforms.emplace("detailMap",Uniform(static_cast<Texture*>(nullptr)));
        material->uniforms.emplace("detailBounds",Uniform(Vector4(0,0,1,1)));
        material->uniforms.emplace("detailEnabled",Uniform(0.f));
        material->uniforms.emplace("previousDetailMap",Uniform(static_cast<Texture*>(nullptr)));
        material->uniforms.emplace("previousDetailBounds",Uniform(Vector4(0,0,1,1)));
        material->uniforms.emplace("previousDetailEnabled",Uniform(0.f));
        material->uniforms.emplace("detailBlend",Uniform(1.f));
    }
    nightMaterial_->uniforms.emplace("cloudOpacity", Uniform(0.f));
    nightMaterial_->uniforms.emplace("visibility",Uniform(1.f));
    night_ = Mesh::create(SphereGeometry::create(1.00015f, 256, 128), nightMaterial_);
    geodeticEarthUV(*night_);
    night_->renderOrder = 2;
    earth->add(night_);

    atmosphereMaterial_ = overlay(R"(
        uniform vec3 localCamera;
        uniform float visibility;
        uniform vec3 localSun;
        uniform sampler2D opticalDepthMap;
        varying vec3 vLocalPosition;
        const float outerRadius = 1.012543;
        const float metres = 6378137.0;
        const vec3 betaR = vec3(5.802e-6, 13.558e-6, 33.100e-6);
        const vec3 betaM = vec3(21.0e-6);
        vec2 sphere(vec3 origin, vec3 ray, float radius) {
            float b = dot(origin, ray);
            float d = b*b - dot(origin, origin) + radius*radius;
            if (d < 0.0) return vec2(1e20, -1e20);
            float root = sqrt(d);
            return vec2(-b-root, -b+root);
        }
        vec2 density(vec3 p) {
            float h = max(0.0, (length(p) - 1.0) * metres);
            return exp(-h / vec2(8000.0, 1200.0));
        }
        void main() {
            vec3 ray = normalize(vLocalPosition - localCamera);
            vec2 air = sphere(localCamera, ray, outerRadius);
            float begin = max(air.x, 0.0), end = air.y;
            vec2 ground = sphere(localCamera, ray, 1.0);
            if (ground.x > 0.0 && ground.x < end) end = ground.x;
            if (end <= begin) discard;
            float stepLength = (end-begin) / 8.0;
            vec2 optical = vec2(0.0);
            vec3 sumR = vec3(0.0), sumM = vec3(0.0);
            for (int i = 0; i < 8; ++i) {
                vec3 p = localCamera + ray * (begin + (float(i)+0.5) * stepLength);
                vec2 segment = density(p) * stepLength * metres;
                optical += segment * 0.5;
                vec2 blocked = sphere(p, localSun, 1.0);
                if (blocked.x <= 0.0 || blocked.y <= 0.0) {
                    float muSun = dot(normalize(p), localSun);
                    float altitude = max(0.0, (length(p)-1.0)*metres);
                    vec2 lookup = vec2(muSun*0.5+0.5, sqrt(clamp(altitude/80000.0,0.0,1.0)));
                    // Map endpoints to texel centres, preserving the horizon.
                    lookup = (lookup * vec2(255.0,127.0) + 0.5) / vec2(256.0,128.0);
                    vec2 sunlight = texture2D(opticalDepthMap, lookup).rg;
                    vec2 depth = optical + sunlight;
                    vec3 attenuation = exp(-betaR*depth.x - betaM*1.1*depth.y);
                    sumR += attenuation * segment.x;
                    sumM += attenuation * segment.y;
                }
                optical += segment * 0.5;
            }
            float mu = dot(ray, localSun);
            float phaseR = 0.0596831 * (1.0 + mu*mu);
            const float g = 0.76;
            float phaseM = 0.0795775 * (1.0-g*g) / pow(max(0.01, 1.0+g*g-2.0*g*mu), 1.5);
            // Single scattering, finite solar disc/multiple scattering omitted.
            vec3 radiance = 2.5 * (betaR * phaseR * sumR + betaM * phaseM * sumM);
            gl_FragColor = vec4(radiance, visibility);
            #include <tonemapping_fragment>
            #include <encodings_fragment>
        }
    )");
    opticalDepthTexture_ = opticalDepthTexture();
    atmosphereMaterial_->uniforms.emplace("opticalDepthMap", Uniform(opticalDepthTexture_.get()));
    atmosphereMaterial_->uniforms.emplace("localCamera", Uniform(Vector3()));
    atmosphereMaterial_->uniforms.emplace("localSun", Uniform(Vector3(1,0,0)));
    atmosphereMaterial_->uniforms.emplace("visibility",Uniform(1.f));
    atmosphere_ = Mesh::create(SphereGeometry::create(1.f + 80.f / 6378.137f, 192, 96), atmosphereMaterial_);
    atmosphere_->renderOrder = 4;
    earth->add(atmosphere_);
    replaceAssets(std::move(assets));
}

albert::PlanetVisuals::~PlanetVisuals() {
    detailProgress.cancelled=true;
    if(pendingImage_.valid()) pendingImage_.wait();
}

void albert::PlanetVisuals::replaceAssets(DecodedAssets assets) {
    for (size_t i = 0; i < textures_.size(); ++i) {
        if (textures_[i]) textures_[i]->dispose();
        textures_[i] = i==5 ? elevationTexture(std::move(*assets.images[i])) : texture(std::move(*assets.images[i]), (i == 2 || i == 4) ? ColorSpace::NoColorSpace : ColorSpace::sRGB);
    }
    if (moonHeightTexture_) moonHeightTexture_->dispose();
    moonHeightTexture_=elevationTexture(std::move(*assets.moonHeight));
    moonMaterial_->bumpMap=moonHeightTexture_;
    moonMaterial_->bumpScale=1.f/6378137.f;
    auto displace=[](Mesh& body,const Image& image,float radius) {
        auto* positions=body.geometry()->getAttribute<float>("position");
        const auto* uv=body.geometry()->getAttribute<float>("uv");
        for (int i=0;i<positions->count();++i) {
            const unsigned x=std::min(image.width()-1,static_cast<unsigned>(std::clamp(uv->getX(i),0.f,1.f)*(image.width()-1)));
            const unsigned y=std::min(image.height()-1,static_cast<unsigned>(std::clamp(uv->getY(i),0.f,1.f)*(image.height()-1)));
            Vector3 point(positions->getX(i),positions->getY(i),positions->getZ(i));
            point.normalize().multiplyScalar(radius+image.data<float>()[4*(static_cast<size_t>(y)*image.width()+x)]/6378137.f);
            positions->setXYZ(i,point.x,point.y,point.z);
        }
        body.geometry()->computeVertexNormals();body.geometry()->computeBoundingSphere();
        positions->needsUpdate();
    };
    displace(*earth,textures_[5]->image(),1.f);
    displace(*moon,moonHeightTexture_->image(),static_cast<float>(1737.4/6378.137));
    if (oceanTexture_) oceanTexture_->dispose();
    oceanTexture_ = texture(std::move(*assets.oceans), ColorSpace::NoColorSpace);
    earthMaterial_->map = textures_[0];
    earthMaterial_->roughnessMap = oceanTexture_;
    earthMaterial_->bumpMap = textures_[5];
    moonMaterial_->map = textures_[3];
    sunMaterial_->uniforms.at("sunMap").setValue(textures_[4].get());
    auto cloudMap = liveCloudTexture_ ? liveCloudTexture_ : textures_[2];
    cloudMaterial_->uniforms.at("cloudMap").setValue(cloudMap.get());
    if(!cloudDetailTexture_) for(auto* mat:{cloudMaterial_.get(),nightMaterial_.get()}) mat->uniforms.at("detailMap").setValue(cloudMap.get());
    if(!previousCloudDetail_) for(auto* mat:{cloudMaterial_.get(),nightMaterial_.get()}) mat->uniforms.at("previousDetailMap").setValue(cloudMap.get());
    nightMaterial_->uniforms.at("nightMap").setValue(textures_[1].get());
    if(!regionalTextures_[3]) nightMaterial_->uniforms.at("nightDetailMap").setValue(textures_[1].get());
    nightMaterial_->uniforms.at("cloudMap").setValue(cloudMap.get());
    earthMaterial_->needsUpdate();
    moonMaterial_->needsUpdate();
    sunMaterial_->needsUpdate();
    cloudMaterial_->needsUpdate();
}

void albert::PlanetVisuals::setCloudImage(Image image) {
    if (liveCloudTexture_) liveCloudTexture_->dispose();
    liveCloudTexture_ = texture(std::move(image), ColorSpace::NoColorSpace);
    cloudMaterial_->uniforms.at("cloudMap").setValue(liveCloudTexture_.get());
    if(!cloudDetailTexture_) for(auto* mat:{cloudMaterial_.get(),nightMaterial_.get()}) mat->uniforms.at("detailMap").setValue(liveCloudTexture_.get());
    if(!previousCloudDetail_) for(auto* mat:{cloudMaterial_.get(),nightMaterial_.get()}) mat->uniforms.at("previousDetailMap").setValue(liveCloudTexture_.get());
    nightMaterial_->uniforms.at("cloudMap").setValue(liveCloudTexture_.get());
    cloudMaterial_->needsUpdate();
}

void albert::PlanetVisuals::setCloudDetail(Image image, const GeoRegion& region) {
    if(previousCloudDetail_) previousCloudDetail_->dispose();
    previousCloudDetail_=std::move(cloudDetailTexture_);
    cloudDetailTexture_ = texture(std::move(image),ColorSpace::NoColorSpace);
    cloudDetailBlend_=0.f;
    for(auto* material : {nightMaterial_.get(),cloudMaterial_.get()}) {
        material->uniforms.at("previousDetailMap").setValue(previousCloudDetail_ ? previousCloudDetail_.get() : (liveCloudTexture_ ? liveCloudTexture_.get() : textures_[2].get()));
        material->uniforms.at("previousDetailEnabled").setValue(previousCloudDetail_ ? 1.f : 0.f);
        if(cloudDetailBounds_) {const auto& old=*cloudDetailBounds_;material->uniforms.at("previousDetailBounds").setValue(Vector4(
            static_cast<float>((old.west+180.)/360.),static_cast<float>((old.south+90.)/180.),
            static_cast<float>((old.east-old.west)/360.),static_cast<float>((old.north-old.south)/180.)));}
        material->uniforms.at("detailBlend").setValue(0.f);
        material->uniforms.at("detailMap").setValue(cloudDetailTexture_.get());
        material->uniforms.at("detailBounds").setValue(Vector4(
            static_cast<float>((region.west+180.)/360.),static_cast<float>((region.south+90.)/180.),
            static_cast<float>((region.east-region.west)/360.),static_cast<float>((region.north-region.south)/180.)));
        material->uniforms.at("detailEnabled").setValue(1.f);
    }
    cloudDetailBounds_=region;
}

void albert::PlanetVisuals::stageRegionalImages(RegionalImages images) {
    if(uploads_.active()) return;
    prepareRegionalImages(images);
    if(images.day) images.gpu[0]=texture(std::move(*images.day));
    if(images.night) images.gpu[1]=texture(std::move(*images.night),ColorSpace::NoColorSpace);
    if(images.height) images.gpu[2]=elevationTexture(std::move(*images.height));
    if(images.roughness) images.gpu[3]=texture(std::move(*images.roughness),ColorSpace::NoColorSpace);
    if(images.surfaceMask) images.gpu[4]=texture(std::move(*images.surfaceMask),ColorSpace::NoColorSpace);
    for(const auto& item:images.gpu) if(item) uploads_.add(item);
    uploadingRegion_=std::move(images);
}
void albert::PlanetVisuals::updateUploads(GLRenderer* renderer) {
    uploads_.tick(renderer);
    if(!uploads_.active() && uploadingRegion_) {
        auto images=std::move(*uploadingRegion_);uploadingRegion_.reset();setRegionalImages(std::move(images));
    }
    if(!uploads_.active() && uploadingSun_) {
        if(imageWanted_[1]) {if(imageDetail_[1])imageDetail_[1]->dispose();imageDetail_[1]=std::move(uploadingSun_);imageFailed_[1]=false;sunDetailLevel_=uploadingSunLevel_;sunStale_=false;sunMaterial_->uniforms.at("sunMap").setValue(imageDetail_[1].get());}
        else {uploadingSun_->dispose();uploadingSun_.reset();}
    }
}

void albert::PlanetVisuals::setRegionalImages(RegionalImages images) {
    prepareRegionalImages(images);
    const auto& region=images.region;
    if(images.night && images.body!="moon") {
        if(regionalTextures_[3]) regionalTextures_[3]->dispose();
        regionalTextures_[3]=images.gpu[1] ? images.gpu[1] : texture(std::move(*images.night),ColorSpace::NoColorSpace);
        nightMaterial_->uniforms.at("nightDetailMap").setValue(regionalTextures_[3].get());
        nightMaterial_->uniforms.at("nightDetailBounds").setValue(Vector4(
            static_cast<float>((region.west+180.)/360.),static_cast<float>((region.south+90.)/180.),
            static_cast<float>((region.east-region.west)/360.),static_cast<float>((region.north-region.south)/180.)));
        nightMaterial_->uniforms.at("nightDetailEnabled").setValue(1.f);
    }
    if(!images.day) return;
    const size_t slot=(images.body=="moon") ? 2 : 0;
    const size_t bodySlot=(images.body=="moon") ? 1 : 0;
    auto& patch=(images.body=="moon") ? moonDetail_ : earthDetail_;
    auto& parent=(images.body=="moon") ? moon : earth;
    // The high-resolution DEM can lie below the coarse interpolated globe.
    // Remove the coarse surface inside the patch instead of exaggerating the
    // new terrain to draw it on top. The overlap margin is outside the viewport.
    const auto oldMask=surfaceMasks_[bodySlot];
    surfaceMasks_[bodySlot]=images.gpu[4] ? images.gpu[4] : texture(std::move(*images.surfaceMask),ColorSpace::NoColorSpace);
    if((images.body=="moon")) {moonMaterial_->alphaMap=surfaceMasks_[bodySlot];moonMaterial_->needsUpdate();}
    else {earthMaterial_->alphaMap=surfaceMasks_[bodySlot];earthMaterial_->needsUpdate();}
    if(oldMask) oldMask->dispose();
    if(patch) { parent->remove(*patch);patch->geometry()->dispose();patch->material()->dispose(); }
    if(regionalTextures_[slot]) regionalTextures_[slot]->dispose();
    regionalTextures_[slot]=images.gpu[0] ? images.gpu[0] : texture(std::move(*images.day));
    auto geometry=std::move(images.geometry);
    const size_t heightSlot=(images.body=="moon") ? 1 : 0;
    if(images.height) {
        if(regionalHeights_[heightSlot]) regionalHeights_[heightSlot]->dispose();
        regionalHeights_[heightSlot]=images.gpu[2] ? images.gpu[2] : elevationTexture(std::move(*images.height));
    }
    if((images.body=="moon")) {
        moonDetailMaterial_=MeshPhongMaterial::create();
        moonDetailMaterial_->map=regionalTextures_[slot];
        moonDetailMaterial_->lightMap=surfaceFillTextures_[1];
        moonDetailMaterial_->specular=Color(0x050505);moonDetailMaterial_->shininess=3;
        moonDetailMaterial_->bumpMap=regionalHeights_[1];moonDetailMaterial_->bumpScale=1.f/6378137.f;
        // Terrain must populate depth in the opaque pass before orbit overlays.
        moonDetailMaterial_->transparent=false;moonDetailMaterial_->depthWrite=true;
        moonDetailMaterial_->alphaTest=.001f;
        patch=Mesh::create(geometry,moonDetailMaterial_);
    } else {
        auto material=MeshStandardMaterial::create();
        material->map=regionalTextures_[slot];material->roughness=1;material->metalness=0;
        material->lightMap=surfaceFillTextures_[0];
        material->bumpMap=regionalHeights_[0];material->bumpScale=1.f/6378137.f;
        if(regionalTextures_[1]) regionalTextures_[1]->dispose();
        if(images.roughness) {
            regionalTextures_[1]=images.gpu[3] ? images.gpu[3] : texture(std::move(*images.roughness),ColorSpace::NoColorSpace);
            material->roughnessMap=regionalTextures_[1];
        }
        material->transparent=false;material->depthWrite=true;material->alphaTest=.001f;
        earthDetailBounds_=region;
        patch=Mesh::create(geometry,material);
    }
    patch->castShadow=patch->receiveShadow=true;patch->renderOrder=1;parent->add(patch);
}

bool albert::PlanetVisuals::releaseDistantDetails(const std::string& focus,const Vector3& camera,bool enabled,float seconds){
    bool released=false;
    for(size_t i=0;i<2;++i){auto& parent=i?moon:earth;auto& patch=i?moonDetail_:earthDetail_;
        const bool wanted=enabled&&focus==(i?"moon":"earth")&&camera.distanceTo(parent->position)<(i?.9f:2.4f);
        detailIdle_[i]=wanted?0.f:detailIdle_[i]+seconds;
        if(detailIdle_[i]<3.f||!patch)continue;
        if(i){moonMaterial_->alphaMap=opaqueMask_;moonMaterial_->needsUpdate();moonDetailMaterial_.reset();}
        else {earthMaterial_->alphaMap=opaqueMask_;earthMaterial_->needsUpdate();earthDetailBounds_.reset();nightMaterial_->uniforms.at("nightDetailMap").setValue(textures_[1].get());nightMaterial_->uniforms.at("nightDetailEnabled").setValue(0.f);}
        parent->remove(*patch);patch->geometry()->dispose();patch->material()->dispose();patch.reset();
        for(size_t slot:i?std::vector<size_t>{2}:std::vector<size_t>{0,1,3})if(regionalTextures_[slot]){regionalTextures_[slot]->dispose();regionalTextures_[slot].reset();}
        if(regionalHeights_[i]){regionalHeights_[i]->dispose();regionalHeights_[i].reset();}
        if(surfaceMasks_[i]){surfaceMasks_[i]->dispose();surfaceMasks_[i].reset();}
        released=true;
    }
    return released;
}

void albert::PlanetVisuals::updateImageDetail(const Vector3& camera, bool enabled) {
    if(!detailError.empty() && std::chrono::steady_clock::now()>=imageRetry_) retryDetails();
    imageWanted_ = {false,enabled && camera.distanceTo(sun->position)<1200.f,false};
    if(const auto generation=visualAssetGeneration();generation!=detailGeneration_){
        detailGeneration_=generation;
        if(imageDetail_[1]&&visualAssetPack()->availableLevel("sun")>sunDetailLevel_){sunStale_=true;imageFailed_[1]=false;}
    }
    if(pendingImage_.valid() && pendingImage_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        auto result=pendingImage_.get();
        detailError=result.error;
        if(!detailError.empty()) imageRetry_=std::chrono::steady_clock::now()+std::chrono::minutes(2);
        if(result.image && imageWanted_[result.index]) {
            uploadingSun_=texture(std::move(*result.image),ColorSpace::sRGB);uploadingSunLevel_=result.level;
            uploads_.add(uploadingSun_);
        }
    }
    for(size_t i=0;i<3;++i) if(!imageWanted_[i]) {
        if(imageDetail_[i]) imageDetail_[i]->dispose();
        imageDetail_[i].reset(); imageFailed_[i]=false;
    }
    moonMaterial_->map=imageDetail_[0] ? imageDetail_[0] : textures_[3];
    sunMaterial_->uniforms.at("sunMap").setValue((imageDetail_[1] ? imageDetail_[1] : textures_[4]).get());
    nightMaterial_->uniforms.at("nightMap").setValue((imageDetail_[2] ? imageDetail_[2] : textures_[1]).get());
    if(pendingImage_.valid() || uploadingSun_) return;
    for(int i=0;i<3;++i) if(imageWanted_[i] && (!imageDetail_[i]||(i==1&&sunStale_)) && !imageFailed_[i]) {
        imageFailed_[i]=true;
        detailProgress.cancelled=false;
        pendingImage_=std::async(std::launch::async,[this,i] {
            DetailedImage result{i,std::nullopt,{}};
            try {
                detailProgress.stage("Lendo detalhes do Sol",0);
                const auto pack=visualAssetPack();result.level=pack->availableLevel("sun");
                result.image=pack->image("sun",GeoRegion{0,0,1,1},4096,detailProgress);
            } catch(const std::exception& e) { result.error=e.what(); }
            return result;
        });
        break;
    }
}

void albert::PlanetVisuals::update(const Vector3& solarDirection, const Vector3& cameraPosition, const VisualSettings& settings, float seconds) {
    cloudDetailBlend_=animateValue(cloudDetailBlend_,1.f,seconds);
    for(auto* mat:{cloudMaterial_.get(),nightMaterial_.get()}) mat->uniforms.at("detailBlend").setValue(cloudDetailBlend_);
    if(cloudDetailBlend_==1.f && previousCloudDetail_) {
        for(auto* mat:{cloudMaterial_.get(),nightMaterial_.get()}) {mat->uniforms.at("previousDetailMap").setValue(cloudDetailTexture_.get());mat->uniforms.at("previousDetailEnabled").setValue(0.f);}
        previousCloudDetail_->dispose();previousCloudDetail_.reset();
    }
    displayedFill_[0]=animateValue(displayedFill_[0],settings.earthAssist ? settings.earthAssistIntensity : 0.f,seconds);
    displayedFill_[1]=animateValue(displayedFill_[1],settings.moonAssist ? settings.moonAssistIntensity : 0.f,seconds);
    displayedAtmosphere_=animateValue(displayedAtmosphere_,settings.atmosphere ? 1.f : 0.f,seconds);
    displayedClouds_=animateValue(displayedClouds_,settings.clouds ? 1.f : 0.f,seconds);
    displayedNight_=animateValue(displayedNight_,settings.nightLights ? 1.f : 0.f,seconds);
    atmosphereMaterial_->uniforms.at("visibility").setValue(displayedAtmosphere_);
    cloudMaterial_->uniforms.at("visibility").setValue(displayedClouds_);
    nightMaterial_->uniforms.at("visibility").setValue(displayedNight_);
    nightMaterial_->uniforms.at("solarDirection").setValue(solarDirection);
    cloudMaterial_->uniforms.at("solarDirection").setValue(solarDirection);
    cloudMaterial_->uniforms.at("moonPosition").setValue(moon->position);
    cloudMaterial_->uniforms.at("eclipseSunPosition").setValue(sun->position);
    cloudMaterial_->uniforms.at("eclipseEnabled").setValue(settings.shadows ? 1.f : 0.f);
    nightMaterial_->uniforms.at("cloudOpacity").setValue(displayedClouds_);
    earth->updateMatrixWorld();
    Vector3 localCamera = cameraPosition;
    earth->worldToLocal(localCamera);
    Matrix4 inverse;
    inverse.copy(*earth->matrixWorld).invert();
    Vector3 localSun = solarDirection;
    localSun.transformDirection(inverse);
    if(displayedFill_[0]>0.f) updateFillMask(*surfaceFillTextures_[0],surfaceFillDirections_[0],localSun,true);
    const float earthFill=displayedFill_[0]*static_cast<float>(pi);
    earthMaterial_->lightMapIntensity=earthFill;
    if(earthDetail_) earthDetail_->material()->as<MeshStandardMaterial>()->lightMapIntensity=earthFill;
    atmosphereMaterial_->uniforms.at("localCamera").setValue(localCamera);
    atmosphereMaterial_->uniforms.at("localSun").setValue(localSun);
    atmosphere_->visible = displayedAtmosphere_>0.f;
    clouds_->visible = displayedClouds_>0.f;
    if(earthDetail_) earthDetail_->visible=settings.quality && localCamera.length()<2.2f;
    if(moonDetail_) moonDetail_->visible=settings.quality && cameraPosition.distanceTo(moon->position)<.8f;
    const auto earthMask=earthDetail_ && earthDetail_->visible ? surfaceMasks_[0] : opaqueMask_;
    const auto moonMask=moonDetail_ && moonDetail_->visible ? surfaceMasks_[1] : opaqueMask_;
    if(earthMaterial_->alphaMap!=earthMask) {earthMaterial_->alphaMap=earthMask;earthMaterial_->needsUpdate();}
    if(moonMaterial_->alphaMap!=moonMask) {moonMaterial_->alphaMap=moonMask;moonMaterial_->needsUpdate();}
    // Close-up city lights are a visual overlay above the regional terrain.
    // No other body can lie between this near-Earth camera and the surface.
    nightMaterial_->depthTest=!(earthDetail_ && earthDetail_->visible);
    night_->visible = displayedNight_>0.f;
    // A didactic fill restricted to the hemisphere without direct sunlight.
    // The near/far hemispheres have no role in this visual aid.
    moon->updateMatrixWorld();
    inverse.copy(*moon->matrixWorld).invert();
    Vector3 lunarSun=solarDirection;
    lunarSun.transformDirection(inverse);
    if(displayedFill_[1]>0.f) updateFillMask(*surfaceFillTextures_[1],surfaceFillDirections_[1],lunarSun);
    const float fill=displayedFill_[1]*static_cast<float>(pi);
    moonMaterial_->lightMapIntensity=fill;
    if(moonDetailMaterial_) moonDetailMaterial_->lightMapIntensity=fill;
}

void albert::PlanetVisuals::updateBorderMaterial(ShaderMaterial& material,const Vector3& cameraPosition) {
    material.uniforms.at("heightMap").setValue(textures_[5].get());
    const bool detailed=earthDetail_ && earthDetail_->visible && earthDetailBounds_;
    material.uniforms.at("detailHeightMap").setValue(detailed ? regionalHeights_[0].get() : textures_[5].get());
    material.uniforms.at("detailEnabled").setValue(detailed ? 1.f : 0.f);
    if(detailed) {
        const auto& region=*earthDetailBounds_;
        material.uniforms.at("detailBounds").setValue(Vector4(
            static_cast<float>((region.west+180.)/360.),static_cast<float>((region.south+90.)/180.),
            static_cast<float>((region.east-region.west)/360.),static_cast<float>((region.north-region.south)/180.)));
    }
    // A sub-pixel clearance covers interpolation error without a kilometre-high
    // fixed cartographic shell. The line still participates in depth testing.
    const float distance=cameraPosition.distanceTo(earth->position);
    material.uniforms.at("surfaceOffset").setValue(distance>2.1f ? .0008f : std::max(.00005f,(distance-1.f)*.00025f));
}

void albert::setStarEpoch(Points& stars, double jdTDB) {
    stars.material()->as<ShaderMaterial>()->uniforms.at("years").setValue(static_cast<float>((jdTDB - 2451545.) / 365.25));
}

std::shared_ptr<Points> albert::makeStarField(const std::filesystem::path& catalogue, double jdTDB) {
    std::ifstream file(catalogue);
    if (!file) throw std::runtime_error("Catalogo estelar local indisponivel");
    std::string line;
    std::getline(file, line); // Header; generated CSV never quotes numeric fields.
    std::vector<float> positions, motions, colors, sizes;
    while (std::getline(file, line)) {
        std::istringstream row(line);
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(row, field, ',')) fields.push_back(field);
        if (fields.size() != 10) throw std::runtime_error("Catalogo estelar invalido");
        const float magnitude = std::stof(fields[2]);
        const float bv = fields[3].empty() ? 0.65f : std::clamp(std::stof(fields[3]), -0.4f, 2.f);
        // J2000 position and space motion per Julian year (same units). Both are
        // scaled so the position lies on the sky sphere: the direction of
        // position + motion*years is unchanged by the common scale.
        const double x = std::stod(fields[4]), y = std::stod(fields[5]), z = std::stod(fields[6]);
        const double vx = std::stod(fields[7]), vy = std::stod(fields[8]), vz = std::stod(fields[9]);
        const double radius = std::sqrt(x*x + y*y + z*z);
        if (!std::isfinite(radius) || radius <= 0 || !std::isfinite(vx + vy + vz)) throw std::runtime_error("Posicao estelar invalida");
        const double scale = 80000 / radius;
        positions.insert(positions.end(), {static_cast<float>(scale*x), static_cast<float>(scale*z), static_cast<float>(-scale*y)});
        motions.insert(motions.end(), {static_cast<float>(scale*vx), static_cast<float>(scale*vz), static_cast<float>(-scale*vy)});
        // Photometric flux is 10^(-0.4m). Compress its dynamic range for a
        // display; angular point sizes are an artistic visibility aid.
        const float flux = std::pow(10.f, -0.4f * (magnitude + 1.46f));
        const float brightness = std::clamp(std::pow(flux, .22f), .035f, 1.f);
        const float warm = std::clamp((bv - .15f) / 1.6f, 0.f, 1.f);
        const float cool = std::clamp((.45f - bv) / .85f, 0.f, 1.f);
        colors.insert(colors.end(), {brightness * (1.f - .23f*cool), brightness * (1.f - .23f*warm), brightness * (1.f - .55f*warm)});
        sizes.push_back(1.f + brightness * 3.3f);
    }
    if (positions.empty()) throw std::runtime_error("Catalogo estelar vazio");
    auto geometry = BufferGeometry::create();
    geometry->setAttribute("position", FloatBufferAttribute::create(std::move(positions), 3));
    geometry->setAttribute("motion", FloatBufferAttribute::create(std::move(motions), 3));
    geometry->setAttribute("color", FloatBufferAttribute::create(std::move(colors), 3));
    geometry->setAttribute("pointSize", FloatBufferAttribute::create(std::move(sizes), 1));
    auto material = ShaderMaterial::create();material->name="albert:stars"; // WGSL in webgpu_programs.cpp.
    material->vertexColors = true;
    material->transparent = true;
    material->depthWrite = false;
    material->toneMapped = false;
    material->uniforms.emplace("years", Uniform(0.f));
    material->vertexShader = R"(
        attribute float pointSize;
        attribute vec3 motion;
        uniform float years;
        varying vec3 vColor;
        void main() {
            vColor = color;
            gl_PointSize = pointSize;
            gl_Position = projectionMatrix * modelViewMatrix * vec4(normalize(position + motion * years) * 80000.0, 1.0);
        }
    )";
    material->fragmentShader = R"(
        varying vec3 vColor;
        void main() {
            float r = length(gl_PointCoord - 0.5) * 2.0;
            float alpha = 1.0 - smoothstep(0.2, 1.0, r);
            if (alpha < 0.01) discard;
            gl_FragColor = vec4(vColor, alpha);
            #include <encodings_fragment>
        }
    )";
    auto points = Points::create(geometry, material);
    points->frustumCulled = false;
    setStarEpoch(*points, jdTDB);
    return points;
}

Matrix4 albert::moonOrientation(double jdTDB) {
    constexpr double phases[] = {125.045,250.089,260.008,176.625,357.529,311.589,134.963,276.617,34.226,15.134,119.743,239.961,25.053};
    constexpr double rates[] = {-1935.5364525,-3871.072905,475263.3328725,487269.629985,35999.0509575,964468.49931,477198.869325,12006.300765,63863.5132425,-5806.6093575,131.84064,6003.1503825,473327.79642};
    constexpr double raTerms[] = {-3.8787,-.1204,.0700,-.0172,0,.0072,0,0,0,-.0052,0,0,.0043};
    constexpr double decTerms[] = {1.5419,.0239,-.0278,.0068,0,-.0029,.0009,0,0,.0008,0,0,-.0009};
    constexpr double wTerms[] = {3.5610,.1208,-.0642,.0158,.0252,-.0066,-.0047,-.0046,.0028,.0052,.0040,.0019,-.0044};
    const double d = jdTDB - 2451545., t = d / 36525.;
    double ra = 269.9949 + .0031 * t, dec = 66.5392 + .013 * t;
    double w = 38.3213 + 13.17635815 * d - 1.4e-12 * d * d;
    for (size_t i = 0; i < 13; ++i) {
        const double e = std::remainder(phases[i] + rates[i] * t, 360.) * pi / 180.;
        ra += raTerms[i] * std::sin(e);
        dec += decTerms[i] * std::cos(e);
        w += wTerms[i] * std::sin(e);
    }
    ra *= pi / 180.; dec *= pi / 180.; w = std::remainder(w, 360.) * pi / 180.;
    // Node in the ICRF equator, pole, and east basis; W measured from node.
    const double nx = -std::sin(ra), ny = std::cos(ra);
    const double ex = -std::sin(dec) * std::cos(ra), ey = -std::sin(dec) * std::sin(ra), ez = std::cos(dec);
    auto graphical = [](double x, double y, double z) { return Vector3(static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y)); };
    auto x = graphical(nx * std::cos(w) + ex * std::sin(w), ny * std::cos(w) + ey * std::sin(w), ez * std::sin(w));
    auto y = graphical(std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec));
    auto z = graphical(nx * std::sin(w) - ex * std::cos(w), ny * std::sin(w) - ey * std::cos(w), -ez * std::cos(w));
    Matrix4 rotation;
    return rotation.makeBasis(x, y, z);
}
