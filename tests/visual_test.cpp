#include "animation.hpp"
#include "borders.hpp"
#include "paths.hpp"
#include "visuals.hpp"
#include "objects.hpp"
#include "asset_pack.hpp"
#include "core/bodies.hpp"
#include "rocks.hpp"
#include "eclipses.hpp"
#include "layers.hpp"
#include "core/layers.hpp"
#include "core/model.hpp"
#include "model_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

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
    {   // Measured small bodies are built-in solids; without their library assets they fall back to rocks.
        albert::core::Scene registry;auto world=Scene::create();albert::SceneObjects objects(*world);auto solid=Mesh::create(SphereGeometry::create(.5f,8,4));
        registry.include(*albert::core::localOrbit("hyperion"));registry.include(*albert::core::localOrbit("phoebe"));objects.bind("phoebe",solid);objects.sync(registry,0,{});
        require(registry.get("hyperion").appearance.shape==albert::core::Shape::Builtin&&objects.nodes().contains("hyperion")&&objects.nodes().at("hyperion")!=solid&&objects.nodes().at("phoebe")==solid,"Measured body without assets vanished or replaced its bound solid");
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
void graphics() {
    Canvas canvas(Canvas::Parameters().title("Albert visual regression").size(192,192).headless(true).antialiasing(0));
    GLRenderer renderer(canvas);renderer.setPixelRatio(1);
    auto scene=Scene::create();scene->background=Color(0x000000);
    auto camera=OrthographicCamera::create(-.6f,.6f,.6f,-.6f,.01f,300.f);
    camera->position.set(0,0,1.5f);camera->lookAt(Vector3());
    scene->add(AmbientLight::create(Color(0xffffff),1.f));
    albert::PlanetVisuals planets(*scene,assets());
    planets.earth->position.set(100,0,0);planets.sun->position.set(250,0,0);
    albert::VisualSettings settings;settings.atmosphere=settings.clouds=settings.nightLights=settings.shadows=false;
    albert::RegionalImages region;region.body="moon";region.region={-135,-45,-45,45};
    region.day=rgba(255,128);region.height=height(-1200.f);
    planets.stageRegionalImages(std::move(region));
    for(int frame=0;planets.uploading() && frame<256;++frame) planets.updateUploads(renderer);
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
    for(int i=-100;i<=100;++i) {const float angle=i*.001f;points.emplace_back(std::sin(angle),0,std::cos(angle));}
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
    solids.update(renderer,*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,1.f);renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    const int lit=pixels[3*(96*192+96)];require(lit>20,"Solid body solar shader failed to render");
    solids.update(renderer,*solidCamera,Vector3(0,0,-23455),2451545.,"mercury",true,1.f);renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    require(pixels[3*(96*192+96)]<3,"Solid body's night hemisphere is lit without solar incidence");
    albert::AuxiliaryLights auxiliary;auxiliary["mercury"].enabled=true;
    solids.update(renderer,*solidCamera,Vector3(0,0,-23455),2451545.,"mercury",true,0.f,auxiliary);renderer.render(*solidScene,*solidCamera);require(renderer.readRGBPixels()[3*(96*192+96)]<3,"Auxiliary light appeared before its fade");
    solids.update(renderer,*solidCamera,Vector3(0,0,-23455),2451545.,"mercury",true,1.f,auxiliary);renderer.render(*solidScene,*solidCamera);require(renderer.readRGBPixels()[3*(96*192+96)]>15,"Solid auxiliary light did not reveal the night hemisphere");
    solids.update(renderer,*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,1.f,auxiliary);renderer.render(*solidScene,*solidCamera);require(std::abs(int(renderer.readRGBPixels()[3*(96*192+96)])-lit)<3,"Auxiliary light changed the day hemisphere");
    auxiliary["mercury"].enabled=false;solids.update(renderer,*solidCamera,Vector3(0,0,-23455),2451545.,"mercury",true,1.f,auxiliary);renderer.render(*solidScene,*solidCamera);require(renderer.readRGBPixels()[3*(96*192+96)]<3,"Solid auxiliary light remained enabled after fade out");
    albert::RegionalImages solidRegion;solidRegion.body="mercury";solidRegion.day=rgba(230);solidRegion.height=height(1000);
    albert::prepareRegionalImages(solidRegion);solids.stage(std::move(solidRegion));
    for(int i=0;solids.uploading()&&i<256;++i)solids.update(renderer,*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,0.f);
    require(!solids.uploading(),"Solid regional texture upload did not finish");renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    require(std::abs(int(pixels[3*(96*192+96)])-lit)<4,"Solid detail appeared without crossfade");
    solids.update(renderer,*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,1.f);renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    require(pixels[3*(96*192+96)]>lit+30,"Solid detail crossfade did not settle");
    solids.update(renderer,*solidCamera,Vector3(0,0,23455),2451545.,"earth",true,4.f);renderer.render(*solidScene,*solidCamera);pixels=renderer.readRGBPixels();
    require(std::abs(int(pixels[3*(96*192+96)])-lit)<4,"Solid detail eviction damaged the global surface");
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
        auto camera=PerspectiveCamera::create(45,1,.001f,30000.f);camera->position.set(0,0,radius*4);camera->lookAt(Vector3());
        moons.update(renderer,*camera,Vector3(0,0,23455),2451545.,id,true,1.f);renderer.render(*world,*camera);
        require(renderer.readRGBPixels()[3*(96*192+96)]>20,"Local moon surface did not render");
        if(moons.hazy(id)){
            // Visible haze hides the grey test map behind its orange colour; the
            // surface view fades it out and shows the map again.
            const auto hazy=renderer.readRGBPixels();require(hazy[3*(96*192+96)]>2*hazy[3*(96*192+96)+2]+10,"Visible haze did not cover the surface");
            moons.showSurface(id,true);for(int i=0;i<40;++i)moons.update(renderer,*camera,Vector3(0,0,23455),2451545.,id,true,.05f);renderer.render(*world,*camera);
            const auto surface=renderer.readRGBPixels();require(std::abs(int(surface[3*(96*192+96)])-int(surface[3*(96*192+96)+2]))<12&&moons.surfaceView(id),"Surface view did not reveal the map");
            moons.showSurface(id,false);
        }
        Raycaster ray;ray.setFromCamera(Vector2(0,0),*camera);require(objects.pick(registry,ray)==id,"Textured local moon cannot be picked");
        auto node=moons.nodes().at(id);node->geometry()->computeBoundingSphere();require(std::abs(node->geometry()->boundingSphere->radius-radius)<1e-5f,"Moon radius scaled twice");
        if(definition.axesMeters[0]>0){const auto& axes=definition.axesMeters;const double r=definition.radiusMeters;
            require(std::abs(node->scale.x-axes[0]/r)<1e-6&&std::abs(node->scale.y-axes[2]/r)<1e-6&&std::abs(node->scale.z-axes[1]/r)<1e-6,"IAU axes mapped incorrectly into render coordinates");}
        albert::RegionalImages detail;detail.body=id;detail.day=rgba(230);detail.height=height(0);albert::prepareRegionalImages(detail);moons.stage(std::move(detail));
        for(int i=0;moons.uploading()&&i<256;++i)moons.update(renderer,*camera,Vector3(0,0,23455),2451545.,id,true,0.f);
        require(!moons.uploading(),"Moon detail upload did not finish");moons.update(renderer,*camera,Vector3(0,0,23455),2451545.,id,true,1.f);
        moons.update(renderer,*camera,Vector3(0,0,23455),2451545.,"earth",true,4.f);require(moons.takeEviction(),"Moon detail stayed resident after focus changed");
    }
    for(const std::string id:{"phobos","deimos","mimas","phoebe","hyperion","proteus"}){
        auto world=Scene::create();world->background=Color(0x000000);auto shape=albert::decodeShape(albert::visualAssetPack()->payload("v1/"+id+"/shape.bin"));auto geometry=shape.geometry;
        std::vector<albert::SurfaceAssets> maps;maps.push_back({id,rgba(140),height(0),{},std::move(shape)});albert::SolidBodies measured(*world,std::move(maps));
        albert::core::Scene registry;registry.include(*albert::core::localOrbit(id));albert::SceneObjects objects(*world);objects.bind(id,measured.nodes().at(id),measured.picker(id));objects.sync(registry,0,{});
        auto camera=PerspectiveCamera::create(45,1,.00001f,30000.f);camera->position.set(0,0,float(albert::core::surfaceBody(id)->shapeRadiusMeters*4/albert::core::earthRadiusMeters));camera->lookAt(Vector3());
        measured.update(renderer,*camera,Vector3(0,0,23455),2451545.,id,true,1.f);renderer.render(*world,*camera);const auto before=renderer.readRGBPixels();require(before[3*(96*192+96)]>10,"Observed shape failed to render");
        Raycaster ray;ray.setFromCamera(Vector2(0,0),*camera);require(objects.pick(registry,ray)==id,"Measured BVH picking failed");
        albert::RegionalImages detail;detail.body=id;detail.day=rgba(230);detail.textureOnly=true;albert::prepareRegionalImages(detail);require(!detail.geometry&&!detail.surfaceMask,"Measured imagery allocated a replacement sphere or mask");measured.stage(std::move(detail));
        for(int i=0;measured.uploading()&&i<256;++i)measured.update(renderer,*camera,Vector3(0,0,23455),2451545.,id,true,0.f);
        require(!measured.uploading(),"Observed shape detail upload stalled");renderer.render(*world,*camera);const auto start=renderer.readRGBPixels();require(std::abs(int(start[3*(96*192+96)])-before[3*(96*192+96)])<4,"Measured detail appeared without fade");
        measured.update(renderer,*camera,Vector3(0,0,23455),2451545.,id,true,1.f);renderer.render(*world,*camera);const auto after=renderer.readRGBPixels();long increase=0;for(size_t i=0;i<after.size();i+=3)increase+=int(after[i])-int(before[i]);require(increase>500,"Measured texture detail fade did not settle");
        require(measured.nodes().at(id)->geometry()==geometry&&measured.nodes().at(id)->children.empty(),"Texture LOD replaced observed plate geometry");
        measured.update(renderer,*camera,Vector3(0,0,23455),2451545.,"earth",true,4.f);require(measured.takeEviction(),"Measured texture detail was not evicted");renderer.render(*world,*camera);const auto released=renderer.readRGBPixels();require(std::abs(int(released[3*(96*192+96)])-before[3*(96*192+96)])<4,"Shape detail eviction damaged base imagery");
    }
    albert::RegionalImages reload;reload.body="mercury";reload.day=rgba(230);reload.height=height(1000);albert::prepareRegionalImages(reload);solids.stage(std::move(reload));
    for(int i=0;solids.uploading()&&i<256;++i)solids.update(renderer,*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,0.f);
    solids.update(renderer,*solidCamera,Vector3(0,0,23455),2451545.,"mercury",true,4.f);
    require(solids.takeEviction(),"Zooming out on the same planet retained high-resolution terrain");
    {
        auto world=Scene::create();world->background=Color(0x000000);world->add(AmbientLight::create(Color(0xffffff),.2f));
        auto light=DirectionalLight::create(Color(0xffffff),3.f);light->position.set(0,0,-100);world->add(light);
        albert::PlanetVisuals bodies(*world,assets());bodies.earth->position.set(0,0,0);bodies.moon->position.set(0,0,60);bodies.sun->position.set(0,0,-23455);
        light->setTarget(*bodies.moon);
        auto view=PerspectiveCamera::create(60,1,.001f,100000);view->position.set(0,0,58.9f);view->lookAt(bodies.moon->position);
        albert::VisualSettings settings;settings.atmosphere=settings.clouds=settings.nightLights=false;
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
    std::cout<<"Graphics OK: incremental upload, terrain occlusion, paths, clouds, objects, artistic rocks and lunar eclipse\n";
}
}
int main(int argc,char**) {
    try {controls();if(argc>1) graphics();return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
