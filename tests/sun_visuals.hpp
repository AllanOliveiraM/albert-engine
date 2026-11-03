// Render the production solar/sky shaders together: a transmitted opaque solar
// disc must also include the foreground air, rather than punching a dark hole.
bool sunVisuals(const std::string& directory={}){
    if(!directory.empty())std::filesystem::create_directories(directory);
    std::unique_ptr<GpuTarget> gpu;
    try{gpu=std::make_unique<GpuTarget>(384,384);}catch(const std::exception& e){std::cout<<"Sun checks skipped: "<<e.what()<<'\n';return false;}
    auto& target=*gpu;target.scene.toneMapping=true;target.scene.exposure=1.05f;
    auto world=Scene::create();world->background=Color(0);
    auto data=assets();data.images[5]=height(0.f);
    albert::PlanetVisuals planets(*world,std::move(data));planets.moon->visible=false;
    auto sunlight=DirectionalLight::create(Color(0xffffff),2.4f);world->add(sunlight);
    auto camera=PerspectiveCamera::create(1.6f,1.f,1e-8f,100000.f);
    albert::gpu::Atmosphere air(target.context,albert::gpu::earthAtmosphere());air.precompute(target.context);
    target.scene.air={&air,planets.earth.get(),6378.137f,1.f};
    albert::VisualSettings settings;settings.terrain=false;settings.clouds=albert::CloudMode::Off;
    settings.shadows=settings.nightLights=false;
    auto save=[&](const std::string& name,const auto& pixels){
        if(directory.empty())return;
        std::ofstream file(directory+"/"+name+".ppm",std::ios::binary);file<<"P6\n384 384\n255\n";
        for(int y=383;y>=0;--y)file.write(reinterpret_cast<const char*>(pixels.data()+y*384*3),384*3);
    };
    auto shot=[&](const char* name,float altitudeKm,float elevation,float haze,float exposure){
        target.scene.exposure=exposure;target.scene.air.strength=altitudeKm<1000?1.f:0.f;
        target.scene.air.hazeExtinction=haze;target.scene.air.hazeScaleKm=.8f;
        const float angle=elevation*math::PI/180.f;Vector3 direction(std::cos(angle),0,std::sin(angle));
        camera->position.set(0,0,1.f+altitudeKm/6378.137f);camera->up.set(0,0,1);
        planets.sun->position.copy(camera->position).addScaledVector(direction,23455.f);
        sunlight->position.copy(direction).multiplyScalar(100.f);
        camera->lookAt(planets.sun->position);camera->updateProjectionMatrix();
        planets.update(direction,camera->position,settings,1.f);
        planets.sun->visible=false;target.render(*world,*camera);const auto sky=target.readRGBPixels();
        planets.sun->visible=true;target.render(*world,*camera);const auto disc=target.readRGBPixels();save(name,disc);
        const int at=3*(192*384+192);
        const auto luminance=[&](const auto& p){return .2126*p[at]+.7152*p[at+1]+.0722*p[at+2];};
        std::cout<<"Solar "<<name<<": RGB "<<int(disc[at])<<","<<int(disc[at+1])<<","<<int(disc[at+2])<<" sky "<<luminance(sky)<<" disc "<<luminance(disc)<<'\n';
        if(elevation>=0){
            require(luminance(disc)+2>=luminance(sky),"Solar disc replaced foreground sky with a dark hole");
            // Added aerosol genuinely attenuates a grazing solar ray. Reserve
            // the saturation check for clear air; a hazy sunset need not clip.
            if(exposure>.1f&&haze==0)require(disc[at]>240,"Unoccluded solar disc is underexposed");
        }
        if(elevation< -1&&altitudeKm<.1f)require(std::abs(luminance(disc)-luminance(sky))<2,"Sun shone through opaque Earth");
        return disc;
    };
    shot("sun-day",.01f,30,0,1.05f);
    const auto clearHorizon=shot("sun-horizon-clear",.01f,.1f,0,1.05f);
    const auto horizon=shot("sun-horizon",.01f,.1f,.04f,1.05f);
    require(horizon[3*(192*384+192)]>=horizon[3*(192*384+192)+2],"Horizon absorption did not warm the solar disc");
    require(horizon[3*(192*384+192)]<clearHorizon[3*(192*384+192)]&&horizon[3*(192*384+192)+2]<clearHorizon[3*(192*384+192)+2],
        "Marine haze did not attenuate the horizon Sun");
    shot("sun-dense-haze",.01f,.4f,.3f,1.05f);
    shot("sun-hidden",.01f,-3,0,1.05f);
    shot("sun-orbital-limb",400,-19.5f,0,1.05f);
    shot("sun-space",10000,30,0,1.05f);
    const auto photo=shot("sun-telescope",10000,30,0,.00003f);
    require(photo[3*(192*384+192)]>80&&photo[3*(192*384+192)]<250,"Solar inspection exposure lost photosphere detail");
    require(albert::gpu::errors()==0,"Sun/atmosphere produced WebGPU validation errors");
    return true;
}
