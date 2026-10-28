#include "roads.hpp"
#include "gpu/programs.hpp"
#include "starbase_cover.hpp"
#include <stb_image.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
namespace {
constexpr double pi=3.141592653589793;
Vector4 four(double a,double b,double c,double d){return Vector4(float(a),float(b),float(c),float(d));}
struct Primitive {Vector4 ends,info,extra;};
void defineRoads();
std::shared_ptr<DataTexture> texture(std::vector<float> values,unsigned width,unsigned height){auto t=DataTexture::create(ImageData(std::move(values)),width,height);t->type=Type::Float;t->minFilter=t->magFilter=Filter::Nearest;t->generateMipmaps=false;return t;}
}
// The terrain roads and scenery slabs use the same metre-scale concrete, including filtered joints.
std::string_view concreteWgsl(){return R"(
fn roadHash(q: vec2f) -> f32 { return fract(sin(dot(q, vec2f(127.1, 311.7))) * 43758.5453); }
fn roadNoise(q: vec2f) -> f32 { let i = floor(q); let a = fract(q); let f = a*a*(3.0-2.0*a);
    return mix(mix(roadHash(i),roadHash(i+vec2f(1,0)),f.x),mix(roadHash(i+vec2f(0,1)),roadHash(i+vec2f(1,1)),f.x),f.y); }
fn roadLine(x: f32, half: f32, pixel: f32) -> f32 { let p = max(pixel, 0.001); return clamp((x+half)/p+0.5,0.0,1.0)-clamp((x-half)/p+0.5,0.0,1.0); }
fn roadIntegral(x: f32, period: f32, width: f32) -> f32 { let q = (x+width*0.5)/period; return floor(q)*width+min(fract(q)*period,width); }
fn roadRepeat(x: f32, period: f32, width: f32, pixel: f32) -> f32 { let p = max(pixel,0.001);return (roadIntegral(x+p*0.5,period,width)-roadIntegral(x-p*0.5,period,width))/p; }
fn concreteSurface(q: vec2f, pixel: vec2f, soot: f32, quality: f32) -> vec3f {
    let footprint = max(pixel.x,pixel.y);
    let fine = quality*(1.0-smoothstep(0.004,0.02,footprint));
    let grain = mix(0.5,roadNoise(q*65.0),fine);let stain = roadNoise(q*0.21);
    let joint = max(roadRepeat(q.x,5.5,0.018,pixel.x),roadRepeat(q.y,5.5,0.018,pixel.y));
    let wet = smoothstep(0.62,0.82,roadNoise(q*0.065+vec2f(17,9)))*smoothstep(0.3,0.7,stain);
    return vec3f(0.42,0.41,0.38)*(0.90+0.15*stain+0.08*(grain-0.5))*(1.0-0.65*joint)*(1.0-0.32*wet)*(1.0-0.72*soot*(0.6+0.4*stain));
}
)";}
void Roads::sync(const core::Scene& scene){
    std::shared_ptr<const core::Scenery> next;
    for(const auto& [id,o]:scene.objects())if(o.visible&&o.scenery&&!o.scenery->roads.empty()){next=o.scenery;break;}
    if(next==source_)return;
    source_=std::move(next);materials_.clear();for(auto& t:cover_)t.reset();index_.reset();data_.reset();segments_=0;slots_=0;if(source_)build(*source_);
}
void Roads::build(const core::Scenery& s){
    std::vector<Primitive> primitives;
    double minx=1e30,miny=1e30,maxx=-1e30,maxy=-1e30;
    auto bounds=[&](double x,double y,double r){minx=std::min(minx,x-r);miny=std::min(miny,y-r);maxx=std::max(maxx,x+r);maxy=std::max(maxy,y+r);};
    for(const auto& road:s.roads){double along=0,total=0;for(std::size_t i=1;i<road.points.size();++i)total+=std::hypot(road.points[i].x-road.points[i-1].x,road.points[i].y-road.points[i-1].y);
        for(std::size_t i=1;i<road.points.size();++i){const auto& a=road.points[i-1];const auto& b=road.points[i];const double length=std::hypot(b.x-a.x,b.y-a.y);if(length<.01)continue;
            const float half=static_cast<float>(road.width/2);primitives.push_back({four(a.x,a.y,b.x,b.y),four(half,static_cast<int>(road.surface),road.highway?1.f:0.f,along),
                four(road.stopStart?7.5f:-1e6f,road.stopEnd?static_cast<float>(total-7.5):-1e6f,0,0)});
            bounds(a.x,a.y,half+3);bounds(b.x,b.y,half+3);along+=length;}}
    segments_=primitives.size();if(!segments_)return;
    // Grounded, fixed boxes mask the roads under buildings and apron slabs; upper decks and pipes do not.
    for(const auto& p:s.parts)if(!p.structure&&p.shape==core::SceneryPart::Shape::Box&&std::abs(p.tilt)<.001&&p.size.x>=2&&p.size.y>=2&&p.centre.z-p.size.z/2<.5){
        const float sn=std::sin(p.heading),cs=std::cos(p.heading);primitives.push_back({four(p.centre.x,p.centre.y,sn,cs),four(p.size.x/2,p.size.y/2,-1,0),four(p.name.find("apron")!=std::string::npos?1:0,0,0,0)});}
    constexpr float cell=16;const int nx=static_cast<int>(std::ceil((maxx-minx)/cell)),ny=static_cast<int>(std::ceil((maxy-miny)/cell));
    std::vector<std::vector<unsigned>> cells(static_cast<std::size_t>(nx*ny));
    for(unsigned k=0;k<primitives.size();++k){const auto& p=primitives[k];float ax,ay,bx,by;
        if(p.info.z<0){const float rx=std::abs(p.ends.w)*p.info.x+std::abs(p.ends.z)*p.info.y,ry=std::abs(p.ends.z)*p.info.x+std::abs(p.ends.w)*p.info.y;
            ax=p.ends.x-rx-p.extra.x*100;bx=p.ends.x+rx+p.extra.x*100;ay=p.ends.y-ry-p.extra.x*100;by=p.ends.y+ry+p.extra.x*100;
        }else{const float r=p.info.x+3;ax=std::min(p.ends.x,p.ends.z)-r;bx=std::max(p.ends.x,p.ends.z)+r;ay=std::min(p.ends.y,p.ends.w)-r;by=std::max(p.ends.y,p.ends.w)+r;}
        const int x0=std::max(0,int(std::floor((ax-minx)/cell))),x1=std::min(nx-1,int(std::floor((bx-minx)/cell))),y0=std::max(0,int(std::floor((ay-miny)/cell))),y1=std::min(ny-1,int(std::floor((by-miny)/cell)));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)cells[y*nx+x].push_back(k+1);}
    for(const auto& c:cells)slots_=std::max(slots_,static_cast<unsigned>(c.size()));
    // No truncated junction lists. Four indices fit in one texel; zero terminates a tile's list.
    const unsigned groups=(slots_+3)/4;std::vector<float> indices(static_cast<std::size_t>(nx*ny*groups*4),0);
    for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){const auto& c=cells[y*nx+x];for(unsigned j=0;j<c.size();++j)indices[((y*nx+x)*groups+j/4)*4+j%4]=static_cast<float>(c[j]);}
    index_=texture(std::move(indices),nx*groups,ny);std::vector<float> values;
    for(const auto& p:primitives)for(const auto& v:{p.ends,p.info,p.extra})values.insert(values.end(),{v.x,v.y,v.z,v.w});
    data_=texture(std::move(values),3,primitives.size());
    const double sn=std::sin(s.latitude),a=6378137.,e2=6.69437999014e-3,n=a/std::sqrt(1-e2*sn*sn);
    // UVs are geodetic, as in the OSM converter. Subtract the origin before scaling to retain centimetres in local coordinates.
    origin_=four((s.longitude+pi)/(2*pi),s.latitude/pi+.5,2*pi*n*std::cos(s.latitude),pi*a*(1-e2)/std::pow(1-e2*sn*sn,1.5));
    grid_=four(minx,miny,nx,ny);geo_=four(cell,groups,slots_,0);
    coverGrid_=four(boca::west,boca::south,boca::cell*boca::size,0);
    if(std::abs(s.latitude-boca::latitude*pi/180)<1e-6&&std::abs(s.longitude-boca::longitude*pi/180)<1e-6){
        coverGrid_.x+=float((boca::longitude*pi/180-s.longitude)*n*std::cos(s.latitude));
        coverGrid_.y+=float((boca::latitude*pi/180-s.latitude)*a*(1-e2)/std::pow(1-e2*sn*sn,1.5));coverGrid_.w=1;
    }
    defineRoads();
}
// Two filtered weight layers share the road material and its geodetic coordinate. The low
// setting averages 2x2 source cells before upload (a quarter of the texture memory).
void Roads::cover(int quality){
    const unsigned slot=quality>0?1:0;if(cover_[slot])return;
    const bool enabled=coverGrid_.w>0;const unsigned step=slot?1:2,width=enabled?boca::size/step:1;
    std::vector<unsigned char> weights(width*width*4,0),shore(weights.size(),0);
    int w=0,h=0,c=0;unsigned char* pixels=nullptr;
    if(enabled){pixels=stbi_load_from_memory(boca::png,sizeof(boca::png),&w,&h,&c,3);
        if(!pixels||w!=boca::size||h!=boca::size){stbi_image_free(pixels);throw std::runtime_error("Invalid built-in Boca Chica cover");}}
    if(enabled)for(unsigned y=0;y<width;++y)for(unsigned x=0;x<width;++x){unsigned a[4]{},b[4]{};
        for(unsigned dy=0;dy<step;++dy)for(unsigned dx=0;dx<step;++dx){const auto* p=pixels+((y*step+dy)*w+x*step+dx)*3;
            if(p[0]==1||p[0]==6)a[0]+=255;else if(p[0]>=2&&p[0]<=4)a[p[0]-1]+=255;
            b[0]+=p[0]==5?255:0;b[1]+=p[0]==6?255:0;b[2]+=unsigned(p[1])*256+p[2];}
        const unsigned count=step*step,coast=(b[2]+count/2)/count;const auto offset=(y*width+x)*4;
        for(unsigned k=0;k<4;++k)weights[offset+k]=static_cast<unsigned char>(a[k]/count);
        for(unsigned k=0;k<2;++k)shore[offset+k]=static_cast<unsigned char>(b[k]/count);
        shore[offset+2]=static_cast<unsigned char>(coast/256);shore[offset+3]=static_cast<unsigned char>(coast%256);}
    stbi_image_free(pixels);
    auto make=[&](std::vector<unsigned char> values){auto t=DataTexture::create(ImageData(std::move(values)),width,width*2);
        t->minFilter=Filter::LinearMipmapLinear;t->magFilter=Filter::Linear;t->generateMipmaps=true;t->anisotropy=slot?4:1;return t;};
    weights.insert(weights.end(),shore.begin(),shore.end());cover_[slot]=make(std::move(weights));
}
std::shared_ptr<Material> Roads::material(const std::shared_ptr<MeshStandardMaterial>& base,int quality){
    if(!base||!data_)return base;
    auto& m=materials_[base->id];if(!m){m=ShaderMaterial::create();m->name="albert:roads";m->lights=true;}
    m->side=base->side;m->visible=base->visible;m->transparent=base->transparent;m->depthWrite=base->depthWrite;
    cover(quality);const unsigned slot=quality>0?1:0;
    auto& u=m->uniforms;u["coverMap"].setValue(cover_[slot].get());u["coverGrid"].setValue(coverGrid_);u["colour"].setValue(four(base->color.r,base->color.g,base->color.b,base->opacity));u["emissive"].setValue(four(0,0,0,base->bumpMap&&base->bumpMap->wrapS==TextureWrapping::Repeat?1:0));
    u["params"].setValue(four(base->roughness,base->metalness,base->alphaTest,base->lightMapIntensity));
    u["flags"].setValue(four(base->map&&base->map->colorSpace==ColorSpace::sRGB?1:0,2,base->alphaMap?1:0,base->lightMap?1:0));
    u["specular"].setValue(four(0,0,0,base->bumpScale));u["more"].setValue(four(base->bumpMap?1:0,base->roughnessMap?2:0,base->side==Side::Double?1:0,0));
    u["map"].setValue(base->map.get());u["alphaMap"].setValue(base->alphaMap.get());u["lightMap"].setValue(base->lightMap.get());u["bumpMap"].setValue(base->bumpMap.get());u["roughnessMap"].setValue(base->roughnessMap.get());
    u["roadOffset"].setValue(four(((source_->longitude+pi)/(2*pi)-origin_.x)*-origin_.z,(source_->latitude/pi+.5-origin_.y)*-origin_.w,0,0));
    u["roadIndex"].setValue(index_.get());u["roadData"].setValue(data_.get());u["roadOrigin"].setValue(origin_);u["roadGrid"].setValue(grid_);auto geo=geo_;geo.w=quality;u["roadGeo"].setValue(geo);return m;
}
namespace {
void defineRoads(){
    static const bool defined=[](){using gpu::Uniform;auto p=gpu::surfaceProgram();
        for(const char* n:{"colour","emissive","params","flags","specular","more","roadOrigin","roadGrid","roadGeo","roadOffset","coverGrid"})p.uniforms.emplace_back(n,Uniform::vec4);
        p.exactTextures={"roadIndex","roadData"};p.textures.push_back("roadIndex");p.textures.push_back("roadData");p.textures.push_back("coverMap");p.attributes.emplace_back("geoResidual",2);
        auto& w=p.wgsl;auto at=w.find("more: vec4f }");w.replace(at,std::string("more: vec4f }").size(),"more: vec4f, roadOrigin: vec4f, roadGrid: vec4f, roadGeo: vec4f, roadOffset: vec4f, coverGrid: vec4f }");
        at=w.find("@location(5) worldNormal: vec3f }");w.replace(at,std::string("@location(5) worldNormal: vec3f }").size(),"@location(5) worldNormal: vec3f, @location(6) road: vec2f }");
        at=w.find("@location(3) uv2: vec2f) -> Varyings");w.replace(at,std::string("@location(3) uv2: vec2f) -> Varyings").size(),"@location(3) uv2: vec2f, @location(4) geoResidual: vec2f) -> Varyings");
        at=w.find("    return o;\n}");w.insert(at,"    o.road = (uv2 - material.roadOrigin.xy) * material.roadOrigin.zw + geoResidual * material.roadOrigin.zw + material.roadOffset.xy;\n"+std::string(R"(
    // Only the drawn vertices move: neither the DEM provider nor Terrain::ground changes.
    let coverUV = (o.road-material.coverGrid.xy)/material.coverGrid.z;
    let inside = all(coverUV>vec2f(0.0))&&all(coverUV<vec2f(1.0));
    let border = min(min(coverUV.x,coverUV.y),min(1.0-coverUV.x,1.0-coverUV.y))*material.coverGrid.z;
    let water = textureSampleLevel(coverMap,coverSampler,coverUV*vec2f(1.0,0.5),0.0).r * material.coverGrid.w * select(0.0,1.0,inside)*smoothstep(0.0,128.0,border);
    if(frame.oceanEast.w>0.0&&water>0.0){
        let up = cross(frame.oceanEast.xyz,frame.oceanNorth.xyz);
        let rel = (o.world-frame.ocean.xyz)*frame.oceanEast.w;let level = rel-up*dot(rel,up);
        let above = dot(rel,up)+dot(level,level)/12742000.0;
        o.world -= up*(above/frame.oceanEast.w)*smoothstep(0.55,0.98,water);
        o.position = (frame.view*vec4f(o.world,1.0)).xyz;o.clip=frame.projection*vec4f(o.position,1.0);
    }
)"));
        at=w.find("@fragment fn fs_main");w.insert(at,std::string(concreteWgsl())+R"(
@group(2) @binding(11) var roadIndex: texture_2d<f32>;
@group(2) @binding(12) var roadIndexSampler: sampler;
@group(2) @binding(13) var roadData: texture_2d<f32>;
@group(2) @binding(14) var roadDataSampler: sampler;
@group(2) @binding(15) var coverMap: texture_2d<f32>;
@group(2) @binding(16) var coverSampler: sampler;
struct Cover { colour: vec3f, water: f32, sea: f32, wet: f32, valid: f32, relief: f32 };
fn coverRelief(q: vec2f, pixel: f32) -> f32 {
    return roadNoise(q*0.38)*(1.0-smoothstep(0.4,1.6,pixel));
}
fn coastValue(p: vec4f) -> f32 {return (p.b*65280.0+p.a*255.0-32768.0)/128.0;}
fn coastAt(uv: vec2f) -> f32 {
    // Decode before bilinear interpolation. Some UNORM samplers round filtered
    // channels separately, losing the high byte's fraction (two-metre surf steps).
    let size = vec2i(textureDimensions(coverMap));
    let p = (uv*vec2f(1.0,0.5)+vec2f(0.0,0.5))*vec2f(size)-0.5;let i = vec2i(floor(p));let f = fract(p);
    let lo = vec2i(0,size.y/2);let hi = size-1;
    let a = coastValue(textureLoad(coverMap,clamp(i,lo,hi),0));
    let b = coastValue(textureLoad(coverMap,clamp(i+vec2i(1,0),lo,hi),0));
    let c = coastValue(textureLoad(coverMap,clamp(i+vec2i(0,1),lo,hi),0));
    let d = coastValue(textureLoad(coverMap,clamp(i+vec2i(1,1),lo,hi),0));
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
fn coverSurface(q: vec2f, dx: vec2f, dy: vec2f, imagery: vec3f) -> Cover {
    let uv = (q-material.coverGrid.xy)/material.coverGrid.z;
    if(material.coverGrid.w<0.5||any(uv<vec2f(0.0))||any(uv>vec2f(1.0))){return Cover(imagery,0.0,0.0,0.0,0.0,0.0);}
    let atlasScale = vec2f(1.0,0.5);
    let weights = textureSampleGrad(coverMap,coverSampler,uv*atlasScale,dx*atlasScale/material.coverGrid.z,dy*atlasScale/material.coverGrid.z);
    let shore = textureSampleGrad(coverMap,coverSampler,uv*atlasScale+vec2f(0.0,0.5),dx*atlasScale/material.coverGrid.z,dy*atlasScale/material.coverGrid.z);
    let boundary = min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y))*material.coverGrid.z;
    let valid = smoothstep(0.0,128.0,boundary)*material.coverGrid.w;
    let pixel = max(length(dx),length(dy));let detail = material.roadGeo.w;
    // Each octave returns to its mean before it becomes subpixel. No distant grain or grass sparkle.
    let broad = mix(0.5,roadNoise(q*0.018),1.0-smoothstep(12.0,45.0,pixel));
    let medium = mix(0.5,roadNoise(q*0.18),1.0-smoothstep(1.5,5.0,pixel));
    var fine = 0.5;
    if(detail>0.0){fine=mix(0.5,roadNoise(q*3.0),1.0-smoothstep(0.06,0.3,pixel));}
    let texture = 0.77+0.36*broad+0.28*(medium-0.5)+0.16*(fine-0.5);
    let maskCell = material.coverGrid.z/f32(textureDimensions(coverMap).x);
    let wetLod = max(2.0,log2(max(pixel/maskCell,1.0)));
    let blurredWater = textureSampleLevel(coverMap,coverSampler,uv*atlasScale,wetLod).r;
    let damp = clamp((blurredWater-weights.r)*2.2,0.0,1.0);
    let sand = vec3f(0.48,0.43,0.32)*(1.0-0.32*damp);
    let marsh = mix(vec3f(0.085,0.105,0.033),vec3f(0.18,0.19,0.075),broad);
    let flats = mix(vec3f(0.28,0.27,0.21),vec3f(0.43,0.405,0.32),0.35+0.3*medium)*(1.0-0.28*damp);
    let scrub = mix(vec3f(0.055,0.071,0.025),vec3f(0.13,0.135,0.055),medium);
    let land = weights.gba;let total = dot(land,vec3f(1.0))+shore.r;
    let painted = (marsh*land.x+flats*land.y+sand*land.z+scrub*shore.r)/max(total,0.001);
    // Keep the satellite's luminance variations and some chroma underneath every class.
    let variation = clamp(dot(imagery,vec3f(0.2126,0.7152,0.0722))/0.20,0.65,1.35);
    var colour = mix(imagery,painted*(0.78+0.22*variation),total*0.72*valid);
    // Unmapped coastal grassland keeps its observed colour, with the same filtered
    // metre-scale tussocks as the mapped vegetation rather than a smooth satellite texel.
    colour *= mix(1.0,texture,valid);
    var coast = coastValue(shore);let sea = shore.g;
    if(abs(coast)<96.0&&pixel<maskCell*2.0){coast=coastAt(uv);}
    let waterColour = mix(vec3f(0.055,0.13,0.15),vec3f(0.018,0.10,0.14),sea)*(0.88+0.18*broad);
    colour = mix(colour,mix(waterColour,imagery,0.18),weights.r*valid);
    // A wet beach margin and two broad, footprint-filtered breakers on the seaward side.
    let aa = max(2.0,pixel);let wetBeach = (1.0-smoothstep(4.0,22.0,-coast))*(1.0-smoothstep(-aa,aa,coast));
    colour *= 1.0-0.20*wetBeach*valid;
    let wave = coast-8.0-2.5*sin(q.y*0.035+frame.ocean.w*frame.weather.z);
    let foam = max(roadLine(wave,3.5,aa),roadLine(wave-15.0,2.0,aa)*0.55)*(1.0-smoothstep(35.0,70.0,coast));
    let breakup = mix(0.7,0.45+0.55*roadNoise(q*0.16),1.0-smoothstep(0.8,4.0,pixel));
    colour = mix(colour,vec3f(0.62,0.68,0.65),foam*breakup*valid*0.72);
    let relief = mix(0.025,0.20,clamp(weights.g+shore.r+(1.0-total)*0.5,0.0,1.0));
    return Cover(colour,weights.r*valid,sea*valid,damp*valid,valid,relief);
}
// Return albedo and coverage; all derivatives are computed before the spatially varying loop.
fn roadSurface(q: vec2f, dx: vec2f, dy: vec2f) -> vec4f {
    let tile = vec2i(floor((q-material.roadGrid.xy)/material.roadGeo.x));
    if (any(tile<vec2i(0)) || any(tile>=vec2i(material.roadGrid.zw))) { return vec4f(0.0); }
    let footprint = max(length(dx),length(dy)); var distance = 1e6; var score = 1e6;
    var info = vec4f(0.0); var extra = vec4f(0.0); var across = 0.0; var along = 0.0; var lateralPixel = 0.01; var alongPixel = 0.01; var soot = 0.0;
    for (var j = 0; j < i32(material.roadGeo.z); j++) {
        let ids = textureLoad(roadIndex,vec2i(tile.x*i32(material.roadGeo.y)+j/4,tile.y),0);
        let id = i32(ids[u32(j%4)])-1; if (id<0) { break; }
        let ends = textureLoad(roadData,vec2i(0,id),0); let properties = textureLoad(roadData,vec2i(1,id),0);
        if (properties.z<0.0) {
            let rel = q-ends.xy;let local = vec2f(dot(rel,vec2f(ends.w,-ends.z)),dot(rel,ends.zw));let d = abs(local)-properties.xy;
            if (all(d<vec2f(0.0))) { return vec4f(0.0); }
            let mask = textureLoad(roadData,vec2i(2,id),0);
            soot = max(soot,mask.x*exp(-length(max(d,vec2f(0.0)))/35.0));continue;
        }
        let delta = ends.zw-ends.xy;let len = length(delta);let tangent = delta/max(len,0.001);let normal = vec2f(-tangent.y,tangent.x);
        let t = clamp(dot(q-ends.xy,tangent),0.0,len);let d = length(q-ends.xy-tangent*t)-properties.x;
        distance = min(distance,d);let rank = d-select(0.0,100.0,properties.z>0.5&&d<0.5);
        if (rank<score) {score=rank;info=properties;extra=textureLoad(roadData,vec2i(2,id),0);across=dot(q-ends.xy,normal);along=properties.w+t;
            lateralPixel=abs(dot(dx,normal))+abs(dot(dy,normal));alongPixel=abs(dot(dx,tangent))+abs(dot(dy,tangent));}
    }
    if (distance>max(2.0,footprint)) {return vec4f(0.0);}
    let sand = info.y>1.5;let feather = select(0.18,1.2,sand);
    let fine = material.roadGeo.w*(1.0-smoothstep(0.004,0.02,footprint));let grain = mix(0.5,roadNoise(q*65.0),fine); // Its 1.5 cm grain only while a pixel resolves it (else it sparkles).
    let coarse = 1.0-smoothstep(0.1,0.4,footprint);let stain = roadNoise(q*0.21);let wear = 0.75+0.25*mix(0.5,roadNoise(q*2.5),coarse);
    var colour = vec3f(0.075,0.078,0.080)*(0.85+0.3*grain);
    let tw = (abs(abs(across)-1.8)-0.85)/0.23;let tyre = exp(-tw*tw); // Squared, not pow(): pow of a negative base is undefined on GPUs (NaN bands).
    colour *= 1.0-0.20*tyre;
    let repair = smoothstep(0.7,0.78,roadNoise(q*0.12));colour *= 1.0-0.22*repair;
    let crack = roadLine(sin(q.x*0.7+sin(q.y*0.6))*0.6,0.012,max(footprint,0.01)) * smoothstep(0.65,0.8,stain);
    colour *= 1.0-0.45*crack*material.roadGeo.w;
    if (info.y>0.5&&info.y<1.5) {
        colour = concreteSurface(vec2f(along,across),vec2f(alongPixel,lateralPixel),soot,material.roadGeo.w);
    }
    if (sand) {colour=mix(vec3f(0.34,0.30,0.23),vec3f(0.50,0.43,0.32),select(0.0,1.0,info.y>2.5))*(0.9+0.2*stain);let sw = (abs(across)-0.85)/0.22;colour*=1.0-0.13*exp(-sw*sw);}
    // Conservative no-passing paint near this industrial site: two 12 cm yellow lines, 12 cm white edges.
    let yellow = max(roadLine(across-0.13,0.06,lateralPixel),roadLine(across+0.13,0.06,lateralPixel))*info.z;
    let edge = max(roadLine(across-3.66,0.06,lateralPixel),roadLine(across+3.66,0.06,lateralPixel))*info.z;
    let stop = max(roadLine(along-extra.x,0.20,alongPixel),roadLine(along-extra.y,0.20,alongPixel))*(1.0-smoothstep(info.x-0.4,info.x,abs(across)))*(1.0-info.z);
    colour=mix(colour,vec3f(0.72,0.49,0.025),yellow*wear);colour=mix(colour,vec3f(0.78,0.77,0.70),max(edge,stop)*wear);
    let fray = (mix(0.5,roadNoise(q*3.0),coarse)-0.5)*select(0.20,0.5,sand);
    // Under a pixel wider than the road, only the share of it the road covers (from afar a 10 m road is not a dark line).
    let cover = (1.0-smoothstep(-feather-footprint*0.5,feather+footprint*0.5,distance+fray))*select(1.0,0.65,sand)*min(1.0,2.0*info.x/max(footprint,1e-3));
    return vec4f(colour,cover);
}
)");
        at=w.find("    let light = airLight");w.insert(at,R"(
    let groundDx = dpdx(v.road);let groundDy = dpdy(v.road);
    let cover = coverSurface(v.road,groundDx,groundDy,base.rgb);
    let road = roadSurface(v.road,groundDx,groundDy);
    base = vec4f(mix(cover.colour,road.rgb,road.a),base.a);
    let sigmaX = dpdx(v.position);let sigmaY = dpdy(v.position);
    if(material.roadGeo.w>0.0&&cover.valid>0.0){
        let pixel = max(length(groundDx),length(groundDy));let height = coverRelief(v.road,pixel);
        let units = length(sigmaX)/max(length(groundDx),0.001);
        let amplitude = cover.relief*cover.valid*(1.0-cover.water)*(1.0-road.a)*units;
        let bx = (coverRelief(v.road+groundDx,pixel)-height)*amplitude;
        let by = (coverRelief(v.road+groundDy,pixel)-height)*amplitude;
        let r1 = cross(sigmaY,n);let r2 = cross(n,sigmaX);let det = dot(sigmaX,r1)*face;
        n = normalize(max(abs(det),1e-30)*n-sign(det)*(bx*r1+by*r2));
    }
)");
        at=w.find("        if (material.more.y > 1.5");w.insert(at,R"(
        let lagoon = max(cover.water-cover.sea,0.0)*(1.0-road.a);
        roughness = mix(roughness,mix(0.88,0.68,cover.wet),cover.valid);
        roughness = mix(roughness,0.24,cover.water*(1.0-road.a));
        roughness = mix(roughness,0.20,lagoon);
        if(frame.oceanEast.w>0.0&&cover.water>0.0){
            let up = cross(frame.oceanEast.xyz,frame.oceanNorth.xyz);
            var calm = normalize((frame.view*vec4f(up,0.0)).xyz);
            // Sheltered water gets centimetre ripples, not the Gulf's deep-water spectrum.
            let ripple = (1.0-smoothstep(0.12,0.8,max(length(groundDx),length(groundDy))))*material.roadGeo.w;
            calm = normalize(calm+vec3f(sin(v.road.x*1.3+v.road.y*0.7),cos(v.road.y*1.1),0.0)*0.018*ripple);
            n = normalize(mix(n,calm,cover.water*(1.0-road.a)));
        }
)");
        // The OSM mask overrides imagery's blue-colour water classifier only inside its bounds.
        const std::string oldWater="let water = (1.0 - smoothstep(0.35, 0.5, roughTexel.g)) * select(1.0, 1.0 - smoothstep(1.0, 3.0, above), frame.ocean.w > 0.0);";
        at=w.find(oldWater);w.replace(at,oldWater.size(),"let water = mix((1.0-smoothstep(0.35,0.5,roughTexel.g))*select(1.0,1.0-smoothstep(1.0,3.0,above),frame.ocean.w>0.0),cover.sea,cover.valid)*(1.0-road.a);");
        at=w.find("if (material.more.y > 1.5 && frame.weather.z > 0.0 && frame.air.w > 0.0)");
        w.replace(at,std::string("if (material.more.y > 1.5 && frame.weather.z > 0.0 && frame.air.w > 0.0)").size(),"if ((material.more.y > 1.5 || cover.sea > 0.0) && frame.weather.z > 0.0 && frame.air.w > 0.0)");
        gpu::define("albert:roads",std::move(p));return true;}();(void)defined;
}
}
}
