// Prescribed state, including paused first-frame staging, without mission or dynamics clocks.
void ascentGraphics(Target& renderer){
    albert::core::VehicleLook look;look.length=120;look.diameter=9;look.sections={{0,11,.05,4.5},{11,52,4.5,4.5},{52,54,4.5,4.5},{54,120,4.5,4.5}};
    for(int i=0;i<33;++i)look.engines.push_back({{120,double(i%6-3)*.7,double(i/6-3)*.7},{1,0,0},.65});
    for(int i=0;i<20;++i){const double a=i*6.2831853/20;look.parts.push_back({"vent",{53,4.5*std::cos(a),4.5*std::sin(a)},{1,0,0},{0,1,0},1.5});}
    albert::Vehicle stack(look);stack.quality(2);stack.humidity(.95f);
    std::vector<float> p(33*3+7);for(int i=0;i<33;++i)p[3*i]=.85f;p[p.size()-7]=90000;p[p.size()-4]=.2f;p.back()=-1;
    stack.animate(&p,0);auto* bloom=stack.node()->getObjectByName("rarefied exhaust");require(bloom->visible&&bloom->scale.y>60,"Ascent cluster lost its long merged exhaust");
    {auto w=Scene::create();w->background=Color(0);w->add(stack.node());auto c=OrthographicCamera::create(-50,50,80,-200,.01,1000);c->position.set(0,-30,300);c->lookAt(Vector3(0,-30,0));
        renderer.render(*w,*c);if(auto* gpu=dynamic_cast<GpuTarget*>(&renderer))require(gpu->scene.drawn()<16,"33-engine ascent escaped its bounded draw budget");}
    p[p.size()-7]=280;p[p.size()-4]=5;stack.animate(&p,1);const float width=bloom->scale.x;require(width>120,"40 km exhaust failed to exceed the stack width");
    p[p.size()-7]=20;stack.animate(&p,2);require(bloom->scale.x>width,"Ascent did not reuse return pressure expansion");
    p[p.size()-7]=26000;p[p.size()-4]=1.3f;stack.animate(&p,3);
    auto* mist=stack.node()->getObjectByName("transonic condensation")->as<Mesh>();require(mist->visible&&mist->material()->as<ShaderMaterial>()->uniforms.at("state").value<Vector4>().x>.1f,"Forward supersonic expansion lost its vapour collar");
    stack.humidity(.1f);stack.animate(&p,3);require(!mist->visible,"Dry ascent retained vapour");
    stack.humidity(.95f);p[p.size()-4]=1.6f;stack.animate(&p,3);require(mist->material()->as<ShaderMaterial>()->uniforms.at("state").value<Vector4>().x==0,"Ascent collar persisted beyond low supersonic flow");
    { // Compare the batched shader with the original per-engine program, including asymmetric flow and gimbal.
        albert::core::VehicleLook l;l.length=8;l.diameter=3;l.sections={{0,8,1.5,1.5}};
        for(int i=0;i<3;++i)l.engines.push_back({{8,double(i-1),0},{1,0,0},.5});
        albert::Vehicle v(l);auto w=Scene::create();w->background=Color(0);w->add(v.node());v.node()->rotation.z=.2f;
        std::vector<float> state(16);state[0]=1;state[3]=.4f;state[1]=.06f;state[5]=-.12f;state[9]=101325;state.back()=-1;v.animate(&state,0);v.animate(&state,2);
        auto c=OrthographicCamera::create(-10,10,6,-20,.01,200);c->position.set(20,-5,60);c->lookAt(Vector3(0,-5,0));
        renderer.render(*w,*c);const auto batched=renderer.readRGBPixels();
        v.node()->traverseType<Mesh>([](Mesh& m){if(m.name.starts_with("engine exhaust batch"))m.visible=false;if(m.material()->name=="albert:plume")m.material()->visible=true;});
        renderer.render(*w,*c);const auto individual=renderer.readRGBPixels();double energy=0,error=0;
        for(std::size_t i=0;i<batched.size();++i){energy+=individual[i];error+=std::abs(int(individual[i])-int(batched[i]));}
        require(energy>1000&&error/energy<.035,"Cluster batching changed per-engine throttle, cutoff, gimbal or plume shading");
    }
    // Scene sync establishes obstruction before animation, and routes the ship's current flow to the receiving band.
    auto world=Scene::create();world->background=Color(0);albert::SceneObjects objects(*world);albert::core::Scene registry;
    auto boosterLook=std::make_shared<albert::core::VehicleLook>(look);boosterLook->length=72;boosterLook->sections={{0,2,4.5,4.5},{2,72,4.5,4.5}};
    for(auto& x:boosterLook->parts)x.hinge.x=1;for(auto& x:boosterLook->engines)x.position.x=72;
    auto shipLook=std::make_shared<albert::core::VehicleLook>();shipLook->length=52;shipLook->diameter=9;shipLook->sections={{0,52,4.5,4.5}};
    for(int i=0;i<6;++i)shipLook->engines.push_back({{52,double(i-3),0},{1,0,0},.7});
    auto add=[&](const char* id,std::shared_ptr<albert::core::VehicleLook> l,double height,int count){albert::core::Object o;o.id=id;o.name=id;o.providerManaged=true;o.look=l;
        albert::core::Sample s{10,{0,0,height},{},albert::core::Quaternion{std::sqrt(.5),0,0,std::sqrt(.5)}};
        auto v=std::make_shared<std::vector<float>>(l->engines.size()*3+7);for(int i=0;i<count;++i)(*v)[3*i]=.8f;(*v)[v->size()-7]=20;v->back()=-1;s.parts=v;o.samples={s};registry.create(o);};
    add("booster",boosterLook,60036,3);add("ship",shipLook,60098,6);objects.quality(2);objects.sync(registry,10,{});
    auto* jets=objects.nodes().at("booster")->getObjectByName("hot-staging vent jets");auto* shipBloom=objects.nodes().at("ship")->getObjectByName("rarefied exhaust");
    require(jets->visible&&!shipBloom->visible,"First paused staging sample lost the vent jets or fired a plume through the booster");
    auto camera=OrthographicCamera::create(-24.f/6378137,24.f/6378137,18.f/6378137,-18.f/6378137,.01f/6378137,200.f/6378137);
    camera->position.set(60.f/6378137,60071.f/6378137,90.f/6378137);camera->lookAt(Vector3(0,60071.f/6378137,0));
    renderer.render(*world,*camera);const auto lit=renderer.readRGBPixels();jets->visible=false;renderer.render(*world,*camera);require(lit!=renderer.readRGBPixels(),"Hot-staging jets did not reach the framebuffer");
    objects.quality(0);objects.sync(registry,10,{});require(jets->visible,"Low quality lost the staging event");
    auto s=registry.evaluate("ship",10).state;auto off=std::make_shared<std::vector<float>>(*s.parts);for(int i=0;i<6;++i)(*off)[3*i]=0;s.parts=off;registry.replace("ship",{s});objects.sync(registry,10,{});require(!jets->visible,"Dark ship invented hot staging");
    for(int i=0;i<6;++i)(*off)[3*i]=.8f;s.positionMeters.z+=100;registry.replace("ship",{s});objects.sync(registry,9,{});require(!jets->visible&&shipBloom->visible,"Separation/rewind retained vent feed or suppressed free ship exhaust");
}
#ifdef ALBERT_TEST_RPC
void ascentCaptures(const std::string& directory){
    namespace ex=albert::example::starship;using M=albert::core::CameraMode;std::filesystem::create_directories(directory);
    CameraFixture f;GpuTarget target(1280,720);target.scene.toneMapping=true;target.scene.samples=4;f.objects.quality(2);f.objects.cloudQuality(2);f.objects.humidity(.95f,{0,0,1});
    albert::gpu::Atmosphere atmosphere(target.context,albert::gpu::earthAtmosphere());atmosphere.precompute(target.context);
    auto planet=Object3D::create();planet->position.y=-1;f.world->add(planet);target.scene.air={&atmosphere,planet.get(),6378.137f,1};
    auto skyMaterial=ShaderMaterial::create();skyMaterial->name="albert:atmosphere";skyMaterial->side=Side::Back;skyMaterial->depthWrite=false;skyMaterial->uniforms={{"visibility",Uniform(1.f)}};
    auto sky=Mesh::create(SphereGeometry::create(2,32,16),skyMaterial);sky->frustumCulled=false;f.world->add(sky);
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(-.1f,.2f,.12f);f.world->add(sun);
    auto ground=Mesh::create(PlaneGeometry::create(200000.f/6378137,200000.f/6378137),albert::assistedMaterial(Color(.12f,.13f,.1f),0,1));ground->rotation.x=-1.5707963f;f.world->add(ground);
    auto sea=Mesh::create(PlaneGeometry::create(100000.f/6378137,200000.f/6378137),albert::assistedMaterial(Color(.05f,.16f,.23f),.2f,.4f));sea->rotation.x=-1.5707963f;sea->position.set(51000.f/6378137,.1f/6378137,0);f.world->add(sea);
    albert::LaunchCloud cloud({0,0,0},{0,1,0});cloud.node()->scale.setScalar(1.f/6378137);f.world->add(cloud.node());
    cloud.update(1,41,0,{2,0,0},true);for(int i=0;i<12;++i)cloud.update(1,41,1,{2,0,0},true);
    const auto stack=captureLook(ex::stack()),booster=captureLook(ex::superHeavy()),ship=captureLook(ex::shipAlone());
    auto camera=PerspectiveCamera::create(40,16.f/9,.05f/6378137,10);
    std::ofstream manifest(directory+"/manifest.csv");manifest<<"capture,quality,draws,fov,pressure_Pa,Mach,booster_engines,ship_engines,gap_m\n";
    auto sample=[&](const std::string& id,int count,float mach){auto s=f.registry.evaluate(id,f.utc).state;const auto& l=*f.registry.get(id).look;unsigned surfaces=0;for(const auto& p:l.parts)surfaces=std::max(surfaces,p.surface);
        auto p=std::make_shared<std::vector<float>>(l.engines.size()*3+surfaces+l.thrusters.size()+7);for(int i=0;i<count;++i)(*p)[3*i]=.85f;
        (*p)[p->size()-7]=float(albert::physics::standardAtmosphere(s.positionMeters.z).pressure);(*p)[p->size()-5]=.6f;(*p)[p->size()-4]=mach;p->back()=-1;s.parts=p;f.registry.replace(id,{s});};
    auto pose=[&](const albert::core::VehicleLook& l,albert::core::Vector position,int count,float mach){*f.look=l;f.vehicle();f.utc+=10;auto s=f.registry.evaluate("flight",f.utc).state;s.utcSeconds=f.utc;s.positionMeters=position;s.velocityMetersPerSecond={100,0,mach*300};s.hasVelocity=false;f.registry.replace("flight",{s});sample("flight",count,mach);};
    auto sync=[&](){f.objects.sync(f.registry,f.utc,{});};
    auto tracking=[&](){albert::BroadcastCamera rig;rig.select(M::Tracking);const auto view=rig.update(f.registry,f.objects,"flight",f.utc,1.f/60);require(bool(view),"Ascent shore camera missing");
        camera->position.copy(view->eye);camera->up.copy(view->up);camera->lookAt(view->aim);camera->fov=view->fov;camera->updateProjectionMatrix();};
    auto close=[&](albert::core::Vector eye,albert::core::Vector aim,float fov=36){camera->position.copy(albert::renderPosition(eye));camera->up.set(0,1,0);camera->lookAt(albert::renderPosition(aim));camera->fov=fov;camera->updateProjectionMatrix();};
    auto shot=[&](const std::string& name,int quality,int count,int upper,float gap){target.render(*f.world,*camera);const auto pixels=target.readRGBPixels();const auto path=directory+"/"+name+".ppm";
        std::ofstream out(path,std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);
        const auto p=f.registry.evaluate("flight",f.utc).state.parts;manifest<<name<<','<<quality<<','<<target.scene.drawn()<<','<<camera->fov<<','<<(*p)[p->size()-7]<<','<<(*p)[p->size()-4]<<','<<count<<','<<upper<<','<<gap<<'\n';std::cout<<path<<", "<<target.scene.drawn()<<" draws"<<std::endl;};
    pose(stack,{0,0,1000+stack.length/2},33,.3f);sync();tracking();shot("liftoff-1km-shore-tracking",2,33,0,0);
    cloud.node()->visible=false;ground->visible=sea->visible=false;pose(stack,{1200,0,11000},33,1.2f);sync();close({1310,-170,11025},{1200,0,11018});shot("max-q-vapor",2,33,0,0);
    f.objects.humidity(.1f,{0,0,1});sync();shot("max-q-dry-control",2,33,0,0);f.objects.humidity(.95f,{0,0,1});
    pose(stack,{35000,0,40000},33,5);sun->position.set(-.1f,-.005f,.12f);sync();tracking();shot("plume-40km-ground-dusk",2,33,0,0);
    for(int quality:{1,0}){f.objects.quality(quality);f.objects.cloudQuality(quality);sync();shot("plume-40km-ground-dusk-"+std::to_string(quality),quality,33,0,0);}f.objects.quality(2);f.objects.cloudQuality(2);
    sun->position.set(-.1f,.2f,.12f);pose(booster,{0,0,60000},3,6);
    auto upper=f.registry.get("flight");upper.id="flight.ship";upper.look=std::make_shared<const albert::core::VehicleLook>(ship);upper.samples.front().positionMeters.z+=(booster.length+ship.length)/2;f.registry.create(upper);sample("flight.ship",6,6);sync();
    close({65,-90,60000+booster.length/2},{0,0,60000+booster.length/2},28);shot("hot-staging-close",2,3,6,0);
    auto* jets=f.objects.nodes().at("flight")->getObjectByName("hot-staging vent jets");jets->visible=false;shot("hot-staging-jets-off-control",2,3,6,0);sync();
    for(int quality:{1,0}){f.objects.quality(quality);f.objects.cloudQuality(quality);sync();shot("hot-staging-close-"+std::to_string(quality),quality,3,6,0);}f.objects.quality(2);f.objects.cloudQuality(2);
    auto s=f.registry.evaluate("flight.ship",f.utc).state;s.positionMeters.z+=35;f.registry.replace("flight.ship",{s});f.utc+=1;sync();
    close({175,-300,60040},{0,0,60020},42);shot("just-after-separation",2,3,6,35);
    auto b=f.registry.evaluate("flight",f.utc).state;Quaternion tilt;tilt.setFromAxisAngle(Vector3(0,0,1),1.1f);Quaternion q(std::sqrt(.5f),0,0,std::sqrt(.5f));q.multiply(tilt);b.attitude=albert::core::Quaternion{q.x,q.y,q.z,q.w};f.registry.replace("flight",{b});
    s.positionMeters.z+=350;s.positionMeters.x+=250;f.registry.replace("flight.ship",{s});f.utc+=1;sync();require(std::abs(Vector3(0,1,0).applyQuaternion(f.objects.nodes().at("flight")->quaternion).y)<.6f,"Flip fixture rolled about the nose instead of tilting");close({150,-300,60045},{0,0,60015},38);shot("booster-flip",2,3,6,385);
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<std::endl;require(albert::gpu::errors()==0,"Ascent captures produced GPU validation errors");
}
#endif
