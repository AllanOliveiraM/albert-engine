// Included after the offscreen target: surface regressions and optional captures of the real
// example appearance. No flight is advanced; engine flow, fill and heat are explicit test samples.
void vehicleGraphics(Target& renderer){
    albert::core::VehicleLook look;look.length=30;look.diameter=9;look.sections.push_back({0,30,4.5,4.5,0xc9c9c6,3.14159265359});
    look.tanks.push_back({5,27,false});look.engines.push_back({{30,0,0},{1,0,0},.65});
    albert::Vehicle vehicle(look);auto world=Scene::create();world->background=Color(0);world->add(vehicle.node());
    auto sun=DirectionalLight::create(Color(0xffffff),1);sun->position.set(20,30,50);world->add(sun);
    auto ambient=AmbientLight::create(Color(0xc8dcff),.7f);world->add(ambient);
    auto camera=OrthographicCamera::create(-8,8,17,-17,.1,200);camera->position.set(0,0,60);camera->lookAt(Vector3());
    std::vector<float> sample(10);sample[3]=101325;sample.back()=-1;vehicle.animate(&sample,0);
    renderer.render(*world,*camera);const auto tiles=renderer.readRGBPixels();
    auto energy=[](const auto& pixels){double sum=0;for(auto p:pixels)sum+=p;return sum;};
    camera->position.z=-60;camera->lookAt(Vector3());sun->position.z=-50;
    renderer.render(*world,*camera);const auto steel=renderer.readRGBPixels();
    std::cout<<"Vehicle steel/tile image energy "<<energy(steel)<<" / "<<energy(tiles)<<'\n';
    require(energy(steel)>energy(tiles)*1.7,"Windward tiles lost their contrast with leeward stainless");
    vehicle.quality(0);renderer.render(*world,*camera);const auto low=renderer.readRGBPixels();require(low!=steel,"Vehicle low quality did not simplify surface detail");
    vehicle.quality(1);renderer.render(*world,*camera);require(renderer.readRGBPixels()==steel,"Vehicle quality failed to restore its surface");
    sample[5]=1;vehicle.animate(&sample,1);renderer.render(*world,*camera);const auto full=renderer.readRGBPixels();
    sample[5]=.3f;vehicle.animate(&sample,2);renderer.render(*world,*camera);const auto draining=renderer.readRGBPixels();
    require(energy(full)>energy(draining)*1.1&&energy(draining)>energy(steel)*1.05,"Frost did not follow the supplied tank fill");
    sample[5]=0;vehicle.animate(&sample,3);renderer.render(*world,*camera);require(renderer.readRGBPixels()==steel,"Empty tanks retained frost");
    sun->intensity=0;ambient->intensity=0;renderer.render(*world,*camera);require(energy(renderer.readRGBPixels())<1,"Stainless emits light without illumination");
    camera->position.z=60;camera->lookAt(Vector3());sample[4]=4e5;vehicle.animate(&sample,4);
    // Isolate tile incandescence from the sheath and its reflected illumination.
    vehicle.node()->traverseType<Mesh>([](Mesh& mesh){auto* m=mesh.material()?mesh.material()->as<ShaderMaterial>():nullptr;
        if(m&&m->name=="albert:vehicle")m->uniforms.at("entryLight").setValue(Vector4(0,0,0,0));});
    for(auto* child:vehicle.node()->children)if(child->material()&&child->material()->name=="albert:plasma")child->visible=false;
    renderer.render(*world,*camera);const auto hot=renderer.readRGBPixels();require(energy(hot)>energy(tiles),"Procedural heat shield lost its thermal emission");
    camera->position.z=-60;camera->lookAt(Vector3());renderer.render(*world,*camera);
    std::cout<<"Tile-only hot/back energy "<<energy(hot)<<" / "<<energy(renderer.readRGBPixels())<<'\n';
    require(energy(renderer.readRGBPixels())<energy(hot)*.15,"Windward thermal glow leaked across the leeward steel");
    // Rotated and scaled as in the live scene: metre-sized relief must not become Earth-radius-sized bump.
    sample[4]=0;vehicle.animate(&sample,5);sun->intensity=1;ambient->intensity=.7f;
    vehicle.node()->scale.setScalar(1.f/6378137);camera->position.set(0,0,-60.f/6378137);camera->left=-8.f/6378137;camera->right=8.f/6378137;
    camera->top=17.f/6378137;camera->bottom=-17.f/6378137;camera->nearPlane=.1f/6378137;camera->farPlane=200.f/6378137;camera->updateProjectionMatrix();camera->lookAt(Vector3());
    renderer.render(*world,*camera);require(energy(renderer.readRGBPixels())>energy(steel)*.7,"Vehicle bump is not invariant to scene units");
    // A vent band is shaded even when low detail is selected, and retains the declared pivot count.
    look.sections={{0,2,4.5,4.5,0x3c3c3c,0},{2,30,4.5,4.5,0xc9c9c6,0}};look.parts.push_back({"vent",{1,0,4.5}});
    albert::Vehicle booster(look);world->remove(*vehicle.node());world->add(booster.node());booster.quality(0);
    camera->position.set(25,20,50);camera->left=-8;camera->right=8;camera->top=17;camera->bottom=-17;camera->nearPlane=.1;camera->farPlane=200;camera->updateProjectionMatrix();camera->lookAt(Vector3());
    renderer.render(*world,*camera);require(energy(renderer.readRGBPixels())>1000,"Hot-staging band failed to render at low quality");
    // In a stack only the ship is tiled; both sides of the booster's steel must still frost.
    look.sections={{0,15,4.5,4.5,0xc9c9c6,3.14159265359},{15,30,4.5,4.5,0xc9c9c6,0}};look.parts.clear();look.tanks={{17,28,false}};
    albert::Vehicle stacked(look);world->remove(*booster.node());world->add(stacked.node());camera->position.set(0,0,60);camera->lookAt(Vector3());sun->position.z=50;
    sample[5]=0;stacked.animate(&sample,0);renderer.render(*world,*camera);const double empty=energy(renderer.readRGBPixels());
    sample[5]=1;stacked.animate(&sample,1);renderer.render(*world,*camera);
    require(energy(renderer.readRGBPixels())>empty*1.2,"Ship tiles masked the booster's windward frost in a stack");
}

