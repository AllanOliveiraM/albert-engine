#ifdef ALBERT_TEST_RPC
// Frozen visual telemetry, no simulation advancement during warmup or measurement.
void mediaCaptures(const std::string& directory){
    namespace ex=albert::example::starship;std::filesystem::create_directories(directory);
    GpuTarget target(1920,1080);target.scene.toneMapping=true;target.scene.samples=4;
    auto world=Scene::create();world->background=Color(0x87b6d2);world->add(AmbientLight::create(Color(0xb8d4ed),.25f));
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(-350,500,-250);world->add(sun);
    auto ground=Mesh::create(PlaneGeometry::create(15000,15000),albert::assistedMaterial(Color(.18f,.2f,.16f),0,1));ground->rotation.x=-1.5707963f;world->add(ground);
    albert::LaunchCloud cloud({0,0,0},{0,1,0});auto padFrame=Object3D::create();padFrame->rotation.x=-1.5707963f;padFrame->add(cloud.node());world->add(padFrame);
    for(int i=0;i<240;++i)cloud.update(1,41,1.f/30,{3,-1,0},true);
    const auto stackLook=captureLook(ex::stack()),boosterLook=captureLook(ex::superHeavy()),shipLook=captureLook(ex::shipAlone());
    albert::Vehicle stack(stackLook),booster(boosterLook),ship(shipLook);for(auto* v:{&stack,&booster,&ship}){v->quality(1);v->gasQuality(1);v->humidity(.8f);world->add(v->node());}
    auto camera=PerspectiveCamera::create(40,16.f/9,.05f,500000);
    auto frame=[&](Vector3 eye,Vector3 aim,float fov){camera->position.copy(eye);camera->lookAt(aim);camera->fov=fov;camera->updateProjectionMatrix();};
    auto sample=[&](albert::Vehicle& v,const auto& look,int engines,float pressure,float mach,double time){unsigned surfaces=0;for(const auto& p:look.parts)surfaces=std::max(surfaces,p.surface);
        std::vector<float> p(look.engines.size()*3+surfaces+look.thrusters.size()+7);for(int i=0;i<engines;++i)p[3*i]=.85f;
        p[p.size()-7]=pressure;p[p.size()-5]=.35f;p[p.size()-4]=mach;p[p.size()-3]=3.14159265f;p.back()=-1;v.animate(&p,time);};
    std::ofstream csv(directory+"/passes.csv");csv<<"capture,frame,pass,ms\n";
    int frames=8;if(auto e=std::getenv("ALBERT_BENCH_FRAMES"))frames=std::max(2,std::atoi(e));
    auto shot=[&](const std::string& name){std::vector<unsigned char> pixels;
        for(int i=-3;i<frames;++i){target.render(*world,*camera);pixels=target.readRGBPixels();wgpuInstanceProcessEvents(target.context.instance());
            if(i>=0)for(const auto&[pass,ms]:target.scene.gpuPassMilliseconds())csv<<name<<','<<i<<','<<pass<<','<<ms<<'\n';}
        std::ofstream out(directory+"/"+name+".ppm",std::ios::binary);out<<"P6\n1920 1080\n255\n";
        for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);
        std::cout<<name<<", "<<target.scene.drawn()<<" draws, "<<target.context.adapter()<<std::endl;};
    booster.node()->visible=ship.node()->visible=false;stack.node()->position.y=113+float(stackLook.length/2);sample(stack,stackLook,33,99000,.2f,0);sample(stack,stackLook,33,99000,.2f,2);
    frame({750,1.7f,661.44f},{0,130,0},18);shot("liftoff-shore-tracking");
    frame({85,165,125},{0,150,0},55);shot("liftoff-orbit-near-stack");
    stack.node()->visible=false;cloud.node()->visible=false;ground->visible=false;world->background=Color(0x030509);
    booster.node()->visible=ship.node()->visible=true;booster.node()->position.set(0,75000,0);booster.node()->rotation.z=.45f;ship.node()->position.set(250,75400,0);
    sample(booster,boosterLook,13,2,4,10);sample(booster,boosterLook,13,2,4,12);sample(ship,shipLook,6,1,4,10);sample(ship,shipLook,6,1,4,12);
    frame({150,75045,300},{0,75015,0},55);shot("boostback-near-booster-two-envelopes");
    ship.node()->visible=false;booster.node()->rotation.z=0;booster.node()->position.y=1200+float(boosterLook.length/2);world->background=Color(0x87b6d2);ground->visible=true;
    sample(booster,boosterLook,0,88000,.8f,20);sample(booster,boosterLook,13,88000,.8f,20.1);sample(booster,boosterLook,13,88000,.8f,20.32);
    frame({90,1228,125},{0,1215,0},36);shot("landing-ignition-near-booster");
    sample(booster,boosterLook,13,88000,.8f,22);shot("landing-burn-near-booster");
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<std::endl;require(albert::gpu::errors()==0,"Media captures produced validation errors");
}
#endif
