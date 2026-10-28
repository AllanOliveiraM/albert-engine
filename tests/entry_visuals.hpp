// Render fixtures only: prescribed telemetry exercises the same Vehicle path as live flight.
void entryGraphics(Target& renderer){
    albert::core::VehicleLook look;look.length=30;look.diameter=9;look.sections.push_back({0,30,4.5,4.5,0xc9c9c6,3.14159265359});
    albert::Vehicle vehicle(look);auto world=Scene::create();world->background=Color(0);world->add(vehicle.node());
    auto camera=OrthographicCamera::create(-25,25,25,-25,.1,500);camera->position.set(45,15,65);camera->lookAt(Vector3());
    std::vector<float> p(7);p[0]=20;p[3]=20;p[4]=1.2f;p.back()=-1;
    auto energy=[](const auto& pixels){double sum=0;for(auto v:pixels)sum+=v;return sum;};
    p[1]=29999;vehicle.animate(&p,10);require(!vehicle.node()->getObjectByName("entry sheath")->visible&&!vehicle.node()->getObjectByName("entry wake")->visible,"Plasma appeared below threshold");
    p[1]=45000;vehicle.animate(&p,10);renderer.render(*world,*camera);const double onset=energy(renderer.readRGBPixels());
    p[1]=600000;vehicle.animate(&p,10);renderer.render(*world,*camera);const auto peak=renderer.readRGBPixels();require(energy(peak)>onset*1.5,"Plasma did not increase with heat flux");
    renderer.render(*world,*camera);require(renderer.readRGBPixels()==peak,"Paused plasma flickered between renders");
    vehicle.animate(&p,10.12);renderer.render(*world,*camera);require(renderer.readRGBPixels()!=peak,"Plasma curtain did not advect with simulated time");
    vehicle.animate(&p,10+6.283185307179586);renderer.render(*world,*camera);const auto wrapped=renderer.readRGBPixels();
    double seam=0;for(std::size_t i=0;i<peak.size();++i)seam+=std::abs(int(peak[i])-int(wrapped[i]));require(seam<energy(peak)*.001,"Plasma time phase popped at its wrap");
    vehicle.animate(&p,10);
    camera->position.x+=.01f;camera->lookAt(Vector3());renderer.render(*world,*camera);const auto moved=renderer.readRGBPixels();
    double difference=0;for(std::size_t i=0;i<peak.size();++i)difference+=std::abs(int(peak[i])-int(moved[i]));
    require(difference<energy(peak)*.04,"Small camera motion popped the entry sheath");
    camera->position.x-=.01f;camera->lookAt(Vector3());vehicle.quality(0);renderer.render(*world,*camera);require(energy(renderer.readRGBPixels())>onset,"Low quality dropped the plasma");
    vehicle.quality(1);vehicle.animate(&p,10);renderer.render(*world,*camera);require(renderer.readRGBPixels()==peak,"Plasma quality did not restore deterministically");
    const Vector3 a=vehicle.node()->getObjectByName("entry wake")->position;p[4]=-.7f;p[5]=.3f;vehicle.animate(&p,11);
    const Vector3 b=vehicle.node()->getObjectByName("entry wake")->position;require(a.distanceTo(b)>100,"Entry wake remained on the body axis instead of following air incidence");
    vehicle.node()->scale.setScalar(1.f/6378137);camera->position.multiplyScalar(1.f/6378137);camera->left/=6378137;camera->right/=6378137;camera->top/=6378137;camera->bottom/=6378137;camera->nearPlane/=6378137;camera->farPlane/=6378137;camera->updateProjectionMatrix();camera->lookAt(Vector3());
    vehicle.animate(&p,11);renderer.render(*world,*camera);require(energy(renderer.readRGBPixels())>onset,"Plasma failed at Earth-radius scene scale");
    p[1]=0;vehicle.animate(&p,12);require(!vehicle.node()->getObjectByName("entry wake")->visible,"Plasma persisted after heating ended");
    std::cout<<"Entry plasma OK: threshold, heat response, paused time, camera continuity, quality, airflow and scene scale\n";
}
#ifdef ALBERT_TEST_RPC
void entryCaptures(const std::string& directory){
    namespace ex=albert::example::starship;std::filesystem::create_directories(directory);GpuTarget target(1280,960);target.scene.toneMapping=true;target.scene.samples=4;target.scene.shadows=false;
    auto world=Scene::create();world->background=Color(0x020307);
    albert::gpu::Atmosphere atmosphere(target.context,albert::gpu::earthAtmosphere());atmosphere.precompute(target.context);
    auto planet=Object3D::create();planet->position.y=-6378137-65000;world->add(planet);target.scene.air={&atmosphere,planet.get(),.001f,1};
    auto sun=DirectionalLight::create(Color(0xffffff),2);sun->position.set(-300,100,180);world->add(sun);
    auto look=captureLook(ex::stack(),1),boosterLook=captureLook(ex::superHeavy());albert::Vehicle ship(look),booster(boosterLook);world->add(ship.node());world->add(booster.node());booster.node()->visible=false;
    auto sample=[&](albert::Vehicle& v,const auto& appearance,float heat,double utc){unsigned surfaces=0;for(const auto& p:appearance.parts)surfaces=std::max(surfaces,p.surface);
        std::vector<float> p(appearance.engines.size()*3+surfaces+appearance.thrusters.size()+7);for(unsigned i=0;i<surfaces;++i)p[appearance.engines.size()*3+i]=.35f;
        p[p.size()-7]=15;p[p.size()-6]=heat;p[p.size()-4]=22;p[p.size()-3]=1.22f;p[p.size()-2]=.08f;p.back()=-1;v.animate(&p,utc);};
    std::shared_ptr<Camera> camera;
    auto frame=[&](Vector3 eye,Vector3 aim,float fov=52){auto c=PerspectiveCamera::create(fov,4.f/3,.03,2e7);c->position.copy(eye);c->lookAt(aim);camera=c;};
    auto shot=[&](const std::string& name){target.render(*world,*camera);const auto pixels=target.readRGBPixels();const auto path=directory+"/"+name+".ppm";std::ofstream out(path,std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
        for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);std::cout<<path<<", "<<target.scene.drawn()<<" draws, GPU "<<target.scene.gpuMilliseconds()<<" ms\n";};
    sample(ship,look,600000,120);frame({75,18,60},{0,-4,-18},60);shot("peak-beside");
    frame({42,-35,-95},{0,0,0},52);shot("peak-behind");
    frame({5,-10,5},{7.5f,-19,-1.3f},85);shot("peak-onboard-flap");
    sample(ship,look,600000,120.12);shot("peak-onboard-flap-next");
    frame({75,18,60},{0,-4,-18},60);sample(ship,look,65000,120);shot("onset-beside");
    sample(ship,look,600000,120);ship.quality(0);shot("peak-beside-low");ship.quality(1);
    sun->intensity=0;frame({-18,2,2},{-4,0,0},65);shot("peak-steel-lit");
    // A true ground-to-65 km range at night. A narrow camera field resolves the angularly small trail.
    sun->position.set(-300,-600,180);sun->intensity=0;ship.node()->rotation.z=1.05f;ship.node()->position.set(0,65000,0);planet->position.y=-6378137;
    sample(ship,look,600000,120);frame({0,2,14000},{0,64930,-120},1.2f);shot("ground-night");
    ship.node()->position.set(0,0,0);ship.node()->rotation.z=0;planet->position.y=-6378137-65000;sun->position.set(-300,100,180);sun->intensity=2;
    ship.node()->visible=false;booster.node()->visible=true;sample(booster,boosterLook,65000,120);frame({85,22,75},{0,0,0},60);shot("booster-brief");booster.node()->visible=false;ship.node()->visible=true;
    frame({75,18,60},{0,-4,-18},60);sample(ship,look,600000,120);
    auto measure=[&](const char* label){std::vector<double> times;for(int i=0;i<12;++i){target.render(*world,*camera);target.readRGBPixels();if(i>=4)times.push_back(target.scene.gpuMilliseconds());}
        std::sort(times.begin(),times.end());std::cout<<"Entry timing "<<label<<": median "<<times[times.size()/2]<<" ms, "<<target.scene.drawn()<<" draws\n";};
    std::size_t triangles=0,bytes=0,effects=0;
    ship.node()->traverseType<Mesh>([&](Mesh& mesh){if(!mesh.material()||mesh.material()->name!="albert:plasma")return;const auto& g=*mesh.geometry();const auto* index=g.getIndex();
        const auto count=g.getAttribute<float>("position")->count();triangles+=(index?index->count():count)/3;bytes+=count*8*sizeof(float)+(index?index->count()*sizeof(unsigned):0);++effects;});
    std::cout<<"Entry geometry "<<effects<<" meshes, "<<triangles<<" triangles, "<<bytes<<" vertex/index bytes\n";
    measure("high");ship.quality(0);measure("low");ship.quality(1);
    ship.node()->traverseType<Mesh>([](Mesh& mesh){if(mesh.material()&&mesh.material()->name=="albert:plasma")mesh.visible=false;});measure("surfaces only");shot("peak-surfaces-only");ship.quality(0);measure("low surfaces only");
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<'\n';require(albert::gpu::errors()==0,"Entry captures produced GPU errors");
}
#endif