#ifdef ALBERT_TEST_RPC
albert::core::VehicleLook captureLook(const albert::v1::VehicleDefinition& definition,int stage=0){
    const auto& s=definition.stages(stage);const auto& a=s.appearance();albert::core::VehicleLook look;look.length=s.length_meters();look.diameter=definition.diameter_meters();
    auto cv=[](const auto& v){return albert::core::Vector{v.x(),v.y(),v.z()};};
    for(const auto& x:a.sections())look.sections.push_back({x.from_meters(),x.to_meters(),x.radius_from_meters(),x.radius_to_meters(),x.color(),x.tiles_radians()});
    for(const auto& x:a.parts()){look.parts.push_back({x.name(),cv(x.hinge_meters()),cv(x.axis()),cv(x.outward()),x.span_meters(),x.width_meters(),x.thickness_meters(),x.gain(),x.offset_radians(),x.surface(),x.color(),x.grid()});auto& p=look.parts.back();p.tipSpan=x.has_tip_span_meters()?x.tip_span_meters():-1;p.tipShift=x.tip_shift_meters();p.tipThickness=x.has_tip_thickness_meters()?x.tip_thickness_meters():-1;}
    for(const auto& t:a.tanks())look.tanks.push_back({t.from_meters(),t.to_meters(),t.full()});
    for(const auto& e:s.engines())look.engines.push_back({cv(e.position_meters()),e.has_direction()?cv(e.direction()):albert::core::Vector{1,0,0},std::sqrt(std::max(e.exit_area_meters2(),.01)/3.14159265359)});
    for(const auto& t:s.thrusters())look.thrusters.push_back({cv(t.position_meters()),cv(t.direction())});
    return look;
}
void vehicleCaptures(const std::string& directory){
    namespace ex=albert::example::starship;std::filesystem::create_directories(directory);GpuTarget target(1280,960);target.scene.toneMapping=true;target.scene.samples=4;target.scene.shadows=true;
    albert::gpu::Atmosphere atmosphere(target.context,albert::gpu::earthAtmosphere());atmosphere.precompute(target.context);
    auto world=Scene::create();world->background=Color(0x000000);auto planet=Object3D::create();planet->position.y=-6378137;world->add(planet);
    target.scene.air={&atmosphere,planet.get(),.001f,1};
    auto skyMaterial=ShaderMaterial::create();skyMaterial->name="albert:atmosphere";skyMaterial->side=Side::Back;skyMaterial->depthWrite=false;skyMaterial->uniforms={{"visibility",Uniform(1.f)}};
    auto sky=Mesh::create(SphereGeometry::create(1000,32,16),skyMaterial);sky->frustumCulled=false;world->add(sky);
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(-350,180,-250);sun->castShadow=true;sun->shadow->mapSize.set(2048,2048);
    auto& sh=static_cast<OrthographicCamera&>(*sun->shadow->camera);sh.left=sh.bottom=-180;sh.right=sh.top=180;sh.nearPlane=.1;sh.farPlane=2000;sh.updateProjectionMatrix();sun->shadow->normalBias=.02f;sun->shadow->bias=-.00005f;world->add(sun);
    auto ground=Mesh::create(PlaneGeometry::create(2000,2000),albert::assistedMaterial(Color(.13f,.12f,.10f),0,1));ground->rotation.x=-1.5707963f;ground->receiveShadow=true;world->add(ground);
    // The same scenery and hinge samples as the mounted flight client, in the capture's Y-up frame.
    albert::v1::CreateFlightRequest request;ex::addTower(request,true);albert::core::Scenery scenery;
    scenery.latitude=ex::towerLatitude*ex::deg;scenery.longitude=ex::towerLongitude*ex::deg;
    auto cv=[](const auto& v){return albert::core::Vector{v.x(),v.y(),v.z()};};
    for(const auto& p:request.scenery()){albert::core::SceneryPart q;q.name=p.name();q.centre=cv(p.centre_enu_meters());q.size=cv(p.size_meters());q.heading=p.heading_degrees()*ex::deg;q.tilt=p.tilt_degrees()*ex::deg;q.shape=static_cast<albert::core::SceneryPart::Shape>(p.shape());q.inner=p.inner_diameter_meters();q.color=p.color();q.structure=p.structure();scenery.parts.push_back(q);}
    std::vector<float> angles;
    for(const auto& b:request.structures()){albert::core::SceneryBar q;q.from=cv(b.start_enu_meters());q.to=cv(b.end_enu_meters());q.radius=b.has_drawn()&&!b.drawn()?0:b.radius_meters();q.hinged=b.has_hinge_enu_meters();q.pivot=cv(b.hinge_enu_meters());q.axis=b.has_hinge_axis()?cv(b.hinge_axis()):albert::core::Vector{0,0,1};scenery.bars.push_back(q);angles.push_back(b.start_radians());}
    albert::Site site(scenery);site.node()->rotation.x=-1.5707963f;site.animate(&angles);world->add(site.node());
    auto stackLook=captureLook(ex::stack()),shipLook=captureLook(ex::stack(),1),boosterLook=captureLook(ex::superHeavy());
    albert::Vehicle stack(stackLook),ship(shipLook),booster(boosterLook);world->add(stack.node());world->add(ship.node());world->add(booster.node());
    auto fill=[&](albert::Vehicle& v,const albert::core::VehicleLook& look,float f,float heat=0,float flow=0,double utc=0){unsigned surfaces=0;for(const auto& part:look.parts)surfaces=std::max(surfaces,part.surface);std::vector<float> p(look.engines.size()*3+surfaces+look.thrusters.size()+7);for(std::size_t i=0;i<look.engines.size();++i)p[3*i]=flow;for(unsigned i=0;i<surfaces;++i)p[look.engines.size()*3+i]=flow>0?0.f:.7853982f;p[p.size()-7]=101325;p[p.size()-6]=heat;p[p.size()-5]=f;p.back()=-1;v.animate(&p,utc);};
    fill(stack,stackLook,0);fill(ship,shipLook,0);fill(booster,boosterLook,0);stack.node()->position.y=float(stackLook.length/2+12);
    std::shared_ptr<Camera> camera;
    auto frame=[&](Vector3 aim,Vector3 eye,float halfHeight){auto c=OrthographicCamera::create(-halfHeight*4/3,halfHeight*4/3,halfHeight,-halfHeight,.05,2000);c->position.copy(eye);c->lookAt(aim);camera=c;};
    auto perspective=[&](Vector3 aim,Vector3 eye,float fov){auto c=PerspectiveCamera::create(fov,4.f/3,.03,2e7);c->position.copy(eye);c->lookAt(aim);camera=c;};
    auto show=[&](albert::Vehicle& v){stack.node()->visible=&v==&stack;ship.node()->visible=&v==&ship;booster.node()->visible=&v==&booster;};
    auto shot=[&](const char* name){target.render(*world,*camera);const auto pixels=target.readRGBPixels();const auto path=directory+"/"+name+".ppm";std::ofstream out(path,std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
        for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);std::cout<<path<<", "<<target.scene.drawn()<<" draws, GPU "<<target.scene.gpuMilliseconds()<<" ms\n";};
    show(stack);frame({0,76,0},{155,100,-210},80);shot("stack-day");
    fill(stack,stackLook,1);shot("stack-loaded");fill(stack,stackLook,0);
    sun->position.set(-450,45,-200);frame({0,75,0},{130,90,-230},80);shot("stack-sunset");
    sun->position.set(-350,180,-250);perspective({0,88,0},{-15,151,-7},82);shot("tower-top");
    show(booster);booster.node()->position.y=float(boosterLook.length/2+12);
    frame({0,57,0},{45,62,-70},42);shot("booster-clean");
    { // Same geometry, atmosphere and camera: isolate the surface shader from scenery, sky and shadow passes.
        sky->visible=ground->visible=site.node()->visible=false;target.scene.shadows=false;
        std::vector<ShaderMaterial*> materials;booster.node()->traverseType<Mesh>([&](Mesh& m){auto* s=m.material()->as<ShaderMaterial>();if(s&&s->name=="albert:vehicle")materials.push_back(s);});
        auto measure=[&](const char* label){std::vector<double> times;for(int i=0;i<12;++i){target.render(*world,*camera);target.readRGBPixels();const double ms=target.scene.gpuMilliseconds();if(i>=4&&ms>=0)times.push_back(ms);}
            std::sort(times.begin(),times.end());std::cout<<"Surface timing "<<label<<": median "<<(times.empty()?-1:times[times.size()/2])<<" ms, "<<target.scene.drawn()<<" draws\n";};
        for(auto* m:materials)m->name="albert:assisted";measure("standard lighting");
        for(auto* m:materials)m->name="albert:vehicle";measure("vehicle high");booster.quality(0);measure("vehicle low");booster.quality(1);
        sky->visible=ground->visible=site.node()->visible=true;target.scene.shadows=true;
    }
    fill(booster,boosterLook,0,0,1,0);for(int i=1;i<=10;++i)fill(booster,boosterLook,0,0,1,i*10);fill(booster,boosterLook,0,0,0,110);
    // Hide transient effects to inspect deposited soot without exhaust or thermal radiance.
    for(auto* c:booster.node()->children)if(c->name=="released exhaust"||c->name=="engine puff")c->visible=false;
    shot("booster-weathered");frame({0,18,0},{18,24,-26},10);shot("booster-base");
    fill(booster,boosterLook,1,0,0,111);frame({0,48,0},{50,52,-65},42);shot("booster-frost-full");
    fill(booster,boosterLook,.35f,0,0,112);shot("booster-frost-draining");fill(booster,boosterLook,0,0,0,113);
    frame({0,78,0},{18,83,-24},12);shot("hot-staging-fins");booster.quality(0);shot("hot-staging-fins-low");booster.quality(1);
    show(ship);site.node()->visible=false;ship.node()->position.y=32;frame({0,34,0},{30,40,-55},31);shot("ship-leeward");
    sun->position.set(350,180,250);frame({0,34,0},{30,40,55},31);shot("ship-windward");
    frame({4.4f,30,.2f},{22,32,5},5);shot("tile-edge");ship.quality(0);shot("tile-edge-low");ship.quality(1);
    ground->visible=sky->visible=false;perspective({0,4.0f,0},{4,6.75f,0},100);shot("raptor-bells");
    fill(ship,shipLook,0,0,1,0);fill(ship,shipLook,0,0,1,1);shot("raptor-burning");fill(ship,shipLook,0,0,0,2);
    fill(ship,shipLook,0,0,0,3);sun->position.set(-350,180,-250);
    sky->visible=true;planet->position.y=-6578137;sky->scale.setScalar(10000);ground->visible=false;ship.node()->position.y=0;ship.node()->rotation.z=1.5707963f;
    auto earthMaterial=MeshStandardMaterial::create();earthMaterial->color=Color(.04f,.18f,.3f);earthMaterial->roughness=1;
    try {albert::AssetPack pack("build/linux/imagery/visuals.pak");albert::DownloadProgress progress;auto image=pack.image("earth-day",{-180,-90,180,90},2048,progress);
        auto map=DataTexture::create(ImageData(std::move(image.data())),image.width(),image.height());map->colorSpace=ColorSpace::sRGB;map->minFilter=map->magFilter=Filter::Linear;earthMaterial->map=map;earthMaterial->color=Color(0xffffff);
    }catch(const std::exception& e){std::cout<<"Capture Earth uses mean colour: "<<e.what()<<'\n';}
    auto earth=Mesh::create(SphereGeometry::create(6378137,128,64),earthMaterial);earth->position.copy(planet->position);earth->rotation.set(.4f,.8f,1.f);world->add(earth);
    perspective({-13,0,0},{32,12,-13},72);shot("ship-orbit");
    world->remove(*earth);ground->visible=true;sky->scale.setScalar(1);planet->position.y=-6378137;ship.node()->position.y=32;ship.node()->rotation.z=0;
    sun->intensity=0;frame({0,34,0},{30,40,-55},31);shot("ship-night");
    fill(ship,shipLook,0,4e5,0,3);frame({0,34,0},{30,40,55},31);shot("tiles-entry-glow");
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<'\n';require(albert::gpu::errors()==0,"Vehicle captures produced GPU validation errors");
}
#endif
