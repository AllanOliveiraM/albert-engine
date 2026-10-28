// Prescribed state exercises the renderer without changing a flight's dynamics or guidance.
void returnGraphics(Target& renderer){
    albert::core::VehicleLook look;look.length=32;look.diameter=6;look.sections={{0,32,3,3}};look.tanks={{2,30,false}};
    for(int i=0;i<3;++i)look.engines.push_back({{32,double(i-1),0},{1,0,0},.6});
    look.parts.push_back({"grid fin",{4,0,3},{0,1,0},{0,0,1},3,4,.1,1,0,0,0x505050,true});
    albert::Vehicle vehicle(look);auto world=Scene::create();world->background=Color(0);world->add(vehicle.node());
    auto ambient=AmbientLight::create(Color(0xffffff),1);world->add(ambient);
    auto camera=OrthographicCamera::create(-16,16,22,-22,.1,200);camera->position.set(35,3,60);camera->lookAt(Vector3());
    std::vector<float> p(16);p[9]=80000;p[11]=.3f;p[12]=.9f;p[13]=3.14159265f;p.back()=-1;
    auto* mist=vehicle.node()->getObjectByName("transonic condensation")->as<Mesh>();auto* bloom=vehicle.node()->getObjectByName("rarefied exhaust");
    auto* burn=vehicle.node()->getObjectByName("return ignition and collar")->as<Mesh>();
    vehicle.humidity(.95f);vehicle.animate(&p,0);renderer.render(*world,*camera);const auto humid=renderer.readRGBPixels();
    // Isolate the vapour: white above the shoulder, with no opaque smear halfway along the steel.
    std::vector<std::pair<Object3D*,bool>> visible;for(auto* child:vehicle.node()->children){visible.emplace_back(child,child->visible);if(child!=mist)child->visible=false;}
    camera->position.set(0,0,60);camera->lookAt(Vector3());renderer.render(*world,*camera);const auto cone=renderer.readRGBPixels();int crown=0,middleWhite=0;
    for(int y=0;y<192;++y)for(int x=0;x<192;++x){const auto k=3*(y*192+x);const bool white=cone[k]>150&&cone[k+1]>150&&cone[k+2]>150;
        crown+=white&&y>140;middleWhite+=white&&y>65&&y<120;}
    require(crown>25&&middleWhite<crown/4,"Transonic vapour lost its white shoulder cone or smeared along the middle");
    for(const auto& [node,on]:visible)node->visible=on;camera->position.set(35,3,60);camera->lookAt(Vector3());
    vehicle.humidity(.1f);vehicle.animate(&p,0);renderer.render(*world,*camera);require(renderer.readRGBPixels()!=humid,"Humidity did not change condensation");
    require(!mist->visible,"Dry air retained condensation");
    vehicle.humidity(.95f);p[12]=.79f;vehicle.animate(&p,1);
    require(mist->material()->as<ShaderMaterial>()->uniforms.at("state").value<Vector4>().x==0,"Subsonic hull retained shock condensation");
    p[12]=1.1f;vehicle.animate(&p,1.1);require(mist->material()->as<ShaderMaterial>()->uniforms.at("state").value<Vector4>().x==0,"Transonic shoulder cloud outlived the narrow Mach crossing");
    for(int i=0;i<3;++i)p[3*i]=.7f;p[12]=.85f;vehicle.animate(&p,2);vehicle.animate(&p,2.2);
    require(burn->visible&&burn->material()->as<ShaderMaterial>()->uniforms.at("state").value<Vector4>().x>.1f,"Observed engine starts lost their fuel-rich flash");
    renderer.render(*world,*camera);const auto retro=renderer.readRGBPixels();burn->visible=false;renderer.render(*world,*camera);
    require(renderer.readRGBPixels()!=retro,"Retropropulsion volume did not render");
    vehicle.animate(&p,3.2);require(burn->material()->as<ShaderMaterial>()->uniforms.at("state").value<Vector4>().x==0,"Ignition flash did not end");
    p[13]=0;vehicle.animate(&p,3.3);require(!burn->visible,"Forward flight produced a retropropulsion collar");
    p[9]=200;vehicle.animate(&p,4);const float width=bloom->scale.x;p[9]=2;vehicle.animate(&p,5);
    require(bloom->visible&&bloom->scale.x>width*2,"Rarefied exhaust did not expand with ambient pressure");
    vehicle.quality(0);vehicle.animate(&p,6);require(!burn->visible&&!mist->visible&&bloom->visible,"Low quality escaped the single-shell budget");
    vehicle.quality(2);for(int i=0;i<3;++i)p[3*i]=0;p[9]=200;p[12]=.2f;vehicle.animate(&p,7);vehicle.animate(&p,8);
    require(mist->visible&&!bloom->visible,"Coasting cold tanks did not vent or burning gas survived cutoff");
    vehicle.animate(&p,1);require(mist->material()->as<ShaderMaterial>()->uniforms.at("state").value<Vector4>().z==0,"Rewind retained pressure-management history");
    p[9]=80000;p[12]=.85f;p[13]=3.14159265f;for(int i=0;i<3;++i)p[3*i]=.7f;vehicle.animate(&p,10);vehicle.animate(&p,10.2);
    for(auto* child:vehicle.node()->children)if(child!=burn)child->visible=false;
    renderer.render(*world,*camera);const auto metres=renderer.readRGBPixels();
    const float units=1.f/6378137;vehicle.node()->scale.setScalar(units);camera->position.multiplyScalar(units);camera->left*=units;camera->right*=units;camera->top*=units;camera->bottom*=units;camera->nearPlane*=units;camera->farPlane*=units;camera->updateProjectionMatrix();camera->lookAt(Vector3());
    renderer.render(*world,*camera);const auto scaled=renderer.readRGBPixels();double a=0,b=0;for(std::size_t i=0;i<metres.size();++i){a+=metres[i];b+=scaled[i];}
    require(a>1000&&std::abs(b/a-1)<.03,"Return volume depends on scene units");
}
#ifdef ALBERT_TEST_RPC
void returnCaptures(const std::string& directory){
    namespace ex=albert::example::starship;std::filesystem::create_directories(directory);GpuTarget target(960,720);target.scene.toneMapping=true;target.scene.samples=4;
    albert::gpu::Atmosphere atmosphere(target.context,albert::gpu::earthAtmosphere());atmosphere.precompute(target.context);
    auto world=Scene::create();world->background=Color(0);auto planet=Object3D::create();planet->position.y=-6378137;world->add(planet);target.scene.air={&atmosphere,planet.get(),.001f,1};
    auto skyMaterial=ShaderMaterial::create();skyMaterial->name="albert:atmosphere";skyMaterial->side=Side::Back;skyMaterial->depthWrite=false;skyMaterial->uniforms={{"visibility",Uniform(1.f)}};
    auto sky=Mesh::create(SphereGeometry::create(250000,32,16),skyMaterial);sky->frustumCulled=false;world->add(sky);
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(-1000,-65,-200);world->add(sun);
    // Local pad ground belongs only in the low-altitude fixture; a huge flat patch invented a floor in coast shots.
    auto ground=Mesh::create(PlaneGeometry::create(300000,300000),albert::assistedMaterial(Color(.13f,.12f,.1f),0,1));ground->rotation.x=-1.5707963f;ground->visible=false;world->add(ground);
    albert::v1::CreateFlightRequest request;ex::addTower(request,true);albert::core::Scenery scenery;std::vector<float> angles;
    auto cv=[](const auto& v){return albert::core::Vector{v.x(),v.y(),v.z()};};
    for(const auto& p:request.scenery()){albert::core::SceneryPart q;q.name=p.name();q.centre=cv(p.centre_enu_meters());q.size=cv(p.size_meters());q.heading=p.heading_degrees()*ex::deg;q.tilt=p.tilt_degrees()*ex::deg;q.shape=static_cast<albert::core::SceneryPart::Shape>(p.shape());q.inner=p.inner_diameter_meters();q.color=p.color();q.structure=p.structure();scenery.parts.push_back(q);}
    for(const auto& b:request.structures()){albert::core::SceneryBar q;q.from=cv(b.start_enu_meters());q.to=cv(b.end_enu_meters());q.radius=b.has_drawn()&&!b.drawn()?0:b.radius_meters();q.hinged=b.has_hinge_enu_meters();q.pivot=cv(b.hinge_enu_meters());q.axis=b.has_hinge_axis()?cv(b.hinge_axis()):albert::core::Vector{0,0,1};scenery.bars.push_back(q);angles.push_back(b.start_radians());}
    albert::Site site(scenery);site.node()->rotation.x=-1.5707963f;site.animate(&angles);world->add(site.node());
    const auto boosterLook=captureLook(ex::superHeavy()),shipLook=captureLook(ex::shipAlone());albert::Vehicle booster(boosterLook),ship(shipLook);world->add(booster.node());world->add(ship.node());ship.node()->visible=false;
    booster.quality(2);ship.quality(2);booster.humidity(.95f);ship.humidity(.95f);
    auto camera=PerspectiveCamera::create(36,4.f/3,.05f,500000);
    auto shot=[&](const std::string& name){target.render(*world,*camera);const auto pixels=target.readRGBPixels();const auto path=directory+"/"+name+".ppm";std::ofstream out(path,std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
        for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);std::cout<<path<<", "<<target.scene.drawn()<<" draws\n";};
    auto sample=[&](albert::Vehicle& v,const auto& look,int count,float height,float speed,float fill,double utc){unsigned surfaces=0;for(const auto& p:look.parts)surfaces=std::max(surfaces,p.surface);
        std::vector<float> p(look.engines.size()*3+surfaces+look.thrusters.size()+7);for(int i=0;i<count;++i)p[3*i]=.75f;
        const auto air=albert::physics::standardAtmosphere(height);p[p.size()-7]=float(air.pressure);p[p.size()-5]=fill;p[p.size()-4]=speed/float(air.sound);p[p.size()-3]=3.14159265f;p.back()=-1;
        v.animate(&p,utc);};
    auto pose=[&](float height){booster.node()->position.set(0,height+float(boosterLook.length/2),0);};
    pose(75000);booster.node()->rotation.z=.45f;sample(booster,boosterLook,13,75000,1300,.3f,0);sample(booster,boosterLook,13,75000,1300,.3f,2);
    camera->fov=4;camera->updateProjectionMatrix();camera->position.set(18000,3,28000);camera->lookAt(Vector3(0,74400,0));shot("boostback-ground-dusk");
    booster.quality(1);shot("boostback-ground-dusk-medium");booster.quality(0);shot("boostback-ground-dusk-low");booster.quality(2);
    booster.node()->rotation.z=0;sun->position.set(-350,130,-250);camera->fov=36;camera->updateProjectionMatrix();
    pose(50000);sample(booster,boosterLook,0,50000,400,.3f,3);sample(booster,boosterLook,0,49500,400,.298f,5);camera->position.set(85,50050,140);camera->lookAt(Vector3(0,50038,0));require(!ground->visible,"Coast fixture retained its flat pad plane");shot("coast-vent-puffs");
    pose(11000);sample(booster,boosterLook,0,11000,298,.27f,10);camera->position.set(85,11045,140);camera->lookAt(Vector3(0,11041,0));shot("transonic-humid");booster.humidity(.1f);sample(booster,boosterLook,0,11000,298,.27f,10);shot("transonic-dry");booster.humidity(.95f);
    ground->visible=true;pose(1200);sample(booster,boosterLook,0,1200,300,.25f,20);sample(booster,boosterLook,13,1200,300,.25f,20.1);sample(booster,boosterLook,13,1200,300,.25f,20.32);
    camera->position.set(90,1228,125);camera->lookAt(Vector3(0,1215,0));shot("landing-ignition-1200m");
    camera->fov=6;camera->updateProjectionMatrix();camera->position.set(-900,3,-1200);camera->lookAt(Vector3(0,1225,0));shot("landing-ignition-ground-tracking");
    camera->fov=36;camera->updateProjectionMatrix();camera->position.set(90,1228,125);camera->lookAt(Vector3(0,1215,0));
    sample(booster,boosterLook,13,1200,250,.24f,22);shot("retropropulsion-250ms");
    sample(booster,boosterLook,13,1200,250,.24f,22.07);shot("retropropulsion-250ms-next");
    camera->position.set(-90,1228,-125);camera->lookAt(Vector3(0,1215,0));shot("retropropulsion-250ms-reverse");camera->position.set(90,1228,125);camera->lookAt(Vector3(0,1215,0));
    sample(booster,boosterLook,5,500,100,.2f,23);pose(500);camera->position.set(75,530,110);camera->lookAt(Vector3(0,520,0));shot("landing-5-engines");
    sun->position.set(-350,-12,-250);pose(75);sample(booster,boosterLook,3,75,12,.16f,24);sample(booster,boosterLook,3,75,12,.16f,26);
    camera->position.set(85,120,140);camera->lookAt(Vector3(-5,105,0));shot("landing-3-near-tower");
    sample(booster,boosterLook,0,75,0,.16f,27);sample(booster,boosterLook,0,75,0,.16f,33.5);sun->position.set(-350,120,-250);
    camera->position.set(105,135,150);camera->lookAt(Vector3(0,114,0));shot("post-catch-haze");
    booster.node()->visible=false;ship.node()->visible=true;ship.node()->position.set(0,1200+float(shipLook.length/2),0);ship.node()->rotation.z=.45f;
    sample(ship,shipLook,0,1200,280,.2f,40);sample(ship,shipLook,3,1200,280,.2f,40.1);sample(ship,shipLook,3,1200,280,.2f,40.32);
    camera->position.set(80,1220,105);camera->lookAt(Vector3(0,1215,0));shot("ship-flip-ignition-3");
    sample(ship,shipLook,3,1200,250,.2f,42);shot("ship-flip-collar-250ms");
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<'\n';require(albert::gpu::errors()==0,"Return captures produced GPU validation errors");
}
#endif
