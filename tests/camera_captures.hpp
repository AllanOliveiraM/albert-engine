#ifdef ALBERT_TEST_RPC
// Prescribed poses still need their own atmosphere and speed: Mach 1 at sea-level pressure invents a condensation collar.
void cameraCaptureAir(const albert::core::Sample& state,std::vector<float>& parts){
    const auto air=albert::physics::standardAtmosphere(std::max(0.,state.positionMeters.z));
    const auto& v=state.velocityMetersPerSecond;const double speed=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    parts[parts.size()-7]=float(air.pressure);parts[parts.size()-4]=float(std::copysign(speed/std::max(air.sound,1.),v.z));
}
void cameraCaptureControls(){
    albert::core::VehicleLook look;look.length=72;look.diameter=9;look.sections.push_back({0,72,4.5,4.5});look.engines.push_back({{72,0,0},{1,0,0},.7});
    albert::Vehicle vehicle(look);std::vector<float> parts(10,0);parts.back()=-1;
    cameraCaptureAir({0,{0,0,500},{0,0,-55}},parts);vehicle.animate(&parts,0);
    const auto* collar=vehicle.node()->getObjectByName("transonic condensation");
    require(collar&&!collar->visible&&std::abs(parts[6])<.2f,"Slow landing fixture invented a transonic collar");
    cameraCaptureAir({0,{10000,0,65000},{0,0,500}},parts);vehicle.animate(&parts,1);
    require(!collar->visible&&parts[3]<100&&parts[6]>1.2f,"Staging fixture did not use high-altitude air");
    cameraCaptureAir({0,{0,0,500},{0,0,340}},parts);vehicle.animate(&parts,2);
    require(collar->visible,"Correcting the camera fixture disabled real transonic condensation");
}
void cameraCaptures(const std::string& directory,bool diagnostics=false){
    namespace ex=albert::example::starship;using M=albert::core::CameraMode;
    std::filesystem::create_directories(directory);CameraFixture f;GpuTarget target(1280,720);
    target.scene.toneMapping=true;target.scene.samples=4;target.scene.shadows=true;target.scene.shadowMetersPerUnit=6378137;
    albert::v1::CreateFlightRequest request;ex::addTower(request,true);f.scenery->parts.clear();std::vector<float> angles;
    auto cv=[](const auto& v){return albert::core::Vector{v.x(),v.y(),v.z()};};
    for(const auto& p:request.scenery()){albert::core::SceneryPart q;q.name=p.name();q.centre=cv(p.centre_enu_meters());q.size=cv(p.size_meters());q.heading=p.heading_degrees()*ex::deg;q.tilt=p.tilt_degrees()*ex::deg;
        q.shape=static_cast<albert::core::SceneryPart::Shape>(p.shape());q.inner=p.inner_diameter_meters();q.color=p.color();q.structure=p.structure();f.scenery->parts.push_back(q);}
    for(const auto& b:request.structures()){albert::core::SceneryBar q;q.from=cv(b.start_enu_meters());q.to=cv(b.end_enu_meters());q.radius=b.has_drawn()&&!b.drawn()?0:b.radius_meters();q.hinged=b.has_hinge_enu_meters();q.pivot=cv(b.hinge_enu_meters());q.axis=b.has_hinge_axis()?cv(b.hinge_axis()):albert::core::Vector{0,0,1};f.scenery->bars.push_back(q);angles.push_back(b.start_radians());}
    f.scenery->buildings=productionBuildings(request);
    auto site=f.registry.get("site");site.providerManaged=true;site.scenery=f.scenery;site.samples.front().parts=std::make_shared<const std::vector<float>>(angles);f.registry.create(site,true);
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(-.1f,.2f,.12f);sun->castShadow=true;f.world->add(sun);
    f.world->background=Color(0x80acd0);f.world->add(AmbientLight::create(Color(0xa0bdd9),.18f));
    auto ground=Mesh::create(PlaneGeometry::create(240000.f/6378137,240000.f/6378137),albert::assistedMaterial(Color(.14f,.15f,.12f),0,1));
    ground->name="capture land";ground->rotation.x=-std::numbers::pi_v<float>/2;ground->receiveShadow=true;f.world->add(ground);
    // A blue shore and coastal sky provide scale for the long lens. The real app supplies terrain/ocean/atmosphere.
    auto sea=Mesh::create(PlaneGeometry::create(100000.f/6378137,240000.f/6378137),albert::assistedMaterial(Color(.055f,.19f,.28f),.2f,.3f));
    // Euler components are float_view proxies: proxy-to-proxy assignment does not notify the quaternion. Use set().
    sea->name="capture ocean";sea->rotation.set(-std::numbers::pi_v<float>/2,0,0);sea->position.set(51000.f/6378137,.1f/6378137,0);f.world->add(sea);
    const auto oceanNormal=Vector3(0,0,1).applyQuaternion(sea->quaternion);
    require(oceanNormal.y>.999f&&std::abs(oceanNormal.z)<1e-5f,"Capture ocean is an upright wall instead of a horizontal surface");
    albert::gpu::Atmosphere atmosphere(target.context,albert::gpu::earthAtmosphere());atmosphere.precompute(target.context);
    auto planet=Object3D::create();planet->position.y=-1;f.world->add(planet);target.scene.air={&atmosphere,planet.get(),6378.137f,1};
    auto skyMaterial=ShaderMaterial::create();skyMaterial->name="albert:atmosphere";skyMaterial->side=Side::Back;skyMaterial->depthWrite=false;skyMaterial->uniforms={{"visibility",Uniform(1.f)}};
    auto sky=Mesh::create(SphereGeometry::create(2,32,16),skyMaterial);sky->frustumCulled=false;f.world->add(sky);
    auto camera=PerspectiveCamera::create(45,16.f/9,.05f/6378137,10);
    std::ofstream manifest(directory+"/manifest.csv");manifest<<"phase,mode,shot,fov,eye_east,eye_north,eye_up,aim_east,aim_north,aim_up\n";
    const auto booster=captureLook(ex::superHeavy()),shipLook=captureLook(ex::shipAlone()),stack=captureLook(ex::stack());
    auto sample=[&](const std::string& id,int lit,float heat){const auto& look=*f.registry.get(id).look;unsigned surfaces=0;for(const auto& p:look.parts)surfaces=std::max(surfaces,p.surface);
        auto p=std::make_shared<std::vector<float>>(look.engines.size()*3+surfaces+look.thrusters.size()+7,0);
        for(int i=0;i<lit&&i<int(look.engines.size());++i)(*p)[3*i]=.65f;
        for(unsigned i=0;i<surfaces;++i)(*p)[look.engines.size()*3+i]=.5f;
        auto s=f.registry.evaluate(id,f.utc).state;cameraCaptureAir(s,*p);
        (*p)[p->size()-6]=heat;(*p)[p->size()-5]=.6f;(*p)[p->size()-3]=heat>0?1.2f:0;p->back()=-1;
        s.parts=p;f.registry.replace(id,{s});};
    auto shot=[&](const std::string& phase,M mode,Object3D* hidden=nullptr){
        albert::BroadcastCamera rig;rig.select(mode);
        // Establish descending flight before a near-stationary catch; the normal director has this history.
        if(phase.find("catch")!=std::string::npos&&mode==M::Auto){auto s=f.registry.evaluate("flight",f.utc).state;const auto saved=s;s.velocityMetersPerSecond.z=-20;f.registry.replace("flight",{s});f.objects.sync(f.registry,f.utc,{});rig.update(f.registry,f.objects,"flight",f.utc,1.f/60);f.registry.replace("flight",{saved});}
        f.objects.sync(f.registry,f.utc,{});auto view=rig.update(f.registry,f.objects,"flight",f.utc,1.f/60);
        if(view){camera->position.copy(view->eye);camera->up.copy(view->up);camera->lookAt(view->aim);camera->fov=view->fov;target.scene.fisheye=view->fisheye;}
        else{camera->position.set(240.f/6378137,180.f/6378137,260.f/6378137);camera->up.set(0,1,0);camera->lookAt(f.objects.nodes().at("flight")->position);camera->fov=45;target.scene.fisheye=false;}
        camera->updateProjectionMatrix();f.objects.updateLighting(*camera,sun->position,{},0);
        const bool visible=hidden&&hidden->visible;if(hidden)hidden->visible=false;
        target.render(*f.world,*camera);const auto pixels=target.readRGBPixels();if(hidden)hidden->visible=visible;
        const std::string name=phase+"-"+std::string(albert::core::cameraName(mode));const std::string path=directory+"/"+name+".ppm";std::ofstream out(path,std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
        for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);
        const Vector3 aim=view?view->aim:f.objects.nodes().at("flight")->position;
        manifest<<phase<<','<<albert::core::cameraName(mode)<<','<<(view?albert::core::cameraName(view->shot):"orbit")<<','<<camera->fov<<','<<camera->position.x*6378137<<','<<-camera->position.z*6378137<<','<<camera->position.y*6378137<<','<<aim.x*6378137<<','<<-aim.z*6378137<<','<<aim.y*6378137<<'\n';
        std::cout<<name<<", "<<target.scene.drawn()<<" draws"<<std::endl;
        return pixels;
    };
    for(int phase=0;phase<4;++phase){
        if(f.registry.objects().contains("flight.ship"))f.registry.remove("flight.ship");
        *f.look=phase==0?stack:booster;f.vehicle();f.utc=phase==0?3:phase==1?150:phase==2?390:420;
        const double catchHeight=ex::armHeight+ex::pinRest()-ex::boosterLength/2+ex::pinAt;
        f.place(phase==0?albert::core::Vector{0,0,ex::deck+stack.length/2+14}:phase==1?albert::core::Vector{10000,0,65000}:phase==2?albert::core::Vector{120,-45,500}:albert::core::Vector{0,0,catchHeight},
            {0,0,phase==0?20.:phase==1?500.:phase==2?-55.:-.8});
        sample("flight",phase==0?33:phase==2?3:0,0);
        if(phase==1){auto ship=f.registry.get("flight");ship.id="flight.ship";ship.look=std::make_shared<const albert::core::VehicleLook>(shipLook);ship.samples.front().positionMeters.z+=booster.length/2+shipLook.length/2+8;f.registry.create(ship);sample("flight.ship",6,0);}
        // Open at launch, closed under the pins at catch, QDs retracted after departure.
        auto pose=angles;if(phase>0)for(std::size_t i=0;i<pose.size();++i)pose[i]=i<6?(phase==3?0.f:angles[i]):0.f;
        auto ss=f.registry.evaluate("site",f.utc).state;ss.parts=std::make_shared<const std::vector<float>>(pose);f.registry.replace("site",{ss});
        f.objects.sync(f.registry,f.utc-2,{});f.objects.deluge("site",phase==0?41:0);f.objects.sync(f.registry,f.utc,{});
        const std::string name=phase==0?"liftoff":phase==1?"staging":phase==2?"landing-burn":"catch";
        if(diagnostics){
            if(phase!=2)continue;
            const auto corrected=f.registry.evaluate("flight",f.utc).state;auto legacy=corrected;
            auto parts=std::make_shared<std::vector<float>>(*legacy.parts);(*parts)[parts->size()-7]=101325;(*parts)[parts->size()-4]=1;legacy.parts=parts;
            f.registry.replace("flight",{legacy});sea->quaternion.identity();
            std::cout<<"Legacy ocean normal: "<<Vector3(0,0,1).applyQuaternion(sea->quaternion)<<"; pressure 101325 Pa, Mach 1\n";
            const auto original=shot("isolate-legacy",M::Drone);
            const auto noOcean=shot("isolate-no-ocean",M::Drone,sea.get());
            auto* collar=f.objects.nodes().at("flight")->getObjectByName("transonic condensation");require(collar&&collar->visible,"Diagnostic did not reproduce the collar");
            const auto noCollar=shot("isolate-no-condensation",M::Drone,collar);
            auto difference=[&](const auto& a,const auto& b,unsigned x0,unsigned y0,unsigned x1,unsigned y1){std::uint64_t sum=0;
                for(unsigned y=y0;y<y1;++y)for(unsigned x=x0;x<x1;++x)for(unsigned c=0;c<3;++c){const auto at=((target.height-1-y)*target.width+x)*3+c;sum+=std::abs(int(a[at])-int(b[at]));}return sum;};
            const auto wedge=difference(original,noOcean,950,50,1050,350),cone=difference(original,noCollar,580,260,700,360);
            std::cout<<"Isolated ocean wedge RGB delta: "<<wedge<<"; condensation collar RGB delta: "<<cone<<'\n';
            require(wedge>100000&&cone>10000,"Visibility isolation did not identify the two defects");
            sea->rotation.set(-std::numbers::pi_v<float>/2,0,0);f.registry.replace("flight",{corrected});shot("isolate-fixed",M::Drone);
            std::cout<<"Fixed ocean normal: "<<oceanNormal<<"; pressure "<<corrected.parts->at(corrected.parts->size()-7)<<" Pa, Mach "<<corrected.parts->at(corrected.parts->size()-4)<<'\n';
            std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<std::endl;require(albert::gpu::errors()==0,"Camera diagnostics produced GPU validation errors");return;
        }
        if(phase==3){
            for(const auto& [label,height]:std::array<std::pair<const char*,double>,2>{{{"catch-approach-high",110},{"catch-approach-low",91}}}){
                f.place({0,0,height},{0,0,-8});sample("flight",0,0);shot(label,M::Tower);}
            f.place({0,0,catchHeight},{0,0,-.8});sample("flight",0,0);
        }
        for(std::size_t i=0;i<albert::core::cameraModes.size();++i)shot(name,static_cast<M>(i));
    }
    if(f.registry.objects().contains("flight.ship"))f.registry.remove("flight.ship");*f.look=shipLook;f.vehicle();f.utc=900;
    Quaternion entry;entry.setFromAxisAngle(Vector3(1,0,0),.22f);entry.multiply(Quaternion(std::sqrt(.5f),0,0,std::sqrt(.5f)));
    f.place({30000,0,65000},{7000,0,-150},{entry.x,entry.y,entry.z,entry.w},600000);sample("flight",0,600000);
    for(M mode:{M::Onboard,M::Flap,M::FlapFisheye,M::Engine,M::Auto})shot("ship-entry",mode);
    f.utc=930;f.place({0,0,ex::armHeight-shipLook.length/2+ex::shipPinAt},{0,0,-.8});sample("flight",2,0);
    for(M mode:{M::Tower,M::TowerFisheye,M::Onboard,M::Engine,M::Auto})shot("ship-catch",mode);
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<std::endl;
    require(albert::gpu::errors()==0,"Camera captures produced GPU validation errors");
}
#endif
