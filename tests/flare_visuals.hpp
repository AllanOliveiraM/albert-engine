bool flareVisuals(const std::string& directory={},bool hd=false){
    if(!directory.empty())std::filesystem::create_directories(directory);
    std::unique_ptr<GpuTarget> gpu;
    std::cout<<"Flare: creating WebGPU target\n";
    try{gpu=std::make_unique<GpuTarget>(hd?1920:384,hd?1080:256);}catch(const std::exception& e){std::cout<<"Flare checks skipped: "<<e.what()<<'\n';return false;}
    auto& target=*gpu;target.scene.toneMapping=true;target.scene.exposure=1.05f;
    std::cout<<"Flare adapter: "<<target.context.adapter()<<'\n';
    auto world=Scene::create();world->background=Color(0);
    auto material=ShaderMaterial::create();material->name="albert:sun";
    auto photo=DataTexture::create(std::vector<unsigned char>{255,255,255,255},1,1);
    material->uniforms.emplace("sunMap",Uniform(photo.get()));
    auto sun=Mesh::create(SphereGeometry::create(109.076f,64,32),material);sun->position.set(5160,1876,-23455);world->add(sun);
    auto camera=PerspectiveCamera::create(50,float(target.width)/target.height,.0001f,100000.f);camera->lookAt(Vector3(0,0,-1));
    auto black=MeshBasicMaterial::create();black->color=Color(0);
    auto blocker=Mesh::create(PlaneGeometry::create(.4f,.4f),black);blocker->position.set(2.2f,.8f,-10);blocker->visible=false;world->add(blocker);
    auto render=[&](float strength,const std::string& name){
        if(!name.empty())std::cout<<"Flare render: "<<name<<'\n';
        target.scene.solarFlare=strength;target.render(*world,*camera);auto pixels=target.readRGBPixels();
        if(!directory.empty()&&!name.empty()){
            std::ofstream file(directory+"/"+name+".ppm",std::ios::binary);file<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
            for(int y=int(target.height)-1;y>=0;--y)file.write(reinterpret_cast<const char*>(pixels.data()+y*target.width*3),target.width*3);
        }
        return pixels;
    };
    auto difference=[&](const auto& a,const auto& b){
        long sum=0;int peak=0;for(size_t i=0;i<a.size();++i){const int d=int(b[i])-int(a[i]);require(d>=0,"Lens flare darkened the scene");sum+=d;peak=std::max(peak,d);}
        require(peak<=16,"Default lens flare is too strong");return sum;
    };
    for(unsigned samples:{1u,4u}){
        target.scene.samples=samples;const auto suffix=std::to_string(samples);
        blocker->visible=false;sun->visible=true;
        const auto off=render(0,"flare-off-"+suffix),on=render(.035f,"flare-on-"+suffix);const auto full=difference(off,on);
        require(full>100,"Visible off-axis Sun produced no optical effect");
        blocker->visible=true;blocker->position.x=2.2f;
        const auto hidden=render(0,""),hiddenOn=render(.035f,"flare-hidden-"+suffix);
        require(hidden==hiddenOn,"Lens flare leaked through an opaque blocker");
        blocker->position.x=2.4f;
        const auto half=render(0,""),halfOn=render(.035f,"flare-partial-"+suffix);const auto partial=difference(half,halfOn);
        require(partial>0&&partial<full,"Partial occlusion did not attenuate the flare");
        blocker->visible=false;sun->visible=false;const auto absent=render(0,""),absentOn=render(.035f,"");
        require(absent==absentOn,"Hidden Sun left stale lens ghosts");
        sun->visible=true;sun->position.z=23455;const auto behind=render(0,""),behindOn=render(.035f,"");
        require(behind==behindOn,"Sun behind the camera produced ghosts");sun->position.z=-23455;
        const auto repeat=render(.035f,"");require(repeat==on,"Stationary lens flare flickers between frames");
        // Some lanes now reject samples outside the viewport, while the rest
        // contribute. They must still synchronize and measure the visible rim.
        const float sourceX=sun->position.x;
        sun->position.x=-sun->position.z*std::tan(25.f*math::PI/180.f)*float(target.width)/target.height;
        const auto edge=render(0,""),edgeOn=render(.035f,"flare-viewport-edge-"+suffix);
        require(difference(edge,edgeOn)>0,"Partially visible Sun lost its lens effect at the viewport edge");
        require(render(.035f,"")==edgeOn,"Viewport-clipped lens flare flickers between frames");
        sun->position.x=sourceX;
        target.scene.renderScale=.6f;render(.035f,"flare-scaled-"+suffix);target.scene.renderScale=1;
        target.scene.fisheye=true;render(.035f,"flare-fisheye-"+suffix);target.scene.fisheye=false;
        camera->nearPlane=1e-9f;camera->updateProjectionMatrix();
        blocker->visible=true;blocker->scale.setScalar(1000);blocker->position.set(2200,800,-10000);
        const auto distant=render(0,""),distantOn=render(.035f,"flare-distant-blocker-"+suffix);
        require(distant==distantOn,"Depth tolerance let lens light through a distant blocker at a tiny near plane");
        blocker->visible=false;blocker->scale.setScalar(1);blocker->position.set(2.2f,.8f,-10);
        camera->nearPlane=.0001f;camera->updateProjectionMatrix();
        std::cout<<"Flare "<<samples<<"x: visible delta "<<full<<", partial "<<partial<<", fully hidden 0\n";
    }
    // Exercise real atmosphere tables and the volumetric output, including
    // both sides of the clouds' 50%-opacity depth-write threshold.
    std::cout<<"Flare: precomputing atmosphere\n";
    albert::gpu::Atmosphere air(target.context,albert::gpu::earthAtmosphere());air.precompute(target.context);
    std::cout<<"Flare: atmosphere ready\n";
    auto earth=Group::create();world->add(earth);
    target.scene.air={&air,earth.get(),6378.137f,1.f};
    const Vector3 direction(.8660254f,0,.5f);camera->position.set(0,0,1.0000016f);camera->up.set(0,0,1);
    sun->position.copy(camera->position).addScaledVector(direction,23455);
    camera->lookAt(Vector3(sun->position).add(Vector3(0,4000,0)));
    auto light=DirectionalLight::create(Color(0xffffff),2.4f);light->position.copy(direction).multiplyScalar(100);world->add(light);
    auto clear=render(0,""),clearOn=render(.035f,"flare-atmosphere");const auto clearDelta=difference(clear,clearOn);
    require(clearDelta>0,"Clear atmosphere unexpectedly suppressed all lens light");
    auto weather=DataTexture::create(std::vector<unsigned char>{255,255,255,255},1,1);
    target.scene.air.clouds=weather.get();target.scene.air.cloudKm=3.5f;target.scene.air.cloudShadow=1;
    auto& volume=target.scene.volume;volume.enabled=true;volume.flat=true;volume.divisor=8;volume.steps=32;volume.strength=1;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!target.scene.cloudsShown()&&std::chrono::steady_clock::now()<deadline){render(0,"");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    require(target.scene.cloudsShown(),"Volumetric clouds did not initialize for lens test");
    auto cloudy=render(0,""),cloudyOn=render(.035f,"flare-opaque-cloud");
    require(cloudy==cloudyOn,"Lens flare leaked through an opaque cloud volume");
    volume.strength=.45f;auto a=render(0,""),aOn=render(.035f,"");const auto thin=difference(a,aOn);
    volume.strength=.55f;auto b=render(0,""),bOn=render(.035f,"");const auto thick=difference(b,bOn);
    require(thin>thick&&thick>0,"Flare popped at the cloud depth-write threshold instead of fading");
    std::cout<<"Flare clouds: clear delta "<<clearDelta<<", 45% "<<thin<<", 55% "<<thick<<", opaque 0\n";
    if(hd){
        for(int i=0;i<16;++i)render(.035f,"");
        for(const auto& [pass,ms]:target.scene.gpuPassMilliseconds())if(pass.starts_with("solar-"))std::cout<<"Flare 1080p "<<pass<<"="<<ms<<" ms\n";
    }
    require(albert::gpu::errors()==0,"Lens flare produced WebGPU validation errors");
    return true;
}
