// Camera checks use scene samples, including live sync and floating-origin conversion; no guidance or dynamics.
struct CameraFixture {
    albert::core::Scene registry;std::shared_ptr<Scene> world=Scene::create();albert::SceneObjects objects{*world};
    std::shared_ptr<albert::core::Scenery> scenery=std::make_shared<albert::core::Scenery>();
    std::shared_ptr<albert::core::VehicleLook> look=std::make_shared<albert::core::VehicleLook>();
    double utc=0;
    CameraFixture(){
        albert::core::SceneryPart p;p.name="tower crane crown";p.centre={-22,0,146};p.size={9.5,9.5,4.5};scenery->parts.push_back(p);
        p.name="launch tower lattice";p.centre.z=73;p.size.z=146;scenery->parts.push_back(p);
        p.name="tank vent";p.centre={500,500,10};p.size={1,1,1};scenery->parts.push_back(p);
        albert::core::Object site;site.id="site";site.name="Camera site";site.scenery=scenery;site.samples.emplace_back();registry.create(site);
        look->length=70;look->diameter=9;look->sections.push_back({0,70,4.5,4.5});
        for(int i=0;i<13;++i)look->engines.push_back({{67,0,0},{1,0,0},.7});
        vehicle();place({0,0,100},{0,0,20});
    }
    void vehicle(){albert::core::Object o;o.id="flight";o.name="Camera vehicle";o.providerManaged=true;o.look=look;o.radiusMeters=look->length/2;
        o.samples.emplace_back(utc,albert::core::Vector{0,0,100},albert::core::Vector{},albert::core::Quaternion{std::sqrt(.5),0,0,std::sqrt(.5)},true);
        registry.create(o,registry.objects().contains(o.id));}
    void place(albert::core::Vector p,albert::core::Vector v={},albert::core::Quaternion q={std::sqrt(.5),0,0,std::sqrt(.5)},float heat=0){
        albert::core::Sample s{utc,p,v,q,true};auto parts=std::make_shared<std::vector<float>>(look->engines.size()*3+7,0);
        (*parts)[parts->size()-6]=heat;parts->back()=-1;s.parts=parts;registry.replace("flight",{s});objects.sync(registry,utc,{});
    }
    albert::BroadcastView view(albert::BroadcastCamera& camera,float dt=1.f/60){
        const auto v=camera.update(registry,objects,"flight",utc,dt);require(v.has_value(),"Camera missing for a valid vehicle and tower");return *v;
    }
};
void cameraControls(){
    using M=albert::core::CameraMode;
    CameraFixture f;albert::BroadcastCamera camera;
    require(!albert::core::cameraMode("typo")&&!albert::core::cameraMode(""),"Unknown camera accepted");
    for(std::size_t i=0;i<albert::core::cameraModes.size();++i){const auto m=static_cast<M>(i);
        require(albert::core::cameraMode(albert::core::cameraModes[i])==m,"Camera name round trip");camera.select(m);
        if(m==M::Orbit){require(!camera.update(f.registry,f.objects,"flight",0,0),"Orbit stolen by broadcast rig");continue;}
        const auto v=f.view(camera);const Vector3 d=Vector3(v.aim).sub(v.eye).normalize();
        require(std::isfinite(v.eye.length())&&std::isfinite(v.fov)&&v.fov>0&&v.fov<150,"Nonfinite camera or lens");
        require(std::abs(v.up.dot(d))<.001f&&std::abs(v.up.length()-1)<.0001f,"Undefined horizon or roll");
    }
    camera.select(M::Tower);const auto crown=f.view(camera);f.utc=1;f.place({0,0,210},{0,0,-10});const auto incoming=f.view(camera);
    require(crown.eye.distanceTo(incoming.eye)<1e-10f,"Tower camera moved off its mount");
    { // Incoming catch pins and the rail slot must share the frame, rather than aiming down at the body centre/pad.
        CameraFixture catchView;
        for(double side:{-1.,1.}){
            albert::core::LookPart pin;pin.name="catch fitting";pin.hinge={7,side*4.56,0};catchView.look->parts.push_back(pin);
            for(double lip:{-.2,.2}){albert::core::SceneryBar rail;rail.from={-10,side*5+lip,100};rail.to={10,side*5+lip,100};rail.hinged=true;rail.pivot={-16,side*5,100};catchView.scenery->bars.push_back(rail);}
        }
        catchView.vehicle();albert::BroadcastCamera tower;tower.select(M::Tower);Vector3 fixed;
        for(double height:{200.,150.,110.,91.,72.15}){
            catchView.utc+=1;catchView.place({0,0,height},{0,0,-8});albert::BroadcastView view;
            for(int i=0;i<120;++i)view=catchView.view(tower);
            if(fixed.lengthSq()==0)fixed=view.eye;else require(view.eye.distanceTo(fixed)<1e-10f,"Catch framing moved the crown mount");
            auto lens=PerspectiveCamera::create(view.fov,16.f/9,.05f/6378137,10);lens->position.copy(view.eye);lens->up.copy(view.up);lens->lookAt(view.aim);lens->updateMatrixWorld();
            for(const auto p:{albert::core::Vector{0,0,height+28},albert::core::Vector{-8,-5,100},albert::core::Vector{8,5,100}}){
                if(height>110&&p.z==100)continue; // The pins are above the crown: keep them framed until the slot also fits.
                const Vector3 ndc=albert::renderPosition(p).project(*lens);
                require(std::abs(ndc.x)<.9f&&std::abs(ndc.y)<.9f,"Tower catch camera lost incoming pins or the arm slot");
            }
            require(view.aim.y*6378137>90&&view.fov<96,"Tower catch camera still frames the lower body/pad");
        }
    }
    camera.select(M::Tracking);const auto close=f.view(camera);f.utc=2;f.place({2000,0,80000});const auto far=f.view(camera);
    require(far.fov<close.fov,"Tracking lens failed to narrow with range");
    for(int i=0;i<120;++i){f.utc+=1./60;f.place({2000+i*8.,0,80000+i*5.},{480,0,300});const auto v=f.view(camera);
        const auto centre=albert::renderPosition(f.registry.evaluate("flight",f.utc).state.positionMeters);
        require(v.aim.distanceTo(centre)*6378137<22,"Tracking lag or jitter lost its subject");}
    // The same eye/aim in physical space across a large floating-origin change.
    const auto a=f.view(camera,0);const albert::core::Vector origin{12345,-7000,80000};f.objects.sync(f.registry,f.utc,origin);const auto b=f.view(camera,0);
    require(Vector3(a.eye).add(albert::renderPosition({},origin)).distanceTo(b.eye)<2e-8f,"Rebase moved a ground camera");
    f.objects.sync(f.registry,f.utc,{});
    // Attached rigs retain their exact hull offset and roll while the vehicle rotates/translates.
    for(M mode:{M::Engine,M::Hotstage,M::Flap}){camera.select(mode);f.utc+=1;f.place({0,0,6000});const auto first=f.view(camera);
        const auto& hull=*f.objects.nodes().at("flight");const Vector3 offset=Vector3(first.eye).sub(hull.position).applyQuaternion(hull.quaternion.clone().invert());
        Quaternion q;q.setFromAxisAngle(Vector3(0,1,0),.8f);q.multiply(Quaternion(std::sqrt(.5f),0,0,std::sqrt(.5f)));
        f.utc+=1;f.place({170,240,6200},{},{q.x,q.y,q.z,q.w});const auto second=f.view(camera);
        const auto& turned=*f.objects.nodes().at("flight");const Vector3 after=Vector3(second.eye).sub(turned.position).applyQuaternion(turned.quaternion.clone().invert());
        require(offset.distanceTo(after)*6378137<.02f,"Onboard mount detached during rotation");}
    // A real cloud under the synced site, exactly where the nominal drone eye would be.
    f.utc+=1;f.place({0,0,100});albert::LaunchCloud cloud({-161,-72,168},{0,1,0});cloud.update(1,41,20,{0,0,0},true);
    f.objects.nodes().at("site")->add(cloud.node());camera.select(M::Drone);const auto drone=f.view(camera);Vector3 local=drone.eye;cloud.node()->worldToLocal(local);
    require(local.length()>=1.19f,"Drone entered the live launch cloud");
    f.objects.nodes().at("site")->remove(*cloud.node());
    // Director: launch -> ascent -> separation -> return -> catch; a held catch stays on the tower.
    camera.select(M::Auto);f.utc=0;f.place({0,0,100},{0,0,20});require(f.view(camera).shot==M::Tower,"Auto launch shot");
    f.utc=1;f.place({500,0,2000},{100,0,100});require(f.view(camera).shot==M::Tracking,"Auto ascent shot");
    albert::core::Object ship=f.registry.get("flight");ship.id="flight.ship";ship.samples.front().positionMeters={0,0,80090};f.registry.create(ship);
    f.utc=2;f.place({0,0,80000},{0,0,100});require(f.view(camera).shot==M::Hotstage,"Auto missed hot staging");
    f.utc=12;f.place({10000,0,20000},{0,0,-300});require(f.view(camera).shot==M::Tracking,"Auto long return shot");
    f.utc=13;f.place({600,0,1000},{0,0,-60});require(f.view(camera).shot==M::Drone,"Auto close return shot");
    f.utc=14;f.place({0,0,140},{0,0,-2});require(f.view(camera).shot==M::Tower,"Auto catch shot");
    f.utc=15;f.place({0,0,140});require(f.view(camera).shot==M::Tower,"Auto cut away from a settled catch");
    f.look->engines.resize(6);f.look->length=52;f.vehicle();camera.reset();f.utc=16;f.place({80000,0,65000},{1000,0,-100},{std::sqrt(.5),0,0,std::sqrt(.5)},600000);
    require(f.view(camera).shot==M::Flap,"Auto missed ship entry");
    f.registry.remove("flight");f.objects.sync(f.registry,f.utc,{});require(!camera.update(f.registry,f.objects,"flight",f.utc,0),"Removed vehicle retained a camera");
    CameraFixture director;auto returning=director.registry.get("flight");returning.id="flight.booster";returning.look=std::make_shared<const albert::core::VehicleLook>(*director.look);
    returning.samples.front().positionMeters={10000,0,60000};returning.samples.front().velocityMetersPerSecond={0,0,-200};director.registry.create(returning);
    director.look->engines.resize(6);director.look->length=52;director.vehicle();director.place({10000,0,61000},{0,0,300});camera.select(M::Auto);
    require(!camera.nextFocus(director.registry,director.objects,"flight",0),"Director abandoned staging too early");
    require(camera.nextFocus(director.registry,director.objects,"flight",9)=="flight.booster","Director missed booster return");
    auto caught=returning.samples.front();caught.utcSeconds=10;caught.positionMeters={0,0,80};caught.velocityMetersPerSecond={};director.registry.replace(returning.id,{caught});director.objects.sync(director.registry,10,{});
    require(!camera.nextFocus(director.registry,director.objects,returning.id,10),"Director abandoned the catch too early");
    require(camera.nextFocus(director.registry,director.objects,returning.id,17)=="flight","Director failed to return to the ship");
    camera.select(M::Drone);camera.cloudLayer(900,2200);director.utc=18;director.place({100,0,1200});const auto clear=director.view(camera);
    require(clear.eye.y*6378137<840||clear.eye.y*6378137>2260,"Drone entered the weather cloud layer");
    std::cout<<"Broadcast cameras: names, lens, horizon, tracking, rebase, rigid mounts, cloud avoidance and director OK\n";
}
void cameraGraphics(GpuTarget& renderer){
    auto world=Scene::create();world->background=Color(0);auto material=MeshBasicMaterial::create();material->color=Color(0xffffff);
    // A bright off-axis grid: radial distortion must bend/relocate it, without affecting the centre or making an empty frame.
    for(float x:{-6.f,-3.f,0.f,3.f,6.f}){auto bar=Mesh::create(BoxGeometry::create(.15f,16,.1f),material);bar->position.set(x,0,-8);world->add(bar);}
    auto camera=PerspectiveCamera::create(145,1,.1f,100);renderer.scene.fisheye=false;renderer.render(*world,*camera);const auto flat=renderer.readRGBPixels();
    renderer.scene.fisheye=true;renderer.render(*world,*camera);const auto lens=renderer.readRGBPixels();
    require(flat!=lens&&std::count_if(lens.begin(),lens.end(),[](auto x){return x>100;})>1000,"Fisheye failed to resample the scene");
    renderer.render(*world,*camera);require(renderer.readRGBPixels()==lens,"Stationary fisheye shimmered");renderer.scene.fisheye=false;
}
