#ifdef ALBERT_TEST_RPC
// Definition-backed inspection poses, plus the real broadcast tracking rig during a prescribed engine-off coast.
void gridFinCaptures(const std::string& directory){
    namespace ex=albert::example::starship;std::filesystem::create_directories(directory);GpuTarget target(1280,960);
    target.scene.toneMapping=true;target.scene.samples=4;target.scene.shadows=true;target.scene.shadowMetersPerUnit=1;
    const auto look=captureLook(ex::superHeavy());albert::Vehicle booster(look);auto world=Scene::create();world->background=Color(.13f,.19f,.27f);world->add(booster.node());
    auto sun=DirectionalLight::create(Color(0xffeddc),3);sun->position.set(-20,65,40);sun->castShadow=true;world->add(sun);world->add(AmbientLight::create(Color(0xbacbdf),.65f));
    auto camera=PerspectiveCamera::create(48,4.f/3,.03,20000);auto sample=geometrySample(look);booster.animate(&sample,0);
    std::ofstream manifest(directory+"/metrics.txt");
    auto save=[&](const char* name,Scene& scene,PerspectiveCamera& lens){target.render(scene,lens);const auto pixels=target.readRGBPixels();
        std::ofstream out(directory+"/"+name+".ppm",std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
        for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);
        manifest<<name<<": "<<target.scene.drawn()<<" draws, fov "<<lens.fov<<'\n';std::cout<<name<<": "<<target.scene.drawn()<<" draws\n";};
    auto* fin=booster.node()->getObjectByName("grid fin");
    auto close=[&](Vector3 eye,const char* name){camera->position.copy(eye.applyQuaternion(fin->quaternion).add(fin->position));camera->up.set(0,0,1).applyQuaternion(fin->quaternion);
        camera->lookAt(fin->position);camera->fov=36;camera->updateProjectionMatrix();save(name,*world,*camera);};
    close({8,11,6.324555f},"fin-close-15m"); // Exactly 15 m from the pivot, looking down through the sculpted lips.
    camera->position.copy(Vector3(0,10,0).applyQuaternion(fin->quaternion).add(fin->position));camera->up.set(0,0,1).applyQuaternion(fin->quaternion);camera->lookAt(fin->position);camera->fov=40;camera->updateProjectionMatrix();save("fin-face-10m",*world,*camera);
    close({11,5,8.888194f},"fin-grazing-15m");
    booster.quality(0);close({8,11,6.324555f},"fin-close-low");booster.quality(1);
    // From above the remaining 180 degree gap, all three declared fins project outside the hull.
    const Vector3 aim(0,float(look.length/2)-4,0);camera->up.set(0,1,0);camera->position.copy(aim).add(Vector3(0,51.961525f,-30));camera->lookAt(aim);camera->fov=30;camera->updateProjectionMatrix();save("booster-top-60m",*world,*camera);
    auto* lod=booster.node()->getObjectByName("vehicle detail LOD")->as<LOD>();
    for(int tier=0;tier<2;++tier){const auto g=albert::gridFin(4.f,4.6f,.6f,tier);manifest<<"tier "<<tier<<": "<<g.cells<<" complete interior cells, "<<g.walls<<" sheet spans, "<<g.ends<<" rounded boundary ends, "<<g.joints<<" rounded boundary welds, "<<g.segments<<" segments per span, "<<g.geometry->getAttribute<float>("position")->count()/3<<" lattice triangles per fin, "<<lod->getLevels()[tier].object->geometry()->getIndex()->count()/3<<" total booster hardware triangles\n";}
    CameraFixture f;*f.look=look;f.utc=310;f.vehicle();f.place({800,0,3000},{160,0,-170});
    auto state=f.registry.evaluate("flight",f.utc).state;auto parts=geometrySample(look,.45f);cameraCaptureAir(state,parts);state.parts=std::make_shared<const std::vector<float>>(parts);f.registry.replace("flight",{state});f.objects.sync(f.registry,f.utc,{});
    f.world->background=Color(0x7ba3c7);auto trackSun=DirectionalLight::create(Color(0xffeddc),3);trackSun->position.set(-.2f,.65f,-.4f);trackSun->castShadow=true;f.world->add(trackSun);f.world->add(AmbientLight::create(Color(0xbacbdf),.65f));
    albert::BroadcastCamera rig;rig.select(albert::core::CameraMode::Tracking);const auto view=f.view(rig);camera->position.copy(view.eye);camera->up.copy(view.up);camera->lookAt(view.aim);camera->fov=view.fov;camera->nearPlane=.05f/6378137;camera->farPlane=10;camera->updateProjectionMatrix();
    target.scene.shadowMetersPerUnit=6378137;f.objects.updateLighting(*camera,trackSun->position,{},0);save("booster-coast-tracking",*f.world,*camera);
    const float range=view.eye.distanceTo(albert::renderPosition(state.positionMeters))*6378137;manifest<<"tracking range "<<range<<" m, altitude 3000 m, engines off, sampled fins +/-0.45 rad\n";
    require(range>3000&&range<5000,"Coast tracking capture is not a few km away");
    manifest<<"WebGPU validation errors "<<albert::gpu::errors()<<'\n';std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<'\n';require(albert::gpu::errors()==0,"Fin captures produced GPU validation errors");
}
#endif
