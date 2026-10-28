void splashdownControls(){
    albert::core::Scene registry;auto world=Scene::create();albert::SceneObjects objects(*world);
    albert::core::Object o;o.id="sea-ship";o.name="Sea ship";o.providerManaged=true;auto look=std::make_shared<albert::core::VehicleLook>();look->length=50;look->diameter=9;look->sections.push_back({0,50,4.5,4.5,0xdddddd,0});look->engines.push_back({{50,0,0},{1,0,0},.5});o.look=look;
    albert::core::Sample a(0,{0,0,85},{},albert::core::Quaternion{std::sqrt(.5),0,0,std::sqrt(.5)});auto parts=std::make_shared<std::vector<float>>(10);(*parts)[0]=.6f;(*parts)[3]=101325;parts->back()=-1;a.parts=parts;o.samples={a};registry.create(o);
    objects.sync(registry,0,{});require(!world->getObjectByName("sea response"),"Dry land created splashdown effects");
    a.water=albert::core::WaterSurface{{0,0,0},{1,0,0},{0,1,0},{2,-1,0},false};auto b=a;b.utcSeconds=8;registry.replace(o.id,{a,b});objects.sync(registry,0,{});objects.sync(registry,8,{});
    auto* response=world->getObjectByName("sea response");require(response&&response->visible,"Scene water telemetry failed to raise a boil");
    const auto position=response->position;objects.sync(registry,8,{1000,2000,3000});require(response->position!=position,"Sea response did not follow the floating origin");
    objects.delugeEffects(false);objects.sync(registry,8,{});require(!response->visible,"Paused scene ignored its effects switch");objects.delugeEffects(true);objects.sync(registry,8,{});require(response->visible,"Effects toggle erased the sea response");
    objects.cloudQuality(2);objects.quality(0);auto* material=response->getObjectByName("sea steam and spray")->as<Mesh>()->material()->as<ShaderMaterial>();require(material->uniforms.at("samples").value<float>()==12,"Global low detail missed splashdown");objects.quality(1);require(material->uniforms.at("samples").value<float>()==32,"Splashdown quality did not restore");
    objects.sync(registry,0,{});require(!response->visible,"Scene rewind retained splashdown history");registry.remove(o.id);objects.sync(registry,0,{});require(!world->getObjectByName("sea response"),"Removed flight retained its sea effects");
}
// Controlled rendering inputs; this fixture does not fly or modify a guidance sequence.
void splashdownGraphics(Target& renderer){
    auto world=Scene::create();world->background=Color(0);auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(100,-200,400);world->add(sun);world->add(AmbientLight::create(Color(0xffffff),.2f));
    albert::Splashdown water(50,4.5);world->add(water.node());auto camera=OrthographicCamera::create(-100,100,80,-30,.1f,1000);camera->up.set(0,0,1);camera->position.set(0,-300,80);camera->lookAt(Vector3(0,0,20));
    water.update({0,0,85},{0,0,1},.5f,false,0,{2,-1,0});for(int i=1;i<=240;++i)water.update({0,0,85},{0,0,1},.5f,false,i/30.,{2,-1,0});
    renderer.render(*world,*camera);const auto burn=renderer.readRGBPixels();auto coverage=[](const auto& p){return std::count_if(p.begin(),p.end(),[](auto x){return x>20;});};
    require(coverage(burn)>300,"Landing burn did not raise water steam");renderer.render(*world,*camera);require(renderer.readRGBPixels()==burn,"Stationary sea steam flickered");
    water.update({0,0,25},{0,0,1},0,true,8.1,{2,-1,0});renderer.render(*world,*camera);const auto touch=renderer.readRGBPixels();require(touch!=burn,"Touchdown failed to change the water response");
    water.update({8,0,3},{.99f,0,.1f},0,true,10,{2,-1,0});renderer.render(*world,*camera);const auto tip=renderer.readRGBPixels();require(coverage(tip)>300&&tip!=touch,"Broadside entry lost its spray burst");
    water.update({8,0,3},{.99f,0,.1f},0,true,70,{2,-1,0});renderer.render(*world,*camera);const auto residual=renderer.readRGBPixels();require(coverage(residual)>80,"Foam vanished before 60 seconds");
    water.update({8,0,3},{.99f,0,.1f},0,true,70,{2,-1,0});renderer.render(*world,*camera);require(renderer.readRGBPixels()==residual,"Paused water contact kept emitting spray");
    for(int q:{0,1,2}){water.quality(q);renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())>80,"Sea response lost a quality tier");}
    water.update({8,0,3},{.99f,0,.1f},0,true,70,{2,-1,0},false);renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())==0,"Effects switch retained splashdown");
    water.update({0,0,200},{0,0,1},0,false,0,{0,0,0});require(!water.node()->visible,"Rewind retained ocean foam");
    // With no sunlight the live plume lights the boil, and that glow decays after cutoff.
    sun->intensity=0;for(auto* n:world->children)if(auto* a=n->as<AmbientLight>())a->intensity=0;
    for(int i=1;i<=150;++i)water.update({0,0,40},{0,0,1},.6f,false,i/30.,{0,0,0});renderer.render(*world,*camera);const auto night=renderer.readRGBPixels();
    int warm=0;for(size_t i=0;i<night.size();i+=3)warm+=night[i]>night[i+2]+20;require(warm>50,"Plume failed to light the sea steam at night");
    water.update({0,0,200},{0,0,1},0,false,30,{0,0,0});renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())<20,"Sea steam emitted after cutoff in darkness");
    // The same waterline applies to steel and hinged geometry, with submerged hull fragments hidden.
    water.node()->visible=false;sun->intensity=3;auto fill=AmbientLight::create(Color(0xffffff),.3f);world->add(fill);
    albert::core::VehicleLook look;look.length=20;look.diameter=6;look.sections.push_back({0,20,3,3,0xdddddd,0});
    albert::Vehicle hull(look);hull.node()->rotation.x=std::numbers::pi_v<float>/2;world->add(hull.node());
    camera->left=camera->bottom=-15;camera->right=camera->top=15;camera->position.set(0,-80,0);camera->lookAt(Vector3());camera->updateProjectionMatrix();
    renderer.render(*world,*camera);const auto dry=renderer.readRGBPixels();hull.water(Vector4(0.f,1.f,0.f,0.f));renderer.render(*world,*camera);const auto half=renderer.readRGBPixels();
    require(coverage(half)>100&&coverage(half)<coverage(dry)*.7,"Hull waterline failed to hide its submerged half");
    hull.water(Vector4(0.f,1.f,0.f,-30.f));renderer.render(*world,*camera);require(coverage(renderer.readRGBPixels())==0,"Sunk hull remained visible above the water");
    std::cout<<"Splashdown: burn, touchdown, tip-over, residual foam, deterministic pause, quality, toggle, rewind and night light OK\n";
}
#ifdef ALBERT_TEST_RPC
void splashdownCaptures(const std::string& directory){
    std::filesystem::create_directories(directory);GpuTarget target(960,720);target.scene.toneMapping=true;target.scene.samples=1;
    albert::gpu::Atmosphere atmosphere(target.context,albert::gpu::earthAtmosphere());atmosphere.precompute(target.context);albert::gpu::Ocean ocean(target.context);
    auto world=Scene::create();world->background=Color(0x8db9d5);auto planet=Object3D::create();planet->position.z=-6378137;world->add(planet);target.scene.air={&atmosphere,planet.get(),.001f,1};target.scene.air.surfaceWind=6;
    target.scene.sea={&ocean,true,0,5,-2,1,{0,0,0},{1,0,0},{0,1,0}};
    auto skyMat=ShaderMaterial::create();skyMat->name="albert:atmosphere";skyMat->side=Side::Back;skyMat->depthWrite=false;skyMat->uniforms={{"visibility",Uniform(1.f)}};
    auto sky=Mesh::create(SphereGeometry::create(2000000,32,16),skyMat);sky->frustumCulled=false;world->add(sky);
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(100,-250,320);world->add(sun);auto ambient=AmbientLight::create(Color(0xbed6ed),.12f);world->add(ambient);
    auto seaMat=MeshStandardMaterial::create();seaMat->name="albert:water";seaMat->color.setRGB(.012f,.046f,.065f);seaMat->roughness=.34f;
    seaMat->roughnessMap=DataTexture::create(ImageData(std::vector<unsigned char>{40,40,40,255}),1,1);
    auto sea=Mesh::create(PlaneGeometry::create(100000,100000),seaMat);world->add(sea);
    const auto look=captureLook(albert::example::starship::shipAlone());albert::Vehicle ship(look);world->add(ship.node());albert::Splashdown water(float(look.length),float(look.diameter*.5));world->add(water.node());
    auto camera=PerspectiveCamera::create(55,4.f/3,.1f,200000);camera->up.set(0,0,1);
    double time=0;auto step=[&](double until){do{time=std::min(until,time+1./30);const float h=float(120-6*time);const float roll=float(std::clamp((time-21)/5.,0.,1.))*1.49f;
        const Vector3 nose(std::sin(roll),0,std::cos(roll));const float centreZ=time<=20?h+float(look.length*.5):float(look.length*.5)*std::cos(roll)+.3f-std::min(4.f,float(std::max(0.,time-26))*.06f);
        const Vector3 centre(time>21?float(look.length*.3)*std::sin(roll):0,0,centreZ);ship.node()->position.copy(centre);ship.node()->quaternion.setFromUnitVectors(Vector3(0,1,0),nose);ship.node()->quaternion.multiply(Quaternion().setFromAxisAngle(Vector3(0,1,0),std::numbers::pi_v<float>));
        unsigned surfaces=0;for(const auto& p:look.parts)surfaces=std::max(surfaces,p.surface);
        std::vector<float> parts(look.engines.size()*3+look.thrusters.size()+surfaces+7);for(int i=0;i<3;++i)parts[3*i]=time<20?.6f:0;parts[parts.size()-7]=101325;parts.back()=-1;ship.animate(&parts,time);
        const auto up=Vector3(0,0,1).applyQuaternion(ship.node()->quaternion.clone().invert());ship.water(Vector4(up.x,up.y,up.z,centre.z));
        water.update(centre,nose,time<20?.3f:0,time>=20,time,{2,-.6f,0});target.scene.sea.seconds=time;
    }while(time<until-.0001);};
    auto shot=[&](const std::string& name){target.render(*world,*camera);const auto pixels=target.readRGBPixels();std::ofstream out(directory+"/"+name+".ppm",std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
        for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);
        std::cout<<name<<", draws "<<target.scene.drawn()<<", GPU ms "<<target.scene.gpuMilliseconds()<<std::endl;};
    auto views=[&](const std::string& label){camera->position.set(145,-185,110);camera->lookAt(Vector3(0,0,35));shot(label+"-chase");camera->position.set(75,-215,2);camera->lookAt(Vector3(0,0,32));shot(label+"-buoy");};
    step(10);views("burn-60m");step(17.5);views("burn-15m");step(20.4);views("touchdown");step(26.5);views("tip-over");
    // Supplemental live-burn night pair, with the same pre-contact state.
    step(80);views("after-60s");
    camera->position.set(145,-185,110);camera->lookAt(Vector3(0,0,25));
    for(int q:{0,1,2}){water.quality(q);shot("cost-residual-"+std::to_string(q)+"-warmup");for(int i=0;i<3;++i)shot("cost-residual-"+std::to_string(q)+"-"+std::to_string(i));}
    water.node()->visible=false;shot("cost-hidden-warmup");for(int i=0;i<3;++i)shot("cost-hidden-"+std::to_string(i));water.node()->visible=true;
    // Recreate the near-water burn for tier timings and night-light evidence.
    water.reset();time=0;step(17.5);camera->lookAt(Vector3(0,0,35));
    for(int q:{0,1,2}){water.quality(q);shot("cost-burn-"+std::to_string(q)+"-warmup");for(int i=0;i<3;++i)shot("cost-burn-"+std::to_string(q)+"-"+std::to_string(i));}
    water.node()->visible=false;shot("cost-burn-hidden-warmup");for(int i=0;i<3;++i)shot("cost-burn-hidden-"+std::to_string(i));water.node()->visible=true;
    sun->intensity=ambient->intensity=0;target.scene.air.strength=0;world->background=Color(0);sky->visible=false;views("burn-night");
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<std::endl;require(albert::gpu::errors()==0,"Splashdown captures produced GPU validation errors");
}
#endif
