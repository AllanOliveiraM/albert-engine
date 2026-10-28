#ifdef ALBERT_TEST_RPC
albert::core::Scenery crowdScenery(){
    namespace ex=albert::example::starship;albert::v1::CreateFlightRequest request;ex::addTower(request,true);
    albert::core::Scenery s;s.latitude=ex::towerLatitude*ex::deg;s.longitude=ex::towerLongitude*ex::deg;s.buildings=productionBuildings(request);
    for(const auto& r:request.roads()){albert::core::SceneryRoad q;q.width=r.width_meters();q.highway=r.highway();q.surface=static_cast<albert::core::SceneryRoad::Surface>(r.surface());
        for(const auto& p:r.points_enu_meters())q.points.push_back({p.x(),p.y(),p.z()});s.roads.push_back(q);}
    return s;
}
void crowdControls(){
    using C=albert::LaunchCrowd;
    require(C::launchDay(-120,false)&&C::launchDay(0,false)&&C::launchDay(1200,false),"Launch crowd missed count/liftoff");
    require(!C::launchDay(-14401,false)&&!C::launchDay(1201,false)&&!C::launchDay(-20,true)&&!C::launchDay(NAN,false),"Launch crowd escaped phase gates");
    const auto s=crowdScenery();C crowd(s);crowd.enabled(true,0);require(crowd.vehicles().empty()&&!crowd.node()->visible,"Low quality allocated/drew crowd instances");crowd.enabled(true,1);
    const auto& cars=crowd.vehicles();const auto& people=crowd.spectators();
    unsigned types[6]{};float west=0,east=-1e6;for(const auto& car:cars){++types[car.kind];west=std::min(west,car.position.x);east=std::max(east,car.position.x);}
    std::cout<<"Launch crowd: "<<cars.size()<<" vehicles/equipment, "<<people.size()<<" spectators, span "<<east-west<<" m, types";for(auto n:types)std::cout<<' '<<n;std::cout<<'\n';
    require(cars.size()>300&&cars.size()<3000&&people.size()>500&&east-west>2000,"Launch parking lost kilometres or spectators");
    for(auto n:types)require(n>0,"Missing a vehicle type or press tripods");
    auto safe=[](Vector3 p){return std::hypot(p.x,p.y)>=2000&&std::hypot(p.x+353.2f,p.y-70.7f)>=2000;};
    auto roadDistance=[](Vector3 p,albert::core::Vector a,albert::core::Vector b){const Vector3 start(float(a.x),float(a.y),0),end(float(b.x),float(b.y),0),d=Vector3(end).sub(start);
        return p.distanceTo(Vector3(start).addScaledVector(d,std::clamp(Vector3(p).sub(start).dot(d)/std::max(d.lengthSq(),.001f),0.f,1.f)));};
    for(const auto& car:cars){require(safe(car.position),"Parking entered pad exclusion");const float radius=car.kind==4?4.8f:car.kind==3?3.6f:car.kind==5?.7f:3.1f;
        for(const auto& road:s.roads)for(std::size_t i=1;i<road.points.size();++i)require(roadDistance(car.position,road.points[i-1],road.points[i])>=road.width/2+radius+.4f,"Parking/camera envelope entered pavement");}
    unsigned beach=0;for(const auto& p:people){require(safe(p.position),"Spectators entered pad exclusion");beach+=p.position.x>0&&p.position.y<-2100;}
    require(beach>100,"Beach lost its public groups");std::cout<<"Beach spectators: "<<beach<<'\n';
    auto camera=PerspectiveCamera::create(60,1,.1,20000);camera->position.set(600,-2700,500);crowd.view(*camera);
    require(crowd.detailedCars()==0&&crowd.detailedPeople()==0&&crowd.carDetailVertices()==0,"Beach view allocated near meshes beyond 150 m");
    camera->position.copy(cars.front().position).add({0,-15,2});crowd.view(*camera,[](const Vector3& p){return std::optional<float>(.02f*p.x+.01f*p.y+3);});
    require(crowd.nearby()>0&&crowd.nearby()<=800,"Near spectator budget failed");
    require(crowd.detailedCars()>0&&crowd.detailedCars()<=C::nearCarLimit&&crowd.detailedPeople()>0&&crowd.detailedPeople()<=C::nearPersonLimit,"Near LOD selection/budget failed");
    std::cout<<"Crowd near templates: "<<crowd.carDetailVertices()/3<<" car triangles, "<<crowd.personDetailVertices()/3<<" person triangles; instances "<<crowd.detailedCars()<<" / "<<crowd.detailedPeople()<<'\n';
    require(crowd.carDetailVertices()>108&&crowd.carDetailVertices()<6000&&crowd.personDetailVertices()>102&&crowd.personDetailVertices()<2000,"Near geometry escaped its budget");
    const auto& grounded=crowd.vehicles().front();require(std::abs(grounded.position.z-(.02f*grounded.position.x+.01f*grounded.position.y+3.06f))<.001f&&grounded.up.z>.99f,"Parking did not follow terrain height/slope");
    camera->position.set(0,0,20000);crowd.view(*camera);require(crowd.nearby()==0,"Distant people retained their draw");
    crowd.enabled(false,1);require(!crowd.node()->visible,"Non-launch day retained crowd");
    C transition(s);transition.enabled(true,1);const auto first=transition.vehicles().front().position;
    auto firstNear=[&](){const auto* mesh=transition.node()->getObjectByName("launch near cars");if(!mesh)return false;const auto* p=mesh->geometry()->getAttribute<float>("position");
        for(int i=0;i<mesh->geometry()->drawRange.count;++i)if(std::abs(p->getX(i)-first.x)<.01f&&std::abs(p->getY(i)-first.y)<.01f)return true;return false;};
    for(const auto [height,expected]:std::array<std::pair<float,bool>,5>{{{134,true},{145,true},{151,false},{145,false},{134,true}}}){
        camera->position.copy(first).add({0,0,height});transition.view(*camera);require(firstNear()==expected,"Near LOD lost its 135/150 m hysteresis");}
    // Integration uses the real count hook; removal, low quality and rewind cannot leave a latched crowd behind.
    CameraFixture f;f.scenery->latitude=s.latitude;f.scenery->longitude=s.longitude;f.scenery->roads=s.roads;f.scenery->buildings=s.buildings;
    auto site=f.registry.get("site");site.scenery=f.scenery;f.registry.create(site,true);f.objects.count("flight",-30,false);f.objects.sync(f.registry,0,{});
    auto* layer=f.objects.nodes().at("site")->getObjectByName("launch day crowd");require(layer&&layer->visible,"Count failed to enable site crowd");
    f.objects.count("flight",0,false,false);f.objects.sync(f.registry,0,{});require(!layer->visible,"Ordinary uncounted flight created launch-day crowd");
    f.objects.count("flight",-30,false);f.objects.sync(f.registry,0,{});
    f.objects.quality(0);require(!layer->visible,"Quality failed to hide crowd");f.objects.quality(1);require(layer->visible,"Quality failed to restore crowd");
    f.objects.count("flight",1400,false);f.objects.sync(f.registry,1400,{});require(!layer->visible,"Postlaunch crowd remained enabled");
    f.objects.count("flight",-30,false);f.objects.sync(f.registry,-30,{});require(layer->visible,"Rewind failed to restore count crowd");
    f.registry.remove("flight");f.objects.sync(f.registry,0,{});require(!layer->visible,"Removed flight retained launch crowd");
}
void crowdGraphics(GpuTarget& renderer){
    albert::LaunchCrowd crowd(crowdScenery());crowd.enabled(true,1);auto world=Scene::create();world->background=Color(0);world->add(crowd.node());
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(0,0,100);world->add(sun);world->add(AmbientLight::create(Color(0xffffff),.2f));
    const auto p=crowd.vehicles().front().position;auto camera=PerspectiveCamera::create(60,1,.1,50000);camera->up.set(0,0,1);camera->position.copy(p).add({14,-20,5});camera->lookAt(p);crowd.view(*camera);
    renderer.render(*world,*camera);const auto lit=renderer.readRGBPixels();require(renderer.scene.drawn()<=4&&renderer.scene.drawn()>=3&&std::count_if(lit.begin(),lit.end(),[](auto c){return c>30;})>100,"Instanced crowd exceeded four draws or lost near meshes");
    renderer.render(*world,*camera);require(renderer.readRGBPixels()==lit,"Static crowd/fade shimmered");
    auto* figures=crowd.node()->getObjectByName("launch nearby spectators");auto* nearPeople=crowd.node()->getObjectByName("launch near people");auto* nearCars=crowd.node()->getObjectByName("launch near cars");
    require(nearPeople&&nearCars&&nearPeople->visible&&nearCars->visible,"Missing near instance draws");
    nearPeople->visible=false;renderer.render(*world,*camera);require(renderer.readRGBPixels()!=lit,"Human meshes produced no pixels");nearPeople->visible=true;
    nearCars->visible=false;renderer.render(*world,*camera);require(renderer.readRGBPixels()!=lit,"Rounded car meshes produced no pixels");nearCars->visible=true;
    // Partitioning must neither duplicate nor drop parked vehicles, including press tripods.
    const auto* farCars=crowd.node()->getObjectByName("launch parked cars and press");
    require(farCars->geometry()->drawRange.count+nearCars->geometry()->drawRange.count==int(crowd.vehicles().size()),"Near/far parking lost or duplicated instances");
    crowd.enabled(true,0);renderer.render(*world,*camera);require(renderer.scene.drawn()==0,"Low quality crowd still drew");
    crowd.enabled(true,1);camera->position.z=20000;crowd.view(*camera);require(!figures->visible,"Distant spectator draw still submitted");
}
#endif
