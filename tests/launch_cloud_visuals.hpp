// Fixed render samples exercise the launch volume without running flight dynamics.
void launchCloudGraphics(Target& renderer){
    auto world=Scene::create();world->background=Color(0);auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(-800,-500,1200);world->add(sun);
    auto ambient=AmbientLight::create(Color(0xffffff),.2f);world->add(ambient);
    albert::LaunchCloud cloud({0,0,0},{0,1,0});world->add(cloud.node());
    auto camera=OrthographicCamera::create(-420,420,420,-420,.1f,3000);camera->up.set(0,1,0);camera->position.set(0,0,1500);camera->lookAt(Vector3());
    for(int i=0;i<60;++i)cloud.update(1,41,1.f/30,{0,0,0},true);
    renderer.render(*world,*camera);const auto early=renderer.readRGBPixels();
    for(int i=0;i<540;++i)cloud.update(i<180?1:0,41,1.f/30,{0,0,0},true);
    renderer.render(*world,*camera);const auto mature=renderer.readRGBPixels();
    auto coverage=[](const auto& p){return std::count_if(p.begin(),p.end(),[](auto x){return x>15;});};
    require(coverage(mature)>coverage(early)*3,"Launch cloud did not spread after the stack left the pad");
    cloud.update(0,0,0,{10,2,0},true);renderer.render(*world,*camera);require(renderer.readRGBPixels()==mature,"Paused launch volume changed");
    cloud.update(0,0,0,{0,0,0},false);renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())==0,"Deluge effects off retained a launch cloud");
    cloud.update(0,0,0,{0,0,0},true);renderer.render(*world,*camera);require(renderer.readRGBPixels()==mature,"Deluge toggle lost the stored cloud");
    auto inside=PerspectiveCamera::create(70,1,.1f,2000);inside->up.set(0,0,1);inside->position.set(0,-20,20);inside->lookAt(Vector3(0,80,25));
    renderer.render(*world,*inside);const auto interior=renderer.readRGBPixels();require(coverage(interior)>1000,"Camera entering the bank lost its volume");
    renderer.render(*world,*inside);require(renderer.readRGBPixels()==interior,"Stationary interior launch view flickered");
    constexpr float metres=6378137;world->scale.setScalar(1/metres);camera->position.z=1500/metres;camera->nearPlane=.1f/metres;
    camera->left=camera->bottom=-420/metres;camera->right=camera->top=420/metres;camera->updateProjectionMatrix();
    renderer.render(*world,*camera);const auto scaled=renderer.readRGBPixels();int changed=0;for(std::size_t i=0;i<mature.size();++i)changed+=std::abs(int(mature[i])-scaled[i])>10;
    require(changed<100,"Launch volume changed in Earth-radius scene units");
    world->scale.setScalar(1);camera->position.z=1500;camera->nearPlane=.1f;camera->left=camera->bottom=-420;camera->right=camera->top=420;camera->updateProjectionMatrix();
    const Vector3 before=cloud.node()->position;cloud.update(0,0,40,{4,-2,0},true);
    require(cloud.node()->position.x>before.x+80&&cloud.node()->position.y<before.y-40,"Cloud ignored the pad's wind");
    renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())>500,"Residual cloud disappeared before T+60");
    cloud.quality(0);renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())>500,"Low quality removed the launch volume");
    cloud.quality(2);renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())>500,"High quality removed the launch volume");
    cloud.reset();cloud.update(1,0,3,{0,0,0},true);sun->intensity=ambient->intensity=0;
    renderer.render(*world,*camera);const auto fire=renderer.readRGBPixels();int orange=0;
    for(std::size_t i=0;i<fire.size();i+=3)orange+=fire[i]>fire[i+2]+30&&fire[i]>50;
    require(orange>10,"Launch cloud lost its internal orange illumination in darkness");
    cloud.update(0,0,30,{0,0,0},true);renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())<20,"Residual cloud emitted light after the plume left");
    cloud.update(0,0,400,{0,0,0},true);require(!cloud.node()->visible,"Launch cloud did not expire");
    albert::core::Scenery scenery;for(double y:{-242.,-158.}){albert::core::SceneryPart v;v.name="steam vent";v.centre={700,y,3};v.size={18,1,6};scenery.parts.push_back(v);}
    albert::Site offset(scenery);offset.exhaust(1,13,1,41,{0,0,0});auto* bank=offset.node()->getObjectByName("launch cloud");
    require(bank&&bank->visible&&bank->position.x==700&&bank->position.y==-200,"Launch cloud ignored the active pad's vent locations");
    offset.exhaust(1,13,0,41,{0,0,0},false);require(!bank->visible,"Effects switch failed at a paused pad");offset.resetExhaust();
    offset.cloudQuality(2);offset.quality(0);require(bank->as<Mesh>()->material()->as<ShaderMaterial>()->uniforms.at("samples").value<float>()==12,"Global low quality did not cap launch sampling");
    offset.quality(1);require(bank->as<Mesh>()->material()->as<ShaderMaterial>()->uniforms.at("samples").value<float>()==32,"Global quality failed to restore launch sampling");
    std::cout<<"Launch cloud: radial coverage, pause, toggle, wind, residual, quality, internal light and expiry OK\n";
}
#ifdef ALBERT_TEST_RPC
void launchCloudCaptures(const std::string& directory){
    namespace ex=albert::example::starship;namespace api=albert::v1;std::filesystem::create_directories(directory);
    GpuTarget target(960,720);target.scene.toneMapping=true;target.scene.samples=1;target.scene.shadows=true;target.scene.shadowMetersPerUnit=1;
    albert::gpu::Atmosphere atmosphere(target.context,albert::gpu::earthAtmosphere());atmosphere.precompute(target.context);
    auto world=Scene::create();world->background=Color(0x89b9d6);auto planet=Object3D::create();planet->position.z=-6378137;world->add(planet);target.scene.air={&atmosphere,planet.get(),.001f,1};
    auto skyMaterial=ShaderMaterial::create();skyMaterial->name="albert:atmosphere";skyMaterial->side=Side::Back;skyMaterial->depthWrite=false;skyMaterial->uniforms={{"visibility",Uniform(1.f)}};
    auto sky=Mesh::create(SphereGeometry::create(2000000,32,16),skyMaterial);sky->frustumCulled=false;world->add(sky);
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(-6000,-5000,9000);sun->castShadow=true;world->add(sun);world->add(AmbientLight::create(Color(0xb8d4ed),.12f));
    api::CreateFlightRequest request;ex::addTower(request,true);auto scenery=std::make_shared<albert::core::Scenery>();scenery->latitude=ex::towerLatitude*ex::deg;scenery->longitude=ex::towerLongitude*ex::deg;
    auto cv=[](const auto& v){return albert::core::Vector{v.x(),v.y(),v.z()};};std::vector<float> angles;
    for(const auto& p:request.scenery()){albert::core::SceneryPart q;q.name=p.name();q.centre=cv(p.centre_enu_meters());q.size=cv(p.size_meters());q.heading=p.heading_degrees()*ex::deg;q.tilt=p.tilt_degrees()*ex::deg;
        q.shape=static_cast<albert::core::SceneryPart::Shape>(p.shape());q.inner=p.inner_diameter_meters();q.color=p.color();q.structure=p.structure();scenery->parts.push_back(q);}
    for(const auto& b:request.structures()){albert::core::SceneryBar q;q.from=cv(b.start_enu_meters());q.to=cv(b.end_enu_meters());q.radius=b.has_drawn()&&!b.drawn()?0:b.radius_meters();q.hinged=b.has_hinge_enu_meters();q.pivot=cv(b.hinge_enu_meters());q.axis=b.has_hinge_axis()?cv(b.hinge_axis()):albert::core::Vector{0,0,1};scenery->bars.push_back(q);angles.push_back(b.start_radians());}
    for(const auto& r:request.roads()){albert::core::SceneryRoad q;q.width=r.width_meters();q.surface=static_cast<albert::core::SceneryRoad::Surface>(r.surface());q.highway=r.highway();for(const auto& p:r.points_enu_meters())q.points.push_back(cv(p));scenery->roads.push_back(q);}
    scenery->buildings=productionBuildings(request);albert::Site site(*scenery);site.animate(&angles);world->add(site.node());
    albert::core::Scene registry;albert::core::Object o;o.id="site";o.name="Starbase";o.scenery=scenery;o.samples.emplace_back();registry.create(o);albert::Roads roads;roads.sync(registry);
    constexpr double a=6378137,e2=6.69437999014e-3;const double phi=scenery->latitude,n=a/std::sqrt(1-e2*std::pow(std::sin(phi),2)),mx=n*std::cos(phi),my=a*(1-e2)/std::pow(1-e2*std::pow(std::sin(phi),2),1.5);
    constexpr double west=-8608,east=2656,south=-6272,north=4992;albert::GeoRegion region{ex::towerLongitude+west/mx/ex::deg,ex::towerLatitude+south/my/ex::deg,ex::towerLongitude+east/mx/ex::deg,ex::towerLatitude+north/my/ex::deg};
    albert::DownloadProgress progress;albert::AssetPack pack("build/linux/imagery/visuals.pak");auto image=pack.image("earth-day",region,2048,progress);
    auto map=DataTexture::create(ImageData(std::move(image.data())),image.width(),image.height());map->colorSpace=ColorSpace::sRGB;map->minFilter=map->magFilter=Filter::Linear;
    auto base=MeshStandardMaterial::create();base->map=map;base->roughness=1;
    auto geometry=PlaneGeometry::create(east-west,north-south,256,256);geometry->translate((west+east)/2,(south+north)/2,0);auto* pos=geometry->getAttribute<float>("position");auto* uv=geometry->getAttribute<float>("uv");std::vector<float> global,residual;
    for(int i=0;i<pos->count();++i){const double x=pos->getX(i),y=pos->getY(i),u=(scenery->longitude+std::numbers::pi+x/mx)/(2*std::numbers::pi),v=(scenery->latitude+y/my)/std::numbers::pi+.5;
        global.insert(global.end(),{float(u),float(v)});residual.insert(residual.end(),{float(u-float(u)),float(v-float(v))});uv->setXY(i,(x-west)/(east-west),(y-south)/(north-south));}
    geometry->setAttribute("uv2",FloatBufferAttribute::create(std::move(global),2));geometry->setAttribute("geoResidual",FloatBufferAttribute::create(std::move(residual),2));
    auto ground=Mesh::create(geometry,roads.material(base,1));ground->receiveShadow=true;ground->frustumCulled=false;world->add(ground);
    const auto boosterLook=captureLook(ex::stack());albert::Vehicle booster(boosterLook);booster.node()->rotation.x=std::numbers::pi_v<float>/2;world->add(booster.node());
    auto camera=PerspectiveCamera::create(55,4.f/3,.1f,100000);camera->up.set(0,0,1);
    auto shot=[&](const std::string& name){site.view(*camera);site.lighting(sun->position);const auto start=std::chrono::steady_clock::now();target.render(*world,*camera);const auto pixels=target.readRGBPixels();
        std::ofstream out(directory+"/"+name+".ppm",std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);
        std::cout<<name<<", draws "<<target.scene.drawn()<<", GPU ms "<<target.scene.gpuMilliseconds()<<", synchronized ms "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;};
    auto frame=[&](Vector3 eye,Vector3 aim,float fov){camera->position.copy(eye);camera->fov=fov;camera->up.set(0,0,1);if(eye.x==aim.x&&eye.y==aim.y)camera->up.set(0,1,0);camera->lookAt(aim);camera->updateProjectionMatrix();};
    double time=-3;auto step=[&](double end){while(time<end-.0001){const float dt=std::min(1.f/30,static_cast<float>(end-time));time+=dt;const float elapsed=static_cast<float>(std::max(0.,time)),altitude=13+1.15f*elapsed*elapsed;
            site.exhaust(std::clamp(static_cast<float>((time+3)/2),0.f,1.f),altitude,dt,time<25?41:0,{3,-1,0});}
        const float altitude=13+1.15f*float(time*time);booster.node()->position.z=altitude+float(boosterLook.length/2);
        std::vector<float> parts(boosterLook.engines.size()*3+7);for(std::size_t i=0;i<boosterLook.engines.size();++i)parts[3*i]=1;parts[parts.size()-7]=101325*std::exp(-altitude/8500);parts.back()=-1;booster.animate(&parts,time);};
    for(int t:{2,8,20,60}){step(t);const std::string prefix="t"+std::to_string(t)+"-";
        frame({0,0,1300},{0,0,0},52);shot(prefix+"above");frame({600,-650,500},{0,0,70},53);shot(prefix+"drone-500m");frame({750,-661.44f,1.7f},{0,0,65},43);shot(prefix+"eye-1km");
        if(t==2||t==8){frame({-10,-16,151},{12,28,66},95);shot(prefix+"tower");}
    }
    frame({600,-650,500},{0,0,70},53);
    for(int quality:{0,1,2}){site.cloudQuality(quality);shot("cost-"+std::to_string(quality)+"-warmup");for(int i=0;i<3;++i)shot("cost-"+std::to_string(quality)+"-"+std::to_string(i));}
    auto* bank=site.node()->getObjectByName("launch cloud");bank->visible=false;shot("cost-cloud-hidden");bank->visible=true;
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<std::endl;require(albert::gpu::errors()==0,"Launch captures produced GPU validation errors");
}
#endif
