#include "animation.hpp"
#include "borders.hpp"
#include "paths.hpp"
#include "visuals.hpp"
#include "objects.hpp"
#include "asset_pack.hpp"
#include "core/bodies.hpp"
#include "rocks.hpp"
#include "ring_systems.hpp"
#include "comets.hpp"
#include "eclipses.hpp"
#include "layers.hpp"
#include "core/layers.hpp"
#include "core/model.hpp"
#include "model_fixture.hpp"
#include "gpu/context.hpp"
#include "gpu/scene.hpp"
#include "lighting.hpp"
#include "sound.hpp"
#include "webgpu_programs.hpp"
#include <string_view>
#include <memory>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <vector>

using namespace threepp;
namespace {
void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
void controls() {
    const albert::VisualSettings settings;
    require(albert::flightEase(-1)==0 && albert::flightEase(2)==1,"Flight endpoints unclamped");
    require(albert::flightEase(.01f)<.00002f && albert::flightEase(.99f)>.99998f,"Flight ramp is abrupt");
    float previous=0;for(int i=0;i<=1000;++i){const float t=i/1000.f, value=albert::flightEase(t);require(value>=previous-1e-6f && std::abs(value+albert::flightEase(1-t)-1)<2e-6f,"Flight curve reverses or is asymmetric");previous=value;}
    require(albert::flightDuration(10,1e6f)<=4.8f && albert::flightDuration(0,0)>2.f,"Flight duration unbounded or too short");
    for(const auto scales:{Vector3(1e8f,1.f,4.f),Vector3(1e10f,.00001f,1e5f),Vector3(1.f,4.f,1.f)}){
        float last=0;
        for(int i=0;i<=1000;++i){const float t=albert::flightTravel(i/1000.f,scales.x,scales.y,scales.z);require(std::isfinite(t)&&t>=last&&t>=0&&t<=1,"Adaptive camera travel reversed or lost precision");last=t;}
        require(last==1,"Adaptive travel missed its destination");
    }
    require(albert::flightTravel(.1f,1e8f,1.f,4.f)*1e8f<50.f,"Distant travel instantly abandoned the initial subject");
    require((1.f-albert::flightTravel(.9f,1e6f,4.f,1.f))*1e6f<50.f,"Distant travel rushed the destination");
    {
        using V=albert::core::Vector;
        auto visible=[](double x,double radius=1.){return albert::solarVisibility({},V{0,0,10000},100.,V{x,0,100},radius);};
        require(visible(0)<1e-12&&visible(3)==1.,"Total/no eclipse disk geometry failed");
        require(std::abs(visible(0,.5)-.75)<.0001,"Annular eclipse lost the visible solar ring");
        require(std::abs(visible(1)-.609)<.002,"Partial eclipse disk overlap is incorrect");
        require(albert::solarVisibility({},V{0,0,10000},100.,V{0,0,20000},400.)==1.,"Body behind the Sun caused an eclipse");
        double previous=0;for(int i=0;i<=300;++i){const double value=visible(i*.01);require(value+1e-12>=previous,"Penumbra is discontinuous or reverses");previous=value;}
        require(albert::solarVisibility(V{0,0,384400000},V{0,0,-149597870700},695700000.,{},6378137.)==0.,"Lunar totality did not block direct sunlight");
        require(albert::solarVisibility({},V{0,0,149597870700},695700000.,V{0,0,350000000},1737400.)==0.,"Solar umbra did not reach Earth");
    }
    require(albert::artisticRock("67p")&&albert::artisticRock("halley")&&albert::artisticRock("vesta")&&!albert::artisticRock("moon")&&!albert::artisticRock("titan"),"Artistic rocks replaced a photographic or spherical moon");
    {   // Faint ring profiles keep each ring's equivalent width and position (PDS Ring-Moon Systems Node).
        const auto* uranus=albert::ringSystem("uranus");
        require(uranus&&albert::ringSystem("jupiter")&&albert::ringSystem("neptune")&&!albert::ringSystem("saturn"),"Faint ring systems changed");
        const auto profile=albert::ringProfile(*uranus,8192);const double step=(profile.outerKm-profile.innerKm)/8192;double dense=0,expected=0;
        for(size_t i=0;i<profile.tau.size();i+=2)dense+=profile.tau[i]*step;
        for(const auto& b:uranus->bands)if(!b.dust)expected+=b.tau*(b.outerKm-b.innerKm);
        require(std::abs(dense-expected)<1e-4*expected,"Ring profile lost equivalent width");
        require(profile.tau[2*static_cast<size_t>((51149-profile.innerKm)/step)]>.9f&&albert::ringReferenceReflectance(*uranus,profile)>0,"Epsilon ring misplaced");
    }
    {   // Measured small bodies are built-in solids; without their library assets they fall back to rocks.
        albert::core::Scene registry;auto world=Scene::create();albert::SceneObjects objects(*world);auto solid=Mesh::create(SphereGeometry::create(.5f,8,4));
        registry.include(*albert::core::localOrbit("hyperion"));registry.include(*albert::core::localOrbit("phoebe"));objects.bind("phoebe",solid);objects.sync(registry,0,{});
        require(registry.get("hyperion").appearance.shape==albert::core::Shape::Builtin&&objects.nodes().contains("hyperion")&&objects.nodes().at("hyperion")!=solid&&objects.nodes().at("phoebe")==solid,"Measured body without assets vanished or replaced its bound solid");
    }
    {   // Height-mapped relief rises above the catalogue sphere: Vesta's high limb must stay pickable.
        albert::core::Scene registry;auto world=Scene::create();albert::SceneObjects objects(*world);const float earth=static_cast<float>(albert::core::earthRadiusMeters);
        auto globe=Mesh::create(SphereGeometry::create(static_cast<float>(albert::core::surfaceRadius("vesta"))/earth,32,16));
        // Floating origin at Vesta, as in the application: float precision at 2.5 AU would be ~44 km.
        registry.include(*albert::core::localOrbit("vesta"));objects.bind("vesta",globe);objects.sync(registry,0,registry.evaluate("vesta",0).state.positionMeters);
        require(globe->position.length()<1e-6f,"Floating origin did not centre the body");
        Raycaster ray;ray.ray.origin.copy(globe->position).add(Vector3(285000.f/earth,0,10000000.f/earth));ray.ray.direction.set(0,0,-1);
        require(objects.pick(registry,ray)==std::optional<albert::core::ObjectId>("vesta"),"Relief above the catalogue sphere cannot be picked");
        ray.ray.origin.copy(globe->position).add(Vector3(300000.f/earth,0,10000000.f/earth));
        require(!objects.pick(registry,ray),"Picking reached beyond the measured relief");
    }
    for(const auto id:{"67p","halley","vesta","pallas"}){
        const auto rock=albert::makeArtisticRock(*albert::core::localOrbit(id));const auto& p=rock->geometry()->getAttribute<float>("position")->array();
        double volume=0;float shortest=1e6f,longest=0;
        for(size_t i=0;i<p.size();i+=3){const float r=std::hypot(p[i],p[i+1],p[i+2]);require(std::isfinite(r)&&r>.1f,"Artistic mesh has invalid or collapsed vertices");shortest=std::min(shortest,r);longest=std::max(longest,r);}
        for(size_t i=0;i<p.size();i+=9){Vector3 a(p[i],p[i+1],p[i+2]),b(p[i+3],p[i+4],p[i+5]),c(p[i+6],p[i+7],p[i+8]);volume+=a.dot(b.cross(c))/6.;}
        require(longest/shortest>1.15f,"Rock retained a spherical silhouette");
        require(std::abs(std::abs(volume)-3.141592653589793/6.)<.0001,"Artistic surface changed the declared equivalent volume");
        require(p.size()/3<=65000,"Rock geometry exceeded its memory budget");
    }
    {
        albert::core::Scene registry;auto world=Scene::create();albert::SceneObjects objects(*world);
        auto earth=Mesh::create(SphereGeometry::create(1.f,16,8));earth->scale.y=.5f;
        auto moon=Mesh::create(SphereGeometry::create(.3f,16,8));moon->position.z=1.5f;
        objects.bind("earth",earth);objects.bind("moon",moon);
        Raycaster ray(Vector3(0,0,4),Vector3(0,0,-1));
        require(objects.pick(registry,ray)=="moon","Hover did not select the nearest visible globe");
        moon->visible=false;
        require(objects.pick(registry,ray)=="earth","Hidden globe blocked object picking");
        ray.set(Vector3(0,.75f,4),Vector3(0,0,-1));
        require(!objects.pick(registry,ray),"Hover ignored globe oblateness");
        ray.set(Vector3(0,0,4),Vector3(0,0,1));
        require(!objects.pick(registry,ray),"Object behind the camera was clickable");
        ray.set(Vector3(0,0,4),Vector3(0,0,-1));ray.farPlane=2.f;
        require(!objects.pick(registry,ray),"Clipped object was clickable");
    }
    // Arrival must spend visible time between small and full apparent size;
    // test a 20:1 framing range and an extreme reset without cancellation.
    float lastSize=0;for(int i=0;i<=1000;++i){const float t=i/1000.f;const float d=albert::framingDistance(80.f,4.f,albert::flightEase(t));const float size=4.f/d;require(size>=lastSize-1e-6f&&std::abs(size-std::lerp(.05f,1.f,albert::flightEase(t)))<1e-6f,"Planet arrival accelerates in screen space");lastSize=size;}
    require(albert::framingDistance(1e10f,4.f,1.f)==4.f&&albert::framingDistance(4.f,1e10f,1.f)==1e10f,"Extreme framing endpoint lost precision");
    require(!albert::AuxiliaryLight{}.enabled,"Generic auxiliary light must start disabled");
    require(!settings.earthAssist && !settings.moonAssist,"Auxiliary lights must start disabled");
    require(!settings.clouds,"Live clouds must start disabled");
    albert::RegionalImages slope;slope.body="moon";slope.region={-10,-10,10,10};
    slope.day=Image(std::vector<unsigned char>(4,255),1,1);
    slope.height=Image(std::vector<float>{0,0,0,1,1000,1000,1000,1,0,0,0,1,1000,1000,1000,1},2,2);
    albert::prepareRegionalImages(slope);const auto* terrainVertices=slope.geometry->getAttribute<float>("position");
    const unsigned center=96*193+96;const float radius=std::hypot(terrainVertices->getX(center),terrainVertices->getY(center),terrainVertices->getZ(center));
    require(std::abs(radius-(1737400.f+520.f)/6378137.f)<1e-7f,"Regional terrain introduced nearest-neighbour terraces");
    require(albert::regionRelevant({170,-10,190,10},{-185,-10,-175,10}),"Moving across the date line cancelled a useful crop");
    require(albert::regionSufficient({170,-10,190,10},{-189,-9,-171,9}),"Existing overlapping detail was not reused");
    require(!albert::regionSufficient({-30,-30,30,30},{-5,-5,5,5}),"Zooming did not request finer detail");
    require(!albert::regionRelevant({0,-10,10,10},{90,-10,100,10}),"Distant view accepted an irrelevant crop");
    require(albert::regionSufficient({-180,80,180,90},{-90,80,270,90}),"Polar panorama was reloaded for a longitude-only change");
    float reference=0;
    for(int fps:{30,60,144}) {
        float value=0;
        for(int frame=0;frame<fps;++frame) value=albert::animateValue(value,.25f,1.f/fps);
        require(std::abs(value-.25f)<.0006f,"Animation did not settle");
        if(reference) require(std::abs(value-reference)<.0006f,"Animation depends on frame rate");
        reference=value;
    }
    float value=albert::animateValue(0.f,.25f,.05f);
    const float reversed=albert::animateValue(value,0.f,.01f);
    require(reversed>0 && reversed<value,"Reversing a fade must preserve continuity");
    require(albert::animateValue(value,.25f,0.f)==value,"Zero elapsed time changed the animation");
    for(int i=0;i<100;++i) value=albert::animateValue(value,0.f,.01f);
    require(value==0,"A disabled effect must stop rendering after its fade");
    constexpr double radians=3.141592653589793/180.;
    // Independent WGS84 relation: a geodetic 45-degree surface point has a
    // sphere parameter of approximately 44.9037878494 degrees.
    require(std::abs(albert::parametricLatitude(45*radians)/radians-44.9037878494)<1e-8,"WGS84 texture latitude is misaligned");
    require(std::abs(albert::geodeticLatitude(44.9037878494*radians)/radians-45)<1e-8,"Inverse geographic latitude is misaligned");
    // An island smaller than the global simplification error must remain a
    // closed outline, and LOD must not oscillate inside its transition band.
    albert::BorderLines island({1,0,0, 1,.0001f,0, 1,.0001f,0, 1,0,.0001f, 1,0,.0001f, 1,0,0});
    int vertices=0;
    for(auto* child:island.children.front()->children) vertices+=dynamic_cast<Mesh*>(child)->geometry()->drawRange.count;
    require(vertices==18,"Global border simplification erased a small island");
    island.updateDetail(2.f);island.updateDetail(2.2f);
    require(island.children.back()->visible,"Border detail oscillated in the hysteresis band");
    island.updateDetail(2.4f);
    require(island.children.front()->visible && !island.children.back()->visible,"Distant borders kept the expensive detail level");
}
Image rgba(unsigned char value,unsigned char alpha=255) {
    return Image(std::vector<unsigned char>{value,value,value,alpha},1,1);
}
Image height(float meters) {return Image(std::vector<float>{meters,meters,meters,1.f},1,1);}
albert::DecodedAssets assets() {
    albert::DecodedAssets result;
    for(size_t i=0;i<5;++i) result.images[i]=rgba(i==1 || i==2 ? 0 : 255);
    result.images[5]=height(6000.f);
    result.oceans=rgba(255);result.moonHeight=height(0.f);
    return result;
}
std::shared_ptr<Mesh> stroke(std::shared_ptr<ShaderMaterial> material,const std::vector<Vector3>& points) {
    std::vector<float> positions,next,sides,times,nextTimes;
    constexpr int ends[]={0,1,0,0,1,1};constexpr float signs[]={-1,-1,1,1,-1,1};
    for(size_t segment=0;segment+1<points.size();++segment) for(int v=0;v<6;++v) {
        const int e=ends[v];const auto& p=points[segment+e];const auto& q=points[segment+1-e];
        positions.insert(positions.end(),{p.x,p.y,p.z});next.insert(next.end(),{q.x,q.y,q.z});
        sides.push_back(signs[v]*(e ? -1.f : 1.f));
        times.push_back(static_cast<float>(segment+e));nextTimes.push_back(static_cast<float>(segment+1-e));
    }
    auto geometry=BufferGeometry::create();
    geometry->setAttribute("position",FloatBufferAttribute::create(std::move(positions),3));
    geometry->setAttribute("nextPosition",FloatBufferAttribute::create(std::move(next),3));
    geometry->setAttribute("lineSide",FloatBufferAttribute::create(std::move(sides),1));
    geometry->setAttribute("orbitTime",FloatBufferAttribute::create(std::move(times),1));
    geometry->setAttribute("nextTime",FloatBufferAttribute::create(std::move(nextTimes),1));
    auto mesh=Mesh::create(geometry,material);mesh->frustumCulled=false;mesh->renderOrder=1;
    return mesh;
}
int cyanPixels(const std::vector<unsigned char>& pixels,int x0,int x1,int y0,int y1) {
    int count=0;
    for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
        const size_t at=3*(y*192+x);
        if(pixels[at+1]>pixels[at]+12 && pixels[at+2]>pixels[at]+12) ++count;
    }
    return count;
}
// The scenes through the WebGPU renderer (192x192, no tone mapping, no MSAA, RGB rows bottom-up).
struct Target {
    virtual ~Target()=default;
    virtual void render(Scene& scene,Camera& camera)=0;
    virtual std::vector<unsigned char> readRGBPixels()=0;
};
struct GpuTarget final:Target {
    albert::gpu::Context context;albert::gpu::SceneRenderer scene{context};WGPUTexture texture=nullptr;WGPUTextureView view=nullptr;
    GpuTarget(){albert::defineWebGpuPrograms();scene.toneMapping=false;scene.samples=1;
        WGPUTextureDescriptor d{};d.size={192,192,1};d.format=context.format();d.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;d.mipLevelCount=1;d.sampleCount=1;d.dimension=WGPUTextureDimension_2D;
        texture=wgpuDeviceCreateTexture(context.device(),&d);view=wgpuTextureCreateView(texture,nullptr);}
    ~GpuTarget() override{wgpuTextureViewRelease(view);wgpuTextureRelease(texture);}
    void render(Scene& s,Camera& c) override{
        WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(context.device(),nullptr);scene.render(s,c,view,192,192,e);
        WGPUCommandBuffer b=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(context.queue(),1,&b);scene.submitted();wgpuCommandBufferRelease(b);wgpuCommandEncoderRelease(e);
    }
    std::vector<unsigned char> readRGBPixels() override{
        constexpr unsigned row=192*4; // A multiple of 256 bytes, as copies require.
        WGPUBufferDescriptor bd{};bd.size=row*192;bd.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;WGPUBuffer b=wgpuDeviceCreateBuffer(context.device(),&bd);
        WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(context.device(),nullptr);WGPUTexelCopyTextureInfo src{};src.texture=texture;WGPUTexelCopyBufferInfo dst{};dst.buffer=b;dst.layout.bytesPerRow=row;dst.layout.rowsPerImage=192;
        const WGPUExtent3D size{192,192,1};wgpuCommandEncoderCopyTextureToBuffer(e,&src,&dst,&size);WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(context.queue(),1,&c);
        bool done=false;WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&done;cb.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};
        wgpuBufferMapAsync(b,WGPUMapMode_Read,0,bd.size,cb);while(!done)wgpuInstanceProcessEvents(context.instance());
        const auto* p=static_cast<const unsigned char*>(wgpuBufferGetConstMappedRange(b,0,bd.size));std::vector<unsigned char> out(192*192*3);const bool bgra=context.format()==WGPUTextureFormat_BGRA8Unorm;
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<192;++x){const auto* s=p+(191-y)*row+x*4;auto* d=&out[(y*192+x)*3];d[0]=bgra?s[2]:s[0];d[1]=s[1];d[2]=bgra?s[0]:s[2];}
        wgpuBufferUnmap(b);wgpuBufferRelease(b);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);return out;
    }
};
void graphics(Target& renderer) {
    auto scene=Scene::create();scene->background=Color(0x000000);
    auto camera=OrthographicCamera::create(-.6f,.6f,.6f,-.6f,.01f,300.f);
    camera->position.set(0,0,1.5f);camera->lookAt(Vector3());
    scene->add(AmbientLight::create(Color(0xffffff),1.f));
    albert::PlanetVisuals planets(*scene,assets());
    planets.earth->position.set(100,0,0);planets.sun->position.set(250,0,0);
    albert::VisualSettings settings;settings.atmosphere=settings.clouds=settings.nightLights=settings.shadows=false;settings.terrain=false; // The globe mesh and its patches: terrain nodes would join their children asynchronously.
    albert::RegionalImages region;region.body="moon";region.region={-135,-45,-45,45};
    region.day=rgba(255,128);region.height=height(-1200.f);
    planets.stageRegionalImages(std::move(region));
    for(int frame=0;planets.uploading() && frame<256;++frame) planets.updateUploads();
    require(!planets.uploading(),"Incremental terrain upload did not complete");
    auto material=albert::orbitMaterial(100.f);
    material->uniforms.at("viewport").setValue(Vector2(192,192));
    material->uniforms.at("present").setValue(2.f);
    auto behind=stroke(material,{{-.55f,0,-1.9f},{.55f,0,-1.9f}});
    scene->add(behind);
    planets.update(Vector3(0,0,1),camera->position,settings,1.f);
    renderer.render(*scene,*camera);auto pixels=renderer.readRGBPixels();
    require(cyanPixels(pixels,83,109,91,101)==0,"Orbit leaked through the regional lunar terrain");
    require(cyanPixels(pixels,15,40,93,100)>10,"Occlusion test hid the whole orbit");
    scene->remove(*behind);
    auto front=stroke(material,{{-.55f,0,-1.1f},{.55f,0,-1.1f}});scene->add(front);
    renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(cyanPixels(pixels,83,109,93,100)>20,"Orbit physically in front of the Moon was hidden");
    scene->remove(*front);planets.moon->visible=false;
    material->uniforms.at("present").setValue(-2.f);
    material->uniforms.at("dashDays").setValue(.0001f);
    scene->add(front);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    int darkest=255,brightest=0;
    for(int x=40;x<150;++x) {const int r=pixels[3*(96*192+x)];darkest=std::min(darkest,r);brightest=std::max(brightest,r);}
    require(darkest>40 && brightest-darkest<15,"Subpixel dashes must converge to a stable continuous line");
    scene->remove(*front);
    material->uniforms.at("present").setValue(1000.f);material->uniforms.at("referenceVisibility").setValue(1.f);
    scene->add(behind);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(pixels[3*(96*192+25)+2]>24,"Complete orbit guide disappeared outside the highlighted time span");
    planets.moon->visible=true;renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    const auto occludedGuide=pixels;material->uniforms.at("referenceVisibility").setValue(0.f);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    for(int y=91;y<101;++y)for(int x=83;x<109;++x)for(int c=0;c<3;++c)require(pixels[3*(y*192+x)+c]==occludedGuide[3*(y*192+x)+c],"Complete orbit guide leaked through the lunar terrain");
    material->uniforms.at("referenceVisibility").setValue(1.f);
    planets.moon->visible=false;material->uniforms.at("visibility").setValue(0.f);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(pixels[3*(96*192+25)+2]==0,"Hiding orbits retained the complete orbit guide");
    material->uniforms.at("visibility").setValue(1.f);material->uniforms.at("referenceVisibility").setValue(0.f);scene->remove(*behind);
    planets.earth->position.set(0,0,0);camera->position.set(0,0,1.5f);camera->lookAt(Vector3());
    albert::RegionalImages earthRegion;earthRegion.region={-135,-45,-45,45};
    earthRegion.day=rgba(32,128);earthRegion.height=height(6000.f);
    planets.setRegionalImages(std::move(earthRegion));
    planets.update(Vector3(0,0,1),camera->position,settings,1.f);
    material->uniforms.at("present").setValue(2.f);
    scene->add(behind);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(cyanPixels(pixels,83,109,91,101)==0,"Orbit leaked through the regional Earth terrain");
    auto earthFront=stroke(material,{{-.55f,0,-.4f},{.55f,0,-.4f}});
    scene->remove(*behind);scene->add(earthFront);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(cyanPixels(pixels,83,109,93,100)>20,"Orbit physically in front of Earth was hidden");
    scene->remove(*earthFront);
    std::vector<Vector3> points;
    for(int i=-100;i<=100;++i) {const float angle=i*.001f;points.emplace_back(std::sin(angle),0.f,std::cos(angle));}
    std::vector<float> borderPoints;
    for(size_t i=0;i+1<points.size();++i) for(const auto& p:{points[i],points[i+1]})
        borderPoints.insert(borderPoints.end(),{p.x,p.y,p.z});
    auto line=std::make_shared<albert::BorderLines>(borderPoints);line->updateDetail(1.5f);
    auto borders=line->style;borders->uniforms.at("viewport").setValue(Vector2(192,192));
    borders->uniforms.at("visibility").setValue(1.f);planets.updateBorderMaterial(*borders,camera->position);
    scene->add(line);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(cyanPixels(pixels,86,106,93,100)>10,"Borders disappeared beneath 6 km terrain");
    scene->remove(*line);
    for(auto& point:points) point.z=-point.z;
    scene->add(stroke(borders,points));renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(cyanPixels(pixels,86,106,93,100)==0,"Borders showed through the far hemisphere");
    const int plain=pixels[3*(96*192+96)];
    planets.setCloudDetail(rgba(255),{-135,-45,-45,45});settings.clouds=true;
    planets.update(Vector3(0,0,1),camera->position,settings,1.f);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    const int cloudy=pixels[3*(96*192+96)];require(cloudy>plain+40,"Live cloud detail shader did not render");
    planets.setCloudDetail(rgba(0),{-135,-45,-45,45});
    planets.update(Vector3(0,0,1),camera->position,settings,0.f);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(std::abs(int(pixels[3*(96*192+96)])-cloudy)<3,"Cloud replacement discarded the previous image before fading");
    settings.nightLights=true;planets.update(Vector3(0,0,1),camera->position,settings,1.f);
    renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(pixels[3*(96*192+96)]<cloudy-40,"Cloud detail transition did not settle");
    const auto earthChildren=planets.earth->children.size(),moonChildren=planets.moon->children.size();
    std::weak_ptr<BufferGeometry> oldEarthDetail=planets.earth->children.back()->as<Mesh>()->geometry(),oldMoonDetail=planets.moon->children.back()->as<Mesh>()->geometry();
    require(!planets.releaseDistantDetails("sun",camera->position,false,1.f),"Detail eviction ignored its grace interval");
    require(planets.releaseDistantDetails("sun",camera->position,false,3.f),"Distant Earth/Moon details stayed resident");
    require(planets.earth->children.size()+1==earthChildren&&planets.moon->children.size()+1==moonChildren&&oldEarthDetail.expired()&&oldMoonDetail.expired(),"Evicted terrain retained mesh resources");
    settings.clouds=false;planets.update(Vector3(0,0,1),camera->position,settings,1.f);planets.updateBorderMaterial(*borders,camera->position);
    scene->add(behind);renderer.render(*scene,*camera);pixels=renderer.readRGBPixels();
    require(cyanPixels(pixels,83,109,91,101)==0&&pixels[3*(96*192+96)]>0,"Eviction left a hole in the global Earth surface");scene->remove(*behind);
    albert::RegionalImages reloaded;reloaded.day=rgba(64,128);reloaded.height=height(1000);planets.setRegionalImages(std::move(reloaded));
    require(planets.earth->children.size()==earthChildren,"Earth details did not reload after eviction");
    auto generic=[&](double origin){
        albert::core::Scene registry;albert::core::Object object;object.id="box";object.name="Box";
        object.appearance.shape=albert::core::Shape::Box;object.appearance.sizeMeters={.5,.5,.5};object.samples.emplace_back(100,albert::core::Vector{origin+.25,0,0});registry.create(object);
        auto world=Scene::create();world->background=Color(0x000000);world->add(AmbientLight::create(Color(0xffffff),1.f));
        albert::SceneObjects objects(*world);objects.sync(registry,100,{origin,0,0});
        auto view=OrthographicCamera::create(-2e-7f,2e-7f,2e-7f,-2e-7f,1e-9f,1e-3f);view->position.z=1e-5f;view->lookAt(Vector3());
        renderer.render(*world,*view);auto result=renderer.readRGBPixels();
        object.id="second";object.samples.front().positionMeters.x+=1000;registry.create(object);objects.sync(registry,100,{origin,0,0});
        require(objects.nodes().at("box")->geometry()==objects.nodes().at("second")->geometry(),"Identical primitives duplicated GPU buffers");
        registry.remove("second");objects.sync(registry,100,{origin,0,0});renderer.render(*world,*view);
        require(renderer.readRGBPixels()==result,"Removing a shared primitive damaged another object");
        registry.visible("box",false);objects.sync(registry,100,{origin,0,0});renderer.render(*world,*view);
        const auto hidden=renderer.readRGBPixels();require(std::all_of(hidden.begin(),hidden.end(),[](auto value){return value==0;}),"Hidden external object still rendered");
        return result;
    };
    const auto near=generic(0),far=generic(1e11);
    require(near==far && std::any_of(near.begin(),near.end(),[](auto value){return value>20;}),"Camera-relative rendering lost sub-metre geometry at astronomical distances");
    auto modelWorld=Scene::create();modelWorld->background=Color(0x000000);modelWorld->add(AmbientLight::create(Color(0xffffff),2.f));
    albert::core::Scene modelRegistry;modelRegistry.installModel(albert::core::decodeModel("triangle","Fixture",model_fixture::glb()));
    albert::core::Object vehicle;vehicle.id="model";vehicle.name="Model";vehicle.appearance.shape=albert::core::Shape::Model;vehicle.appearance.modelId="triangle";vehicle.samples={{100,{1e11,0,0}}};modelRegistry.create(vehicle);
    albert::SceneObjects modelObjects(*modelWorld);modelObjects.sync(modelRegistry,100,{1e11,0,0});
    auto modelCamera=OrthographicCamera::create(-1e-6f,1e-6f,1e-6f,-1e-6f,1e-9f,1e-3f);modelCamera->position.set(0,1e-5f,0);modelCamera->up.set(0,0,-1);modelCamera->lookAt(Vector3(0,0,-3.f/6378137.f));renderer.render(*modelWorld,*modelCamera);pixels=renderer.readRGBPixels();
    require(std::any_of(pixels.begin(),pixels.end(),[](auto p){return p>20;}),"Imported model hierarchy/material did not render");
    auto secondVehicle=vehicle;secondVehicle.id="second-model";secondVehicle.samples.front().positionMeters.x+=1000;modelRegistry.create(secondVehicle);modelObjects.sync(modelRegistry,100,{1e11,0,0});require(modelObjects.nodes().at("model")->children.front()->as<Mesh>()->geometry()==modelObjects.nodes().at("second-model")->children.front()->as<Mesh>()->geometry(),"Model instances duplicated GPU geometry");auto* firstAid=modelObjects.nodes().at("model")->children.front()->as<Mesh>()->material()->as<ShaderMaterial>();auto* secondAid=modelObjects.nodes().at("second-model")->children.front()->as<Mesh>()->material()->as<ShaderMaterial>();
    require(firstAid!=secondAid,"Model instances share mutable auxiliary materials");
    albert::AuxiliaryLights modelAid;modelAid["model"].enabled=true;modelObjects.updateLighting(*modelCamera,Vector3(0,-1,0),modelAid,1.f);
    require(firstAid->uniforms.at("auxiliaryStrength").value<float>()>.24f&&secondAid->uniforms.at("auxiliaryStrength").value<float>()==0,"Auxiliary light leaked between model instances");
    modelAid["model"].enabled=false;modelObjects.updateLighting(*modelCamera,Vector3(0,-1,0),modelAid,1.f);
    require(firstAid->uniforms.at("auxiliaryStrength").value<float>()==0,"Model auxiliary fade out failed");
    modelRegistry.remove("second-model");modelObjects.sync(modelRegistry,100,{1e11,0,0});
    auto previous=modelObjects.nodes().at("model");modelRegistry.remove("model");vehicle.appearance.shape=albert::core::Shape::Box;vehicle.appearance.modelId.clear();modelRegistry.create(vehicle);modelObjects.sync(modelRegistry,100,{1e11,0,0});require(modelObjects.nodes().at("model")!=previous,"Recreated ID retained obsolete mesh/resources");
    modelRegistry.remove("model");modelRegistry.removeModel("triangle");modelObjects.sync(modelRegistry,100,{1e11,0,0});renderer.render(*modelWorld,*modelCamera);pixels=renderer.readRGBPixels();require(std::all_of(pixels.begin(),pixels.end(),[](auto p){return p==0;}),"Removed model retained visible GPU geometry");
    auto annotation=[&](double origin){
        auto world=Scene::create();world->background=Color(0x000000);world->add(AmbientLight::create(Color(0xffffff),1.f));
        albert::core::Scene registry;albert::core::Object box;box.id="anchor";box.name="Anchor";box.appearance.shape=albert::core::Shape::Box;box.appearance.sizeMeters={.5,.5,.5};box.appearance.color=0x224466;box.samples={{100,{origin,0,0}}};registry.create(box);
        albert::SceneObjects objects(*world);objects.sync(registry,100,{origin,0,0});albert::VectorLayers layers(*world);
        auto camera=OrthographicCamera::create(-2e-7f,2e-7f,2e-7f,-2e-7f,1e-9f,1e-3f);camera->position.z=1e-5f;camera->lookAt(Vector3());renderer.render(*world,*camera);const auto original=renderer.readRGBPixels();
        albert::core::Layer point;point.id="marker";point.name="Marker";point.color=0xff0000;point.widthPixels=12;std::vector<albert::core::Vector> coordinates{{origin,.4,0}};registry.createLayer(albert::core::prepareLayer(point,coordinates));
        auto update=[&](float dt){layers.update(registry,objects,{origin,0,0},Vector2(192,192),camera->nearPlane,dt,[](ShaderMaterial&){});renderer.render(*world,*camera);return renderer.readRGBPixels();};
        require(update(1)==original,"Spatial marker rendered through an opaque object");registry.removeLayer(point.id);update(1);coordinates[0].y=-.4;registry.createLayer(albert::core::prepareLayer(point,coordinates));const auto marked=update(1);
        require(marked[3*(96*192+96)]>original[3*(96*192+96)]+50,"Marker in front failed to render");registry.layerVisible(point.id,false);require(update(0)==marked,"Layer hide did not preserve its transition");require(update(1)==original,"Layer hide did not settle");registry.removeLayer(point.id);require(update(1)==original,"Removed layer damaged the scene");return marked;
    };
    require(annotation(0)==annotation(1e11),"Spatial layers lost precision at astronomical distances");
    auto surfaceWorld=Scene::create();surfaceWorld->background=Color(0x000000);albert::PlanetVisuals surfacePlanets(*surfaceWorld,assets());surfacePlanets.moon->visible=false;surfacePlanets.sun->visible=false;
    auto surfaceCamera=OrthographicCamera::create(-.04f,.04f,.04f,-.04f,.01f,10.f);surfaceCamera->position.set(0,0,3);surfaceCamera->lookAt(Vector3());albert::VisualSettings surfaceSettings;surfaceSettings.atmosphere=surfaceSettings.clouds=surfaceSettings.nightLights=false;surfacePlanets.update(Vector3(0,0,1),surfaceCamera->position,surfaceSettings,1.f);
    albert::core::Scene surfaceRegistry;albert::SceneObjects surfaceObjects(*surfaceWorld);surfaceObjects.bind("earth",surfacePlanets.earth);albert::VectorLayers surfaceLayers(*surfaceWorld);
    albert::core::Layer surfaceLine;surfaceLine.id="surface-line";surfaceLine.name="Surface line";surfaceLine.kind=albert::core::LayerKind::Polyline;surfaceLine.frame=albert::core::LayerFrame::EarthSurface;surfaceLine.widthPixels=3;
    std::vector<albert::core::Geographic> coordinates{{-91,0},{-89,0}};surfaceRegistry.createLayer(albert::core::prepareLayer(surfaceLine,{},coordinates));
    auto surfaceDraw=[&]{surfaceLayers.update(surfaceRegistry,surfaceObjects,{},Vector2(192,192),surfaceCamera->nearPlane,1.f,[&](ShaderMaterial& material){surfacePlanets.updateBorderMaterial(material,surfaceCamera->position);});renderer.render(*surfaceWorld,*surfaceCamera);return renderer.readRGBPixels();};
    require(cyanPixels(surfaceDraw(),30,162,85,107)>60,"Geographic line did not follow 6 km terrain");surfaceRegistry.removeLayer(surfaceLine.id);surfaceDraw();coordinates={{89,0},{91,0}};surfaceRegistry.createLayer(albert::core::prepareLayer(surfaceLine,{},coordinates));require(cyanPixels(surfaceDraw(),30,162,85,107)==0,"Far-side surface line leaked through Earth");
    auto solidScene=Scene::create();solidScene->background=Color(0x000000);
    auto solidCamera=PerspectiveCamera::create(45,1,.001f,30000.f);solidCamera->position.set(0,0,1.5f);solidCamera->lookAt(Vector3());
    std::vector<albert::SurfaceAssets> surfaceAssets;surfaceAssets.push_back({"mercury",rgba(120),height(0)});
    albert::SolidBodies solids(*solidScene,std::move(surfaceAssets));
    solids.update(*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,1.f);renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    const int lit=pixels[3*(96*192+96)];require(lit>20,"Solid body solar shader failed to render");
    solids.update(*solidCamera,Vector3(0,0,-23455),2451545.,"mercury",true,1.f);renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    require(pixels[3*(96*192+96)]<3,"Solid body's night hemisphere is lit without solar incidence");
    albert::AuxiliaryLights auxiliary;auxiliary["mercury"].enabled=true;
    solids.update(*solidCamera,Vector3(0,0,-23455),2451545.,"mercury",true,0.f,auxiliary);renderer.render(*solidScene,*solidCamera);require(renderer.readRGBPixels()[3*(96*192+96)]<3,"Auxiliary light appeared before its fade");
    solids.update(*solidCamera,Vector3(0,0,-23455),2451545.,"mercury",true,1.f,auxiliary);renderer.render(*solidScene,*solidCamera);require(renderer.readRGBPixels()[3*(96*192+96)]>15,"Solid auxiliary light did not reveal the night hemisphere");
    solids.update(*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,1.f,auxiliary);renderer.render(*solidScene,*solidCamera);require(std::abs(int(renderer.readRGBPixels()[3*(96*192+96)])-lit)<3,"Auxiliary light changed the day hemisphere");
    auxiliary["mercury"].enabled=false;solids.update(*solidCamera,Vector3(0,0,-23455),2451545.,"mercury",true,1.f,auxiliary);renderer.render(*solidScene,*solidCamera);require(renderer.readRGBPixels()[3*(96*192+96)]<3,"Solid auxiliary light remained enabled after fade out");
    {   // Jupiter's dusty rings: invisible at physical brightness, visible when enhanced, and far
        // brighter seen against the Sun (forward scattering) than with it behind the camera.
        auto ringScene=Scene::create();ringScene->background=Color(0x000000);
        std::vector<albert::SurfaceAssets> ringAssets;ringAssets.push_back({"jupiter",rgba(120),height(0)});
        albert::SolidBodies ringed(*ringScene,std::move(ringAssets));require(ringed.faintRings("jupiter")&&!ringed.faintRings("missing"),"Jupiter lost its faint rings");
        const float earth=static_cast<float>(albert::core::earthRadiusMeters),r=static_cast<float>(albert::core::surfaceRadius("jupiter"))/earth;
        auto view=PerspectiveCamera::create(20,1,r*.01f,r*1000);
        auto shot=[&](float side,bool enhanced){
            ringed.enhanceRings("jupiter",enhanced);
            ringed.update(*view,Vector3(0,0,20000),2451545.,"jupiter",false,1.f);auto node=ringed.nodes().at("jupiter");node->updateMatrixWorld();
            Vector3 normal(0,1,0);normal.transformDirection(*node->matrixWorld);Vector3 point(125750000.f/earth,0,0);point.applyMatrix4(*node->matrixWorld);
            view->position.copy(point).add(normal.clone().multiplyScalar(r*2));view->lookAt(point);view->updateMatrixWorld();
            const auto sun=point.clone().add(normal.clone().multiplyScalar(side*20000.f));
            for(int i=0;i<4;++i)ringed.update(*view,sun,2451545.,"jupiter",false,10.f);
            renderer.render(*ringScene,*view);return int(renderer.readRGBPixels()[3*(96*192+96)]);
        };
        const int physical=shot(1,false),back=shot(1,true),forward=shot(-1,true);
        require(physical<=2&&back>physical+5&&forward>back+20,"Faint ring brightness, enhancement or forward scattering is wrong");
        ringed.enhanceRings("jupiter",false);
    }
    {   // Uranus' epsilon ring (optical depth ~1) dims a bright background seen through it.
        auto ringScene=Scene::create();ringScene->background=Color(0x000000);
        std::vector<albert::SurfaceAssets> ringAssets;ringAssets.push_back({"uranus",rgba(120),height(0)});
        albert::SolidBodies ringed(*ringScene,std::move(ringAssets));
        const float earth=static_cast<float>(albert::core::earthRadiusMeters),r=static_cast<float>(albert::core::surfaceRadius("uranus"))/earth;
        auto view=PerspectiveCamera::create(20,1,r*.001f,r*1000);
        ringed.update(*view,Vector3(0,0,20000),2451545.,"uranus",false,1.f);auto node=ringed.nodes().at("uranus");node->updateMatrixWorld();
        Vector3 normal(0,1,0);normal.transformDirection(*node->matrixWorld);Vector3 point(51149000.f/earth,0,0);point.applyMatrix4(*node->matrixWorld);
        view->position.copy(point).add(normal.clone().multiplyScalar(r*.2f));view->lookAt(point);view->updateMatrixWorld();
        auto backdrop=Mesh::create(PlaneGeometry::create(r,r),MeshBasicMaterial::create());backdrop->position.copy(point).sub(normal.clone().multiplyScalar(r*.1f));backdrop->lookAt(view->position);ringScene->add(backdrop);
        auto centre=[&](bool ring){for(auto& child:node->children)child->visible=ring;
            for(int i=0;i<3;++i)ringed.update(*view,point.clone().add(normal.clone().multiplyScalar(20000.f)),2451545.,"uranus",false,1.f);
            renderer.render(*ringScene,*view);return int(renderer.readRGBPixels()[3*(96*192+96)]);};
        const int clear=centre(false),dimmed=centre(true);
        require(clear>100&&dimmed<clear*.6,"Dense ring did not attenuate the background");
        backdrop->geometry()->dispose();backdrop->material()->dispose();
    }
    albert::RegionalImages solidRegion;solidRegion.body="mercury";solidRegion.day=rgba(230);solidRegion.height=height(1000);
    albert::prepareRegionalImages(solidRegion);solids.stage(std::move(solidRegion));
    for(int i=0;solids.uploading()&&i<256;++i)solids.update(*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,0.f);
    require(!solids.uploading(),"Solid regional texture upload did not finish");renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    require(std::abs(int(pixels[3*(96*192+96)])-lit)<4,"Solid detail appeared without crossfade");
    solids.update(*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,1.f);renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    require(pixels[3*(96*192+96)]>lit+30,"Solid detail crossfade did not settle");
    solids.update(*solidCamera,Vector3(0,0,23455),2451545.,"earth",true,4.f);renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    require(std::abs(int(pixels[3*(96*192+96)])-lit)<4,"Solid detail eviction damaged the global surface");
    { // Review: Mars's terrain carries the regional imagery it hides the patch for (here the whole globe).
        auto marsScene=Scene::create();std::vector<albert::SurfaceAssets> marsAssets;marsAssets.push_back({"mars",rgba(120),height(0)});
        albert::SolidBodies marsBodies(*marsScene,std::move(marsAssets));const float r=static_cast<float>(albert::core::surfaceRadius("mars")/albert::core::earthRadiusMeters);
        auto marsCamera=PerspectiveCamera::create(45,1,1e-7f,10.f);marsCamera->position.set(0,0,r*1.002f);marsCamera->lookAt(Vector3());
        albert::RegionalImages region;region.body="mars";region.day=rgba(230);region.height=height(0);albert::prepareRegionalImages(region);marsBodies.stage(std::move(region));
        const auto* terrain=marsBodies.terrain("mars");require(terrain,"Mars terrain");
        for(int i=0;i<20000&&(marsBodies.uploading()||terrain->drawn()==0||terrain->pending()>0);++i){marsBodies.update(*marsCamera,Vector3(0,0,23455),2451545.,"mars",true,0.f);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        marsBodies.update(*marsCamera,Vector3(0,0,23455),2451545.,"mars",true,0.f);
        require(terrain->drawn()>0&&terrain->detailed()==terrain->drawn(),"Mars terrain nodes did not take the regional imagery");
    }
    require(solids.takeEviction()&&!solids.takeEviction(),"Regional loader was not notified of eviction exactly once");
    // Local moons share surface LOD/lighting while retaining their local registry
    // ownership and category; their mesh radius must not be scaled twice.
    for(const auto& definition:albert::core::surfaceBodies()){
        const std::string id(definition.id);if(!albert::core::localOrbit(id))continue;
        auto world=Scene::create();world->background=Color(0x000000);
        std::vector<albert::SurfaceAssets> maps;maps.push_back({id,rgba(140),height(0)});
        albert::SolidBodies moons(*world,std::move(maps));albert::core::Scene registry;
        registry.include(*albert::core::localOrbit(id));albert::SceneObjects objects(*world);objects.bind(id,moons.nodes().at(id));
        objects.sync(registry,0,{});const float radius=static_cast<float>(albert::core::surfaceRadius(id)/albert::core::earthRadiusMeters);
        // Near plane scaled to the body: 67P's catalogue radius is only 1.65 km.
        auto camera=PerspectiveCamera::create(45,1,radius*.1f,30000.f);camera->position.set(0,0,radius*4);camera->lookAt(Vector3());
        moons.update(*camera,Vector3(0,0,23455),2451545.,id,true,1.f);renderer.render(*world,*camera);
        require(renderer.readRGBPixels()[3*(96*192+96)]>20,"Local moon surface did not render");
        if(moons.hazy(id)){
            // Visible haze hides the grey test map behind its orange colour; the
            // surface view fades it out and shows the map again.
            const auto hazy=renderer.readRGBPixels();require(hazy[3*(96*192+96)]>2*hazy[3*(96*192+96)+2]+10,"Visible haze did not cover the surface");
            moons.showSurface(id,true);for(int i=0;i<40;++i)moons.update(*camera,Vector3(0,0,23455),2451545.,id,true,.05f);renderer.render(*world,*camera);
            const auto surface=renderer.readRGBPixels();require(std::abs(int(surface[3*(96*192+96)])-int(surface[3*(96*192+96)+2]))<12&&moons.surfaceView(id),"Surface view did not reveal the map");
            moons.showSurface(id,false);
        }
        Raycaster ray;ray.setFromCamera(Vector2(0,0),*camera);require(objects.pick(registry,ray)==id,"Textured local moon cannot be picked");
        auto node=moons.nodes().at(id);node->geometry()->computeBoundingSphere();require(std::abs(node->geometry()->boundingSphere->radius-radius)<1e-5f,"Moon radius scaled twice");
        if(definition.axesMeters[0]>0){const auto& axes=definition.axesMeters;const double r=definition.radiusMeters;
            require(std::abs(node->scale.x-axes[0]/r)<1e-6&&std::abs(node->scale.y-axes[2]/r)<1e-6&&std::abs(node->scale.z-axes[1]/r)<1e-6,"IAU axes mapped incorrectly into render coordinates");}
        albert::RegionalImages detail;detail.body=id;detail.day=rgba(230);detail.height=height(0);albert::prepareRegionalImages(detail);moons.stage(std::move(detail));
        for(int i=0;moons.uploading()&&i<256;++i)moons.update(*camera,Vector3(0,0,23455),2451545.,id,true,0.f);
        require(!moons.uploading(),"Moon detail upload did not finish");moons.update(*camera,Vector3(0,0,23455),2451545.,id,true,1.f);
        moons.update(*camera,Vector3(0,0,23455),2451545.,"earth",true,4.f);require(moons.takeEviction(),"Moon detail stayed resident after focus changed");
    }
    for(const std::string id:{"phobos","deimos","mimas","phoebe","hyperion","proteus","67p","halley"}){
        auto world=Scene::create();world->background=Color(0x000000);auto shape=albert::decodeShape(albert::visualAssetPack()->payload("v1/"+id+"/shape.bin"));auto geometry=shape.geometry;
        std::vector<albert::SurfaceAssets> maps;maps.push_back({id,rgba(140),height(0),{},std::move(shape)});albert::SolidBodies measured(*world,std::move(maps));
        albert::core::Scene registry;registry.include(*albert::core::localOrbit(id));albert::SceneObjects objects(*world);objects.bind(id,measured.nodes().at(id),measured.picker(id));objects.sync(registry,0,{});
        // Within 2.4 radii, as the app keeps a body's detail resident (farther it is released after 3 s of
        // wall time, which a slow adapter spent before the fade: WARP lost Mimas's detail).
        auto camera=PerspectiveCamera::create(45,1,.00001f,30000.f);camera->position.set(0,0,float(albert::core::surfaceRadius(id)*2.2/albert::core::earthRadiusMeters));camera->lookAt(Vector3());
        measured.update(*camera,Vector3(0,0,23455),2451545.,id,true,1.f);renderer.render(*world,*camera);const auto before=renderer.readRGBPixels();require(before[3*(96*192+96)]>10,"Observed shape failed to render");
        Raycaster ray;ray.setFromCamera(Vector2(0,0),*camera);require(objects.pick(registry,ray)==id,"Measured BVH picking failed");
        albert::RegionalImages detail;detail.body=id;detail.day=rgba(230);detail.textureOnly=true;albert::prepareRegionalImages(detail);require(!detail.geometry&&!detail.surfaceMask,"Measured imagery allocated a replacement sphere or mask");measured.stage(std::move(detail));
        for(int i=0;measured.uploading()&&i<256;++i)measured.update(*camera,Vector3(0,0,23455),2451545.,id,true,0.f);
        require(!measured.uploading(),"Observed shape detail upload stalled");renderer.render(*world,*camera);const auto start=renderer.readRGBPixels();require(std::abs(int(start[3*(96*192+96)])-before[3*(96*192+96)])<4,"Measured detail appeared without fade");
        measured.update(*camera,Vector3(0,0,23455),2451545.,id,true,1.f);renderer.render(*world,*camera);const auto after=renderer.readRGBPixels();long increase=0;for(size_t i=0;i<after.size();i+=3)increase+=int(after[i])-int(before[i]);if(increase<=500){const std::size_t c=3*(96*192+96);std::fprintf(stderr,"%s: brightness increase %ld; centre %d -> %d -> %d; WebGPU errors %u\n",id.c_str(),increase,int(before[c]),int(start[c]),int(after[c]),albert::gpu::errors());}
        require(increase>500,"Measured texture detail fade did not settle");
        require(measured.nodes().at(id)->geometry()==geometry&&measured.nodes().at(id)->children.empty(),"Texture LOD replaced observed plate geometry");
        measured.update(*camera,Vector3(0,0,23455),2451545.,"earth",true,4.f);require(measured.takeEviction(),"Measured texture detail was not evicted");renderer.render(*world,*camera);const auto released=renderer.readRGBPixels();require(std::abs(int(released[3*(96*192+96)])-before[3*(96*192+96)])<4,"Shape detail eviction damaged base imagery");
    }
    albert::RegionalImages reload;reload.body="mercury";reload.day=rgba(230);reload.height=height(1000);albert::prepareRegionalImages(reload);solids.stage(std::move(reload));
    for(int i=0;solids.uploading()&&i<256;++i)solids.update(*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,0.f);
    solids.update(*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,4.f);
    require(solids.takeEviction(),"Zooming out on the same planet retained high-resolution terrain");
    {
        auto world=Scene::create();world->background=Color(0x000000);world->add(AmbientLight::create(Color(0xffffff),.2f));
        auto light=DirectionalLight::create(Color(0xffffff),3.f);light->position.set(0,0,-100);world->add(light);
        albert::PlanetVisuals bodies(*world,assets());bodies.earth->position.set(0,0,0);bodies.moon->position.set(0,0,60);bodies.sun->position.set(0,0,-23455);
        light->setTarget(*bodies.moon);
        auto view=PerspectiveCamera::create(60,1,.001f,100000);view->position.set(0,0,58.9f);view->lookAt(bodies.moon->position);
        albert::VisualSettings settings;settings.atmosphere=settings.clouds=settings.nightLights=false;settings.terrain=false;
        bodies.update(Vector3(0,0,-1),view->position,settings,1.f);
        albert::EclipseVisuals eclipses(bodies.earth,bodies.moon,bodies.sun);eclipses.update(true);renderer.render(*world,*view);auto total=renderer.readRGBPixels();
        const size_t center=3*(96*192+96);
        require(eclipses.lunarActive()&&total[center]>total[center+1]*2&&total[center]>5,"Lunar eclipse did not render filtered red light");
        eclipses.update(false);renderer.render(*world,*view);auto ordinary=renderer.readRGBPixels();
        require(!eclipses.lunarActive()&&ordinary[center+1]>total[center+1]*2,"Disabling shadows did not restore direct lunar light");
        bodies.earth->position.x=5;eclipses.update(true);renderer.render(*world,*view);
        require(!eclipses.lunarActive(),"A missed alignment retained the lunar eclipse");
        bodies.earth->position.set(0,0,0);bodies.moon->position.set(0,0,55);bodies.sun->position.set(0,0,23455);
        light->position.set(0,0,100);light->setTarget(*bodies.earth);view->position.set(0,0,3);view->lookAt(Vector3());
        bodies.update(Vector3(0,0,1),view->position,settings,1.f);eclipses.update(true);renderer.render(*world,*view);auto solar=renderer.readRGBPixels();
        eclipses.update(false);renderer.render(*world,*view);auto sunlit=renderer.readRGBPixels();
        require(solar[center]<sunlit[center]/3,"Solar eclipse did not darken the terrestrial umbra");
        world->remove(*bodies.earth);world->remove(*bodies.moon);world->remove(*bodies.sun);
        auto rock=albert::makeArtisticRock(*albert::core::localOrbit("67p"));rock->scale.setScalar(1);world->add(rock);light->setTarget(*rock);view->position.set(0,0,-2.5f);view->lookAt(Vector3());
        renderer.render(*world,*view);pixels=renderer.readRGBPixels();
        require(std::count_if(pixels.begin(),pixels.end(),[](auto value){return value>20;})>300,"Rock PBR surface shader did not render");
    }
    {   // 67P at its 2021 perihelion, the Sun along +x: coma seen from inside and from
        // outside its shell, physical and stretched, and the dust tail antisunward.
        auto world=Scene::create();world->background=Color(0x000000);albert::CometVisuals comets(*world);
        const double jd=2459520.5;const auto state=albert::core::heliocentricState(*albert::core::localOrbit("67p"))(jd);
        auto graphical=[](albert::core::Vector v){return Vector3(static_cast<float>(v.x),static_cast<float>(v.z),static_cast<float>(-v.y)).normalize();};
        const Vector3 sun=graphical(state.position*-1.).multiplyScalar(23455.f); // Real direction of the Sun from 67P.
        const Vector3 pole=graphical({state.position.y*state.velocity.z-state.position.z*state.velocity.y,state.position.z*state.velocity.x-state.position.x*state.velocity.z,state.position.x*state.velocity.y-state.position.y*state.velocity.x});
        const std::map<std::string,Vector3> nuclei{{"67p",Vector3()}};
        auto view=PerspectiveCamera::create(60,1,1e-6f,1e5f);
        const std::map<std::string,albert::core::StateVector> states{{"67p",state}};
        auto shot=[&](Vector3 eye,Vector3 at,float seconds){view->position.copy(eye);view->lookAt(at);view->updateMatrixWorld();
            comets.update(*view,sun,nuclei,states,jd,seconds);renderer.render(*world,*view);return renderer.readRGBPixels();};
        auto luminance=[](const auto& p,int x,int y){const auto i=3*(y*192+x);return p[i]+p[i+1]+p[i+2];};
        const Vector3 inside(0,0,.002f),outside(0,0,1.f);
        auto physical=shot(inside,Vector3(),.1f);
        require(comets.afrho("67p")>10&&luminance(physical,96+40,96)<9,"Physical coma not faint near the nucleus");
        comets.enhance("67p",true);for(int i=0;i<50;++i)shot(inside,Vector3(),1.f);
        auto near=shot(inside,Vector3(),.1f);require(luminance(near,96+40,96)>60,"Stretched coma missing from inside its shell");
        auto far=shot(outside,Vector3(),.1f);require(luminance(far,96+3,96)>60&&luminance(far,96+3,96)>luminance(far,96+60,96),"Stretched coma missing or not centred from outside");
        // Seen along the orbit normal; pixels split by the projected antisolar direction.
        auto tail=shot(Vector3(pole).multiplyScalar(3000.f),Vector3(),.1f);Vector3 away=Vector3(sun).multiplyScalar(-.01f);away.project(*view);
        const float ax=away.x,ay=away.y;int anti=0,sunward=0;
        for(int y=0;y<192;++y)for(int x=0;x<192;++x){const float dx=(x-95.5f)/96.f,dy=(y-95.5f)/96.f; // Rows from the bottom (glReadPixels).if(dx*dx+dy*dy<.01f)continue;
            (dx*ax+dy*ay>0?anti:sunward)+=luminance(tail,x,y);}
        require(anti>4*sunward+300,"Dust tail not antisunward");
        // Same date, another state (as when a table replaces the elements): the grains
        // must follow it. Reversed motion bends the tail the other way.
        auto reversed=state;reversed.velocity=reversed.velocity*-1.;
        const std::map<std::string,albert::core::StateVector> replaced{{"67p",reversed}};
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // Past the 10 Hz recompute limit.
        view->position.copy(Vector3(pole).multiplyScalar(3000.f));view->lookAt(Vector3());view->updateMatrixWorld();
        comets.update(*view,sun,nuclei,replaced,jd,.1f);renderer.render(*world,*view);const auto bent=renderer.readRGBPixels();
        long changed=0;for(int y=0;y<192;++y)for(int x=0;x<192;++x){const float dx=(x-95.5f)/96.f,dy=(y-95.5f)/96.f;if(dx*dx+dy*dy<.01f)continue;
            changed+=std::abs(luminance(bent,x,y)-luminance(tail,x,y));}
        require(changed>std::max(50,anti/4),"Dust tail kept the previous state at the same date");

    }
    {   // CO+ ion tail of Halley at the Giotto encounter (67P's CO is too scarce to show):
        // blue light along the aberrated antisolar axis, seen across the orbit.
        auto world=Scene::create();world->background=Color(0x000000);albert::CometVisuals comets(*world);comets.enhance("halley",true);
        const double jd=2446503.5;const auto state=albert::core::heliocentricState(*albert::core::localOrbit("halley"))(jd);
        auto graphical=[](albert::core::Vector v){return Vector3(static_cast<float>(v.x),static_cast<float>(v.z),static_cast<float>(-v.y)).normalize();};
        const Vector3 sun=graphical(state.position*-1.).multiplyScalar(23455.f*.89f);
        const Vector3 pole=graphical({state.position.y*state.velocity.z-state.position.z*state.velocity.y,state.position.z*state.velocity.x-state.position.x*state.velocity.z,state.position.x*state.velocity.y-state.position.y*state.velocity.x});
        auto view=PerspectiveCamera::create(60,1,1e-3f,1e6f);view->position.copy(Vector3(pole).multiplyScalar(10000.f));view->lookAt(Vector3());view->updateMatrixWorld();
        for(int i=0;i<60;++i)comets.update(*view,sun,{{"halley",Vector3()}},{{"halley",state}},jd,1.f);
        renderer.render(*world,*view);const auto pixels=renderer.readRGBPixels();
        Vector3 far=graphical(albert::core::ionTailAxis(state)).multiplyScalar(4000.f);far.project(*view);
        int blue=0;for(int step=8;step<=80;step+=4){const float t=step/96.f,length=std::hypot(far.x,far.y);
            const int x=static_cast<int>(95.5f+96.f*far.x/length*t),y=static_cast<int>(95.5f+96.f*far.y/length*t); // Rows from the bottom.
            if(x<0||y<0||x>=192||y>=192)continue;const auto i=3*(y*192+x);if(pixels[i+2]>pixels[i]+4&&pixels[i+2]>=pixels[i+1])++blue;}
        require(blue>=5,"Ion tail missing along its axis");
        // Exactly along the axis, behind the tail: the end-on disc keeps the gas visible.
        view->position.copy(graphical(albert::core::ionTailAxis(state)).multiplyScalar(10000.f));view->lookAt(Vector3());view->updateMatrixWorld();
        for(int i=0;i<20;++i)comets.update(*view,sun,{{"halley",Vector3()}},{{"halley",state}},jd,1.f);
        renderer.render(*world,*view);const auto endOn=renderer.readRGBPixels();int core=0;
        for(int y=86;y<106;++y)for(int x=86;x<106;++x){const auto i=3*(y*192+x);if(endOn[i+2]>endOn[i]+4&&endOn[i+2]>=endOn[i+1])++core;}
        require(core>=40,"Ion tail vanished when seen along its axis");
    }
    std::cout<<"Graphics OK: incremental upload, terrain occlusion, paths, clouds, objects, artistic rocks, lunar eclipse, comet dust and ions\n";
}
}
// Revolution period of a sampled path and the folded highlight time.
void orbitPaths() {
    constexpr double pi=3.14159265358979323846;
    // Tilted plane: x' = x, y' = y cos i, z' = y sin i.
    const double tilt=.4;auto plane=[&](double day,double x,double y){return albert::PathPoint{day,x,y*std::cos(tilt),y*std::sin(tilt)};};
    const std::array<double,3> normal{0,-std::sin(tilt),std::cos(tilt)};
    std::vector<albert::PathPoint> circle;for(int i=0;i<=120;++i){const double day=-6+.1*i,angle=2*pi*day/10+1;circle.push_back(plane(day,std::cos(angle),std::sin(angle)));}
    require(std::abs(albert::revolutionDays(circle,normal)-10)<1e-9,"Circular revolution period");
    require(albert::revolutionDays(std::span(circle).first(90),normal)==0,"Less than one turn reported a period");
    // Kepler orbit, e = 0.6, period 20 days, starting away from periapsis.
    std::vector<albert::PathPoint> ellipse;const double e=.6;
    for(int i=0;i<=480;++i){const double day=.05*i,mean=2*pi*day/20+2.;double E=mean;for(int k=0;k<50;++k)E-=(E-e*std::sin(E)-mean)/(1-e*std::cos(E));
        ellipse.push_back(plane(day,std::cos(E)-e,std::sqrt(1-e*e)*std::sin(E)));}
    require(std::abs(albert::revolutionDays(ellipse,normal)-20)<.01,"Eccentric revolution period");
    require(albert::pathPresent(3,6,10)==3&&std::abs(albert::pathPresent(7,6,10)+3)<1e-12&&std::abs(albert::pathPresent(-26,6,10)-4)<1e-12&&albert::pathPresent(100,6,0)==100,"Folded path time");
    require(albert::pathPresent(37,6,10)==1e7&&albert::pathPresent(-20000,6,10)==1e7,"Highlight shown far from its samples");
}
// No argument: controls and paths. "webgpu": also the graphics offscreen on WebGPU (exit 77 without
// an adapter). Any other argument: the graphics on OpenGL (a window) and on WebGPU.
// The sound's synthesis, offline: one band at 95 dB SPL plays at -14 dBFS around its centre, 135 dB comes down to the same
// over ~10 s (loudness adaptation), silence when not audible, the pan sends it to one side; crackle and booms.
void soundLevels(){
    namespace ac=albert::physics::acoustics;albert::Sound s(false);ac::Spectrum levels;levels.fill(-1e9);levels[29]=95; // 1 kHz.
    auto play=[&](double seconds,bool audible,float pan){s.hear(levels,audible,pan,seconds);std::vector<float> out(static_cast<std::size_t>(seconds*albert::Sound::rate)*2);
        s.render(out.data(),static_cast<unsigned>(out.size()/2),2);return out;};
    auto rms=[](const std::vector<float>& v,int channel,std::size_t from){double sum=0;std::size_t n=0;for(std::size_t i=2*from+static_cast<std::size_t>(channel);i<v.size();i+=2){sum+=static_cast<double>(v[i])*v[i];++n;}return 20*std::log10(std::sqrt(sum/static_cast<double>(n)));};
    play(.3,true,0);const auto one=play(1,true,0);int crossings=0;for(std::size_t i=2;i<one.size();i+=2)crossings+=(one[i]>0)!=(one[i-2]>0);
    std::cout<<"sound: 1 kHz band at 95 dB SPL plays at "<<rms(one,0,0)<<" dBFS, "<<crossings/2<<" Hz by zero crossings\n";
    require(std::abs(rms(one,0,0)+14)<1&&crossings/2>850&&crossings/2<1150,"Sound: a band plays at its level and frequency");
    levels[29]=135;for(int i=0;i<20;++i)play(.5,true,0);const auto loud=play(1,true,0);
    std::cout<<"sound: 135 dB after 10 s plays at "<<rms(loud,0,0)<<" dBFS (adaptation "<<s.adaptation()<<" dB)\n";
    require(std::abs(rms(loud,0,0)+14)<1.2&&std::abs(s.adaptation()-40)<1,"Sound: loud sources adapt down to the same level");
    const auto side=play(1,true,1);std::cout<<"sound: panned right, left "<<rms(side,0,24000)<<" dBFS, right "<<rms(side,1,24000)<<" dBFS\n";
    require(rms(side,0,24000)<-40&&std::abs(rms(side,1,24000)+11)<1.5,"Sound: pan");
    play(.3,false,0);const auto quiet=play(.5,false,0);std::cout<<"sound: silent at "<<rms(quiet,0,0)<<" dBFS\n";require(rms(quiet,0,0)<-80,"Sound: silence");
    // Crackle: the 2 kHz band's power partly as shocks, the level kept, the slope's skewness (crackle's measure) well above 0;
    // a boom of 1 Pa peaks at 0.1774 (95 dB SPL at -14 dBFS) and is negative after half its length.
    auto skew=[](const std::vector<float>& v){double m2=0,m3=0;std::size_t n=0;for(std::size_t i=2;i<v.size();i+=2){const double d=v[i]-v[i-2];m2+=d*d;m3+=d*d*d;++n;}
        m2/=static_cast<double>(n);m3/=static_cast<double>(n);return m3/std::pow(m2,1.5);};
    albert::Sound c(false);levels.fill(-1e9);levels[32]=95;
    auto with=[&](float share){c.hear(levels,true,0,.3,share);std::vector<float> out(48000*2);c.render(out.data(),48000,2);c.hear(levels,true,0,1,share);c.render(out.data(),48000,2);return out;};
    const auto smooth=with(0),cracking=with(.6);
    std::cout<<"sound: crackle at 60% of the treble: "<<rms(cracking,0,0)<<" dBFS against "<<rms(smooth,0,0)<<", the slope's skewness "<<skew(cracking)<<" against "<<skew(smooth)<<"\n";
    require(std::abs(rms(cracking,0,0)-rms(smooth,0,0))<1.5&&skew(cracking)>2&&std::abs(skew(smooth))<.5,"Sound: crackle");
    albert::Sound b(false);b.hear(levels,false,0,.1);b.boom(1,.1,0);std::vector<float> n(14400*2);b.render(n.data(),14400,2);
    float peak=0;std::size_t at=0;for(std::size_t i=0;i<n.size();i+=2)if(n[i]>peak){peak=n[i];at=i/2;}
    std::cout<<"sound: a 1 Pa boom peaks at "<<peak<<" after "<<at<<" samples, "<<n[2*3600]<<" at 75 ms\n";
    require(std::abs(peak-std::tanh(.1774f))<.01&&at<=120&&n[2*3600]<-.05f,"Sound: boom");
}
// Dark adaptation at the Moon: none down to quarter phase; the night side seen from the Earth's direction (sun-Moon-camera
// 180°, full Earth 60.27 radii away) is lit by earthshine alone, ~760 times fainter than the quarter (Allen's law at 90°).
void lunarAdaptationLevels(){
    require(albert::lunarAdaptation(0,180,180,60.27)==1.f&&albert::lunarAdaptation(90,90,90,60.27)==1.f,"No adaptation down to quarter phase");
    const double quarter=std::pow(10.,-.4*(.026*90+4e-9*std::pow(90.,4))),night=albert::lunarAdaptation(180,0,0,60.27),crescent=albert::lunarAdaptation(150,30,30,60.27);
    std::cout<<"lunar dark adaptation: crescent (150 deg) x"<<crescent<<", night side x"<<night<<" (quarter over earthshine "<<quarter/(.434/(60.27*60.27))<<")\n";
    require(std::abs(night-quarter/(.434/(60.27*60.27)))<1&&crescent>1&&crescent<night&&albert::lunarAdaptation(180,0,0,30)<night,"Exposure follows the light the Moon sends");
}
int main(int argc,char** argv) {
    try {
        controls();orbitPaths();lunarAdaptationLevels();soundLevels();
        if(argc>1){
            std::unique_ptr<GpuTarget> gpu;try{gpu=std::make_unique<GpuTarget>();}catch(const std::exception& e){std::cout<<"WebGPU graphics skipped: "<<e.what()<<'\n';return 77;}
            if(gpu){std::cout<<"WebGPU ("<<gpu->context.adapter()<<"):\n";graphics(*gpu);require(albert::gpu::errors()==0,"WebGPU graphics without validation errors");}
        }
        return 0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
