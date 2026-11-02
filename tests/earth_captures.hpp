// Manual, reproducible space views using the same textures, atmosphere and
// cloud shaders as Albert; no window or live satellite download is needed.
void earthCaptures(const std::string& directory){
    std::filesystem::create_directories(directory);GpuTarget target(1280,960);target.scene.toneMapping=true;target.scene.samples=4;target.scene.exposure=1.05f;
    albert::DownloadProgress progress;auto data=assets();const auto pack=albert::visualAssetPack();
    data.images[0]=pack->image("earth-day",{},4096,progress);data.images[1]=pack->image("earth-night",{},2048,progress);data.images[5]=pack->image("earth-height",{},2048,progress);
    data.oceans=albert::oceanRoughness(*data.images[0]);
    auto world=Scene::create();world->background=Color(0);albert::PlanetVisuals planets(*world,std::move(data));planets.moon->visible=false;planets.sun->visible=false;
    auto sunlight=DirectionalLight::create(Color(0xffffff),2.4f);world->add(sunlight);world->add(AmbientLight::create(Color(0xffffff),.003f));
    auto camera=PerspectiveCamera::create(40,4.f/3,.001f,100);camera->position.set(0,.25f,3.6f);camera->lookAt(Vector3());
    albert::gpu::Atmosphere air(target.context,albert::gpu::earthAtmosphere());air.precompute(target.context);
    albert::VisualSettings settings;settings.terrain=false;planets.setCloudTime(1792861200.);
    auto shot=[&](const char* name,Vector3 direction){direction.normalize();sunlight->position.copy(direction).multiplyScalar(100);planets.sun->position.copy(direction).multiplyScalar(23455);
        for(int i=0;i<1500;++i){planets.update(direction,camera->position,settings,1.f);if(settings.clouds!=albert::CloudMode::Simulated||planets.cloudMap()->image().width()>1)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        require(settings.clouds!=albert::CloudMode::Simulated||planets.cloudMap()->image().width()>1,"Cloud weather did not become ready for capture");
        target.scene.air={&air,planets.earth.get(),6378.137f,1};target.scene.air.clouds=planets.cloudMap();target.scene.air.cloudShadow=planets.cloudOpacity();target.scene.air.cloudOffset=planets.cloudOffset();target.scene.air.surfaceWind=7;
        target.render(*world,*camera);auto pixels=target.readRGBPixels();std::ofstream out(directory+"/"+name+".ppm",std::ios::binary);out<<"P6\n1280 960\n255\n";
        for(unsigned y=0;y<960;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(959-y)*1280*3),1280*3);
    };
    shot("earth-artificial-day",{-.45f,.25f,1});shot("earth-artificial-terminator",{-1,.15f,.08f});shot("earth-artificial-crescent",{-.5f,.1f,-1});
    settings.clouds=albert::CloudMode::Off;shot("earth-clouds-off",{-.45f,.25f,1});
    require(albert::gpu::errors()==0,"Earth captures produced GPU errors");
}
