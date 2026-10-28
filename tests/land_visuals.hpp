#pragma once
#ifdef ALBERT_TEST_RPC
void landCaptures(const std::string& directory,bool production=false,bool industrial=false,bool shadowCheck=false,bool nightCheck=false,bool coastCheck=false){
    std::filesystem::create_directories(directory);
    namespace ex=albert::example::starship;namespace api=albert::v1;
    api::CreateFlightRequest request;ex::addTower(request,true);auto scenery=std::make_shared<albert::core::Scenery>();scenery->latitude=ex::towerLatitude*ex::deg;scenery->longitude=ex::towerLongitude*ex::deg;
    scenery->buildings=productionBuildings(request);
    auto cv=[](const api::Vector3& v){return albert::core::Vector{v.x(),v.y(),v.z()};};
    for(const auto& p:request.scenery()){albert::core::SceneryPart q;q.name=p.name();q.centre=cv(p.centre_enu_meters());q.size=cv(p.size_meters());q.heading=p.heading_degrees()*ex::deg;q.tilt=p.tilt_degrees()*ex::deg;
        q.shape=static_cast<albert::core::SceneryPart::Shape>(p.shape());q.inner=p.inner_diameter_meters();q.color=p.color();q.structure=p.structure();scenery->parts.push_back(q);}
    for(const auto& r:request.roads()){albert::core::SceneryRoad q;q.width=r.width_meters();q.highway=r.highway();q.stopStart=r.stop_start();q.stopEnd=r.stop_end();q.surface=static_cast<albert::core::SceneryRoad::Surface>(r.surface());q.osm=r.osm_way();
        for(const auto& p:r.points_enu_meters())q.points.push_back(cv(p));scenery->roads.push_back(q);}
    std::ofstream parts(directory+"/parts.tsv");for(const auto& p:scenery->parts)parts<<p.name<<'\t'<<p.centre.x<<'\t'<<p.centre.y<<'\t'<<p.centre.z<<'\t'<<p.size.x<<'\t'<<p.size.y<<'\t'<<p.size.z<<'\t'<<p.heading<<'\n';parts.close();
    std::map<std::string,int> counts;for(const auto& p:scenery->parts)++counts[p.name];
    std::cout<<"Scenery parts "<<scenery->parts.size()<<" / 2048, structures "<<request.structures_size()<<"\n";
    for(const auto& [name,n]:counts)if(name.starts_with("detail ")||name=="mount leg"||name=="perimeter T wall")std::cout<<name<<": "<<n<<"\n";
    if(scenery->parts.size()>2048)throw std::runtime_error("Scenery API budget exceeded");
    for(const auto& b:request.structures()){albert::core::SceneryBar q;q.from=cv(b.start_enu_meters());q.to=cv(b.end_enu_meters());q.radius=b.has_drawn()&&!b.drawn()?0:b.radius_meters();q.hinged=b.has_hinge_enu_meters();q.pivot=cv(b.hinge_enu_meters());q.axis=b.has_hinge_axis()?cv(b.hinge_axis()):albert::core::Vector{0,0,1};scenery->bars.push_back(q);}
    albert::core::Scene registry;albert::core::Object object;object.id="site";object.name="Starbase";object.scenery=scenery;object.samples.emplace_back();registry.create(object);albert::Roads roads;roads.sync(registry);
    std::cout<<"Ways "<<scenery->roads.size()<<", segments "<<roads.segments()<<", maximum tile candidates "<<roads.candidates()<<"\n";
    std::cout<<"Production buildings/tanks "<<scenery->buildings.size()<<"\n";
    constexpr double a=6378137,e2=6.69437999014e-3;const double phi=scenery->latitude,n=a/std::sqrt(1-e2*std::pow(std::sin(phi),2)),mx=n*std::cos(phi),my=a*(1-e2)/std::pow(1-e2*std::pow(std::sin(phi),2),1.5);
    constexpr double west=-8608,east=2656,south=-6272,north=4992;albert::GeoRegion region{ex::towerLongitude+west/mx/ex::deg,ex::towerLatitude+south/my/ex::deg,ex::towerLongitude+east/mx/ex::deg,ex::towerLatitude+north/my/ex::deg};
    albert::DownloadProgress progress;albert::AssetPack pack("build/linux/imagery/visuals.pak");auto image=pack.image("earth-day",region,2048,progress);
    auto map=DataTexture::create(ImageData(std::move(image.data())),image.width(),image.height());map->colorSpace=ColorSpace::sRGB;map->minFilter=map->magFilter=Filter::Linear;
    auto base=MeshStandardMaterial::create();base->map=map;base->roughness=1;
    auto geometry=PlaneGeometry::create(east-west,north-south,256,256);geometry->translate((west+east)/2,(south+north)/2,0);auto* pos=geometry->getAttribute<float>("position");auto* uv=geometry->getAttribute<float>("uv");std::vector<float> global,residual;
    for(int i=0;i<pos->count();++i){const double x=pos->getX(i),y=pos->getY(i),u=(scenery->longitude+std::numbers::pi+x/mx)/(2*std::numbers::pi),v=(scenery->latitude+y/my)/std::numbers::pi+.5;
        global.insert(global.end(),{float(u),float(v)});residual.insert(residual.end(),{float(u-float(u)),float(v-float(v))});uv->setXY(i,(x-west)/(east-west),(y-south)/(north-south));}
    geometry->setAttribute("uv2",FloatBufferAttribute::create(std::move(global),2));geometry->setAttribute("geoResidual",FloatBufferAttribute::create(std::move(residual),2));
    auto ground=Mesh::create(geometry,roads.material(base,1));ground->receiveShadow=true;ground->frustumCulled=false;auto world=Scene::create();world->background=Color(0x7998b4);world->add(ground);
    albert::Site site(*scenery);site.node()->visible=true;world->add(site.node());
    if(production)for(const char* name:{"production near","production distant"}){auto* level=site.node()->getObjectByName(name);unsigned triangles=0;
        for(auto* child:level->children){auto* mesh=child->as<Mesh>();triangles+=mesh->geometry()->getAttribute<float>("position")->count()/3;}
        std::cout<<name<<": "<<level->children.size()<<" draws, "<<triangles<<" triangles\n";}
    std::vector<float> angles;for(const auto& b:request.structures())angles.push_back(b.start_radians());site.animate(&angles);
    const auto definition=ex::stack();const auto& stage=definition.stages(0);const auto& appearance=stage.appearance();
    albert::core::VehicleLook look;look.length=ex::shipLength+ex::boosterLength;look.diameter=9;
    for(const auto& x:appearance.sections())look.sections.push_back({x.from_meters(),x.to_meters(),x.radius_from_meters(),x.radius_to_meters(),x.color(),x.tiles_radians()});
    for(const auto& x:appearance.parts()){look.parts.push_back({x.name(),cv(x.hinge_meters()),cv(x.axis()),cv(x.outward()),x.span_meters(),x.width_meters(),x.thickness_meters(),x.gain(),x.offset_radians(),x.surface(),x.color(),x.grid()});auto& q=look.parts.back();q.tipSpan=x.has_tip_span_meters()?x.tip_span_meters():-1;q.tipShift=x.tip_shift_meters();q.tipThickness=x.has_tip_thickness_meters()?x.tip_thickness_meters():-1;}
    for(const auto& t:appearance.tanks())look.tanks.push_back({t.from_meters(),t.to_meters(),t.full()});
    albert::Vehicle vehicle(look);vehicle.node()->rotation.x=ex::deg*90;vehicle.node()->position.z=ex::deck+look.length/2;vehicle.animate(nullptr,0);world->add(vehicle.node());
    auto light=DirectionalLight::create(Color(0xffffff),3.0);light->position.set(-400,-300,9000);world->add(light);light->castShadow=true;light->shadow->mapSize.set(2048,2048);
    auto& shadow=static_cast<OrthographicCamera&>(*light->shadow->camera);shadow.left=shadow.bottom=-650;shadow.right=shadow.top=650;shadow.nearPlane=.1;shadow.farPlane=16000;shadow.updateProjectionMatrix();light->shadow->bias=-.0004f;light->shadow->normalBias=.25f;
    auto ambient=AmbientLight::create(Color(0xb8d4ed),.12f);world->add(ambient);
    albert::gpu::Context context;albert::gpu::SceneRenderer renderer(context);albert::defineWebGpuPrograms();renderer.samples=4;renderer.shadows=true;
    albert::gpu::Atmosphere atmosphere(context,albert::gpu::earthAtmosphere());atmosphere.precompute(context);
    auto planet=Object3D::create();planet->position.z=-6378137;world->add(planet);renderer.air={&atmosphere,planet.get(),.001f,1};renderer.air.surfaceWind=5;
    albert::gpu::Ocean ocean(context);renderer.sea={&ocean,true,12,3,-4,1,{0,0,0},{1,0,0},{0,1,0}};
    auto skyMaterial=ShaderMaterial::create();skyMaterial->name="albert:atmosphere";skyMaterial->side=Side::Back;skyMaterial->depthWrite=false;skyMaterial->uniforms={{"visibility",Uniform(1.f)}};
    auto sky=Mesh::create(SphereGeometry::create(2000000,32,16),skyMaterial);sky->frustumCulled=false;world->add(sky);
    renderer.toneMapping=true;
    constexpr unsigned width=1280,height=960,row=width*4;WGPUTextureDescriptor td{};td.size={width,height,1};td.format=context.format();td.dimension=WGPUTextureDimension_2D;td.mipLevelCount=td.sampleCount=1;td.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    auto target=wgpuDeviceCreateTexture(context.device(),&td);auto view=wgpuTextureCreateView(target,nullptr);std::shared_ptr<Camera> camera=OrthographicCamera::create(-560,560,420,-420,.1,30000);
    Vector3 aim(-75,100,0);
    auto shot=[&](const std::string& name){const int repeats=(nightCheck||coastCheck)?4:shadowCheck?6:1;std::vector<double> milliseconds,gpuTimes;for(int repeat=0;repeat<repeats;++repeat){const auto start=std::chrono::steady_clock::now();if(shadowCheck&&std::getenv("ALBERT_SHADOW_BASELINE"))site.node()->material()->as<ShaderMaterial>()->uniforms.at("strength").setValue(0.f);camera->lookAt(aim);site.view(*camera);auto encoder=wgpuDeviceCreateCommandEncoder(context.device(),nullptr);renderer.render(*world,*camera,view,width,height,encoder);auto cmd=wgpuCommandEncoderFinish(encoder,nullptr);wgpuQueueSubmit(context.queue(),1,&cmd);renderer.submitted();wgpuCommandBufferRelease(cmd);wgpuCommandEncoderRelease(encoder);
        WGPUBufferDescriptor bd{};bd.size=row*height;bd.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;auto buffer=wgpuDeviceCreateBuffer(context.device(),&bd);encoder=wgpuDeviceCreateCommandEncoder(context.device(),nullptr);WGPUTexelCopyTextureInfo src{};src.texture=target;WGPUTexelCopyBufferInfo dst{};dst.buffer=buffer;dst.layout.bytesPerRow=row;dst.layout.rowsPerImage=height;WGPUExtent3D size{width,height,1};wgpuCommandEncoderCopyTextureToBuffer(encoder,&src,&dst,&size);cmd=wgpuCommandEncoderFinish(encoder,nullptr);wgpuQueueSubmit(context.queue(),1,&cmd);
        bool ready=false;WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&ready;cb.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};wgpuBufferMapAsync(buffer,WGPUMapMode_Read,0,bd.size,cb);while(!ready)wgpuInstanceProcessEvents(context.instance());
        const auto* pixels=static_cast<const unsigned char*>(wgpuBufferGetConstMappedRange(buffer,0,bd.size));std::vector<unsigned char> rgba(pixels,pixels+bd.size);if(context.format()==WGPUTextureFormat_BGRA8Unorm)for(std::size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);
        if(repeat>0&&renderer.gpuMilliseconds()>=0)gpuTimes.push_back(renderer.gpuMilliseconds());
        milliseconds.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        if(repeat==repeats-1){std::ofstream output(directory+"/"+name+".ppm",std::ios::binary);output<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)output.write(reinterpret_cast<const char*>(rgba.data()+y*row+x*4),3);}
        wgpuBufferUnmap(buffer);wgpuBufferRelease(buffer);wgpuCommandBufferRelease(cmd);wgpuCommandEncoderRelease(encoder);}std::sort(gpuTimes.begin(),gpuTimes.end());std::sort(milliseconds.begin()+((shadowCheck||nightCheck||coastCheck)?1:0),milliseconds.end());std::cout<<name<<" synchronized frame ms "<<milliseconds[milliseconds.size()/2]<<", draws "<<renderer.drawn()<<", shadow draws "<<renderer.shadowDrawn()<<", shadow bytes "<<renderer.shadowBytes()<<", GPU ms "<<(gpuTimes.empty()?renderer.gpuMilliseconds():gpuTimes[gpuTimes.size()/2])<<"\n";};
    auto frame=[&](Vector3 at,Vector3 eye,float fov){aim=at;auto c=PerspectiveCamera::create(fov,4.f/3,.1,30000);c->up.set(0,0,1);if(eye.x==at.x&&eye.y==at.y)c->up.set(0,1,0);c->position.copy(eye);camera=c;};
    if(coastCheck){
        renderer.shadowMetersPerUnit=1;renderer.shadowHigh=false;renderer.sea.enabled=false;
        const auto coastal=albert::coastalLight(1);
        renderer.air.hazeExtinction=coastal.extinction;renderer.air.hazeScaleKm=coastal.scaleKm;renderer.air.localCloudCover=coastal.cover;
        renderer.air.cloudKm=(coastal.baseKm+coastal.topKm)*.5f;renderer.air.cloudShadow=.45f;
        renderer.volume.enabled=true;renderer.volume.baseKm=coastal.baseKm;renderer.volume.topKm=coastal.topKm;
        renderer.volume.divisor=4;renderer.volume.steps=32;renderer.volume.maxRays=80000;
        light->intensity=2.4f;ambient->intensity=0; // Same sunlight and sky irradiance as the application.
        for(const std::string time:{"noon","golden","sunset"}){
            const float elevation=time=="noon"?.90f:time=="golden"?.104528f:.008727f;
            const float horizontal=10000*std::sqrt(1-elevation*elevation);
            light->position.set(-horizontal*.97f,-horizontal*.2431f,10000*elevation);
            site.lighting(light->position);renderer.exposure=2.1f*albert::earthNightExposure(elevation);
            frame({0,0,57},{100,-150,1.7f},64);shot("pad-eye-"+time);
            frame({-3330,-1040,42},{-150,-200,500},48);shot("production-west-500m-"+time);
            frame({3000,900,90},{-22,0,165},65);shot("tower-sea-"+time);
        }
        // Component timings and weather/night views use the same atmosphere and materials.
        light->position.set(-4000,-3000,9000);site.lighting(light->position);renderer.exposure=2.1f;
        renderer.volume.enabled=false;
        frame({-3330,-1040,42},{-150,-200,500},48);shot("production-west-500m-haze-only");
        renderer.air.hazeExtinction=0;renderer.air.localCloudCover=-1;renderer.air.cloudShadow=0;shot("production-west-500m-clear-control");
        auto weather=[&](float humidity,float cover){const auto c=albert::coastalLight(1,humidity,cover);
            renderer.air.hazeExtinction=c.extinction;renderer.air.localCloudCover=c.cover;renderer.air.cloudShadow=.45f;
            renderer.air.cloudKm=(c.baseKm+c.topKm)*.5f;renderer.volume.baseKm=c.baseKm;renderer.volume.topKm=c.topKm;};
        weather(.90f,.85f);renderer.volume.enabled=true;
        frame({3000,900,90},{-22,0,165},65);shot("tower-sea-stratocumulus");
        weather(.78f,.38f);light->position.set(9700,2431,1045);site.lighting(light->position);renderer.exposure=2.1f*albert::earthNightExposure(.104528f);shot("tower-sea-golden-glint");
        light->position.set(-9000,0,-5000);site.lighting(light->position);renderer.shadows=false;renderer.exposure=2.1f*albert::earthNightExposure(-.48f);
        auto stars=albert::makeStarField("assets/stars.csv",2461322.5);stars->frustumCulled=false;world->add(stars);
        frame({3000,900,400},{-22,0,165},65);shot("tower-sea-night");
        std::cout<<renderer.memory()<<"\nAdapter "<<context.adapter()<<", validation errors "<<albert::gpu::errors()<<"\n";
        wgpuTextureViewRelease(view);wgpuTextureRelease(target);require(albert::gpu::errors()==0,"Coastal captures produced GPU validation errors");return;
    }
    if(nightCheck){
        renderer.shadowMetersPerUnit=1;
        const bool disabled=std::getenv("ALBERT_SITE_LIGHTS_OFF");
        for(const std::string time:{"day","dusk","night"}){
            const float elevation=time=="day"?.73f:time=="dusk"?0.f:-.21f;
            light->position.set(-8000,-6000,10000*elevation/std::sqrt(1-elevation*elevation));
            site.lighting(disabled?Vector3(0,0,10000):light->position);renderer.shadows=time!="night";
            ambient->intensity=time=="day"?.12f:time=="dusk"?.015f:.002f;
            renderer.exposure=2.1f*albert::earthNightExposure(elevation);
            // Age real scheduled boil-off at the same UTC in each lighting state.
            site.venting(100);for(unsigned i=1;i<=120;++i)site.venting(100+i/30.);
            frame({0,0,57},{100,-150,1.7f},64);shot("pad-eye-"+time);
            frame({-10,0,139},{45,-65,165},65);shot("tower-top-"+time);
            frame({-65,100,0},{160,-190,300},62);shot("oblique-300m-"+time);
            frame({-3330,-1040,42},{-150,-200,20},42);shot("production-pad-"+time);
            if(time=="night"){
                frame({-65,100,0},{160,-190,300},62);renderer.shadowHigh=false;site.quality(0);ground->setMaterial(roads.material(base,0));shot("oblique-300m-night-low");
                site.quality(1);renderer.shadowHigh=true;ground->setMaterial(roads.material(base,1));
                frame({-3330,-1040,35},{-2840,-1360,45},62);shot("production-close-night");
                frame({-90,123,5},{-22,5,34},66);shot("tank-farm-night");
                frame({-22,0,154},{0,-5000,100},8);shot("beacons-5km-night");
            }
        }
        std::cout<<renderer.memory()<<"\nAdapter "<<context.adapter()<<", validation errors "<<albert::gpu::errors()<<"\n";
        wgpuTextureViewRelease(view);wgpuTextureRelease(target);require(albert::gpu::errors()==0,"Night captures produced GPU validation errors");return;
    }
    if(shadowCheck){
        renderer.shadowMetersPerUnit=std::getenv("ALBERT_SHADOW_BASELINE")?0:1;
        light->position.set(-6000,-8000,9000);
        frame({0,0,30},{65,-100,1.7f},64);shot("pad-eye");
        frame({-80,124,2},{-26,68,1.7f},66);shot("tank-eye");
        frame({-45,65,0},{45,-65,165},70);shot("tower-top");
        frame({-65,100,0},{160,-190,300},62);shot("oblique-300m");
        renderer.shadowHigh=false;site.quality(0);ground->setMaterial(roads.material(base,0));light->shadow->mapSize.set(1024,1024);shot("oblique-300m-low");
        renderer.shadowHigh=true;site.quality(1);ground->setMaterial(roads.material(base,1));light->shadow->mapSize.set(2048,2048);
        frame({-700,0,0},{100,-400,2000},70);shot("altitude-2km");
        light->position.set(-10000,-4000,1800);frame({-20,30,10},{90,-150,55},65);shot("low-sun");
        light->position.set(-6000,-8000,9000);frame({-65,100,0},{160,-190,300},62);
        site.node()->material()->as<ShaderMaterial>()->uniforms.at("strength").setValue(0.f);shot("contact-disabled");
        site.quality(1);renderer.shadows=false;shot("sun-shadows-disabled");renderer.shadows=true;
        light->intensity=0;world->remove(*sky);world->background=Color(0);renderer.air.strength=0;
        // No sky illumination: baked visibility must never add light.
        std::vector<Object3D*> ambientLights;world->traverse([&](Object3D& o){if(dynamic_cast<AmbientLight*>(&o))ambientLights.push_back(&o);});for(auto* o:ambientLights)world->remove(*o);
        shot("night");
        std::cout<<renderer.memory()<<"\nAdapter "<<context.adapter()<<", validation errors "<<albert::gpu::errors()<<"\n";
        wgpuTextureViewRelease(view);wgpuTextureRelease(target);require(albert::gpu::errors()==0,"Shadow captures produced GPU validation errors");return;
    }
    if(industrial){
        light->position.set(-6000,-8000,9000);
        for(const char* name:{"tank farm shells","tank farm steel","tank farm fittings"}){auto* mesh=site.node()->getObjectByName(name)->as<Mesh>();
            const auto& g=*mesh->geometry();const unsigned triangles=g.getIndex()?g.getIndex()->count()/3:g.getAttribute<float>("position")->count()/3;
            std::cout<<name<<": 1 draw, "<<triangles<<" triangles\n";}
        frame({-67,116,3},{-22,5,146},70);shot("tank-farm-tower-top");
        frame({-91,124,0},{110,-100,300},62);shot("tank-farm-300m");
        site.quality(0);shot("tank-farm-300m-low");site.quality(1);
        frame({-28,102,4},{46,41,1.7f},64);shot("tank-farm-eye");
        frame({-99,123,4},{-118,72,1.7f},66);shot("tank-farm-canopy-eye");
        frame({-167,150,5},{-225,100,1.7f},66);shot("tank-farm-gse-eye");
        light->position.set(6000,8000,9000);frame({-96,155,3},{-66,179,11},54);shot("tank-farm-vaporizers");
        std::cout<<"Adapter "<<context.adapter()<<", validation errors "<<albert::gpu::errors()<<"\n";
        wgpuTextureViewRelease(view);wgpuTextureRelease(target);require(albert::gpu::errors()==0,"Tank farm captures produced GPU validation errors");return;
    }
    if(production){
        productionData();
        frame({-3330,-1040,42},{-150,-200,20},42);shot("production-pad-west");
        frame({-1900,-400,0},{-1900,-900,1600},84);shot("production-ascent-1600m");
        frame({-3190,-1050,35},{-2840,-1360,45},62);shot("production-starfactory-close");
        site.quality(0);ground->setMaterial(roads.material(base,0));shot("production-starfactory-low");
        site.quality(1);ground->setMaterial(roads.material(base,1));
        frame({-3500,-1000,0},{-3500,-1000,1800},65);shot("production-plan-1800m");
        frame({-3580,-1145,32},{-3660,-1250,35},55);shot("production-rocket-garden");
        frame({-3330,-1040,42},{-150,-200,20},42);light->position.set(-10000,1000,2500);shot("production-pad-west-sunset");
        std::cout<<"Adapter "<<context.adapter()<<", validation errors "<<albert::gpu::errors()<<"\n";
        wgpuTextureViewRelease(view);wgpuTextureRelease(target);require(albert::gpu::errors()==0,"Production captures produced GPU validation errors");return;
    }
    frame({-1700,0,0},{-1700,-2600,6500},65);shot("boca-chica-ascent");
    frame({-1700,0,0},{-1700,0,7000},75);shot("boca-chica-plan");
    ground->setMaterial(base);shot("boca-chica-imagery");ground->setMaterial(roads.material(base,1));
    frame({-600,500,0},{1400,-1700,1150},68);shot("pad-coast-marsh");
    frame({300,1100,0},{150,-120,2.2f},70);shot("pad-eye-beach");
    frame({-300,1300,0},{-200,-100,250},70);shot("tidal-channels");
    frame({-3600,1700,0},{-600,0,500},70);shot("western-lagoons");
    frame({380,-1300,0},{700,-1900,60},70);shot("gulf-surf");
    site.quality(0);ground->setMaterial(roads.material(base,0));shot("gulf-surf-low");
    frame({-1700,0,0},{-1700,-2600,6500},65);shot("boca-chica-ascent-low");
    site.quality(1);ground->setMaterial(roads.material(base,1));
    frame({-600,500,0},{1400,-1700,1150},68);renderer.sea.enabled=false;shot("coast-waves-disabled");
    std::cout<<"Adapter "<<context.adapter()<<", validation errors "<<albert::gpu::errors()<<"\n";
    wgpuTextureViewRelease(view);wgpuTextureRelease(target);require(albert::gpu::errors()==0,"Land captures produced GPU validation errors");
}

#endif
