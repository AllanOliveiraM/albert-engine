// Included after vehicle_visuals.hpp. Explicit render samples; no flight guidance or dynamics run here.
void plumeGraphics(Target& renderer){
    albert::core::VehicleLook look;look.length=8;look.diameter=3;look.sections.push_back({0,8,1.5,1.5,0xaaaaaa,0});
    for(int i=0;i<3;++i){const float a=i*2.0943951f;look.engines.push_back({{8,.65*std::cos(a),.65*std::sin(a)},{1,0,0},.5});}
    albert::Vehicle vehicle(look);auto world=Scene::create();world->background=Color(0);world->add(vehicle.node());
    auto camera=OrthographicCamera::create(-6,6,4,-8,.01,100);camera->position.set(0,-6,30);camera->lookAt(Vector3(0,-6,0));
    std::vector<float> sample(16);sample[9]=101325;sample.back()=-1;for(int i=0;i<3;++i)sample[3*i]=.4f;
    vehicle.animate(&sample,0);vehicle.animate(&sample,1);renderer.render(*world,*camera);const auto side=renderer.readRGBPixels();
    int white=0,width=0;for(int y=0;y<192;++y){int row=0;for(int x=0;x<192;++x){const auto k=3*(y*192+x);white+=side[k]>200&&side[k+1]>180&&side[k+2]>160;row+=side[k]>45||side[k+2]>45;}width=std::max(width,row);}
    std::cout<<"Landing plume white pixels "<<white<<", width "<<width<<'\n';require(white>30&&width>35,"Low-throttle cluster lost its bright broad volume");
    camera->position.set(0,-30,0);camera->lookAt(Vector3(0,-4,0));renderer.render(*world,*camera);const auto aft=renderer.readRGBPixels();
    require(std::count_if(aft.begin(),aft.end(),[](auto p){return p>180;})>100,"Axial plume view collapsed into a line");
    vehicle.quality(0);renderer.render(*world,*camera);const auto low=renderer.readRGBPixels();require(std::count_if(low.begin(),low.end(),[](auto p){return p>180;})>100,"Low quality lost the plume volume");
    // Isolate a receiver: the new exhaust light must warm real material, vanish on cutoff, and survive Earth-radius scene units.
    auto receiver=Mesh::create(PlaneGeometry::create(10,10),albert::assistedMaterial(Color(.5f,.5f,.5f),0,.8f));receiver->position.set(5,-5,0);receiver->rotation.y=-1.5707963f;world->add(receiver);
    camera->position.set(-20,-5,0);camera->lookAt(receiver->position);
    for(auto* child:vehicle.node()->children)if(child->name!="exhaust light")child->visible=false;
    renderer.render(*world,*camera);const auto lit=renderer.readRGBPixels();double warm=0,cool=0;for(std::size_t k=0;k<lit.size();k+=3){warm+=lit[k];cool+=lit[k+2];}
    require(warm>10000&&warm>cool*1.5,"Exhaust failed to light the nearby receiver warm orange");
    const float scale=1.f/6378137;vehicle.node()->scale.setScalar(scale);receiver->scale.setScalar(scale);receiver->position.multiplyScalar(scale);camera->position.multiplyScalar(scale);camera->left*=scale;camera->right*=scale;camera->top*=scale;camera->bottom*=scale;camera->nearPlane*=scale;camera->farPlane*=scale;camera->updateProjectionMatrix();camera->lookAt(receiver->position);
    renderer.render(*world,*camera);const auto scaled=renderer.readRGBPixels();double energy=0;for(std::size_t k=0;k<scaled.size();k+=3)energy+=scaled[k];require(std::abs(energy/warm-1)<.05,"Exhaust illumination depends on scene units");
    for(int i=0;i<3;++i)sample[3*i]=0;vehicle.animate(&sample,2);for(auto* child:vehicle.node()->children)if(child->name!="exhaust light")child->visible=false;renderer.render(*world,*camera);const auto off=renderer.readRGBPixels();require(std::count_if(off.begin(),off.end(),[](auto p){return p>2;})==0,"Exhaust light remained after engine cutoff");
}

#ifdef ALBERT_TEST_RPC
void plumeCaptures(const std::string& directory){
    namespace ex=albert::example::starship;std::filesystem::create_directories(directory);GpuTarget target(960,720);target.scene.toneMapping=true;target.scene.samples=4;
    albert::gpu::Atmosphere atmosphere(target.context,albert::gpu::earthAtmosphere());atmosphere.precompute(target.context);
    auto world=Scene::create();world->background=Color(0);auto planet=Object3D::create();planet->position.y=-6378137;world->add(planet);target.scene.air={&atmosphere,planet.get(),.001f,1};
    auto skyMaterial=ShaderMaterial::create();skyMaterial->name="albert:atmosphere";skyMaterial->side=Side::Back;skyMaterial->depthWrite=false;skyMaterial->uniforms={{"visibility",Uniform(1.f)}};
    auto sky=Mesh::create(SphereGeometry::create(6000,32,16),skyMaterial);sky->frustumCulled=false;world->add(sky);
    auto sun=DirectionalLight::create(Color(1.f,1.f,1.f),3);world->add(sun);
    auto ground=Mesh::create(PlaneGeometry::create(100000,100000),albert::assistedMaterial(Color(.13f,.12f,.1f),0,1));ground->rotation.x=-1.5707963f;world->add(ground);
    albert::v1::CreateFlightRequest request;ex::addTower(request,true);albert::core::Scenery scenery;std::vector<float> angles;
    auto cv=[](const auto& v){return albert::core::Vector{v.x(),v.y(),v.z()};};
    for(const auto& p:request.scenery()){albert::core::SceneryPart q;q.name=p.name();q.centre=cv(p.centre_enu_meters());q.size=cv(p.size_meters());q.heading=p.heading_degrees()*ex::deg;q.tilt=p.tilt_degrees()*ex::deg;q.shape=static_cast<albert::core::SceneryPart::Shape>(p.shape());q.inner=p.inner_diameter_meters();q.color=p.color();q.structure=p.structure();scenery.parts.push_back(q);}
    for(const auto& b:request.structures()){albert::core::SceneryBar q;q.from=cv(b.start_enu_meters());q.to=cv(b.end_enu_meters());q.radius=b.has_drawn()&&!b.drawn()?0:b.radius_meters();q.hinged=b.has_hinge_enu_meters();q.pivot=cv(b.hinge_enu_meters());q.axis=b.has_hinge_axis()?cv(b.hinge_axis()):albert::core::Vector{0,0,1};scenery.bars.push_back(q);angles.push_back(b.start_radians());}
    albert::Site site(scenery);site.node()->rotation.x=-1.5707963f;site.animate(&angles);world->add(site.node());
    const auto boosterLook=captureLook(ex::superHeavy()),shipLook=captureLook(ex::shipAlone());albert::Vehicle booster(boosterLook),ship(shipLook);world->add(booster.node());world->add(ship.node());
    auto camera=PerspectiveCamera::create(40,4.f/3,.05f,12000);
    auto shot=[&](const std::string& name){target.render(*world,*camera);const auto pixels=target.readRGBPixels();const auto path=directory+"/"+name+".ppm";std::ofstream out(path,std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
        for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);std::cout<<path<<", "<<target.scene.drawn()<<" draws\n";};
    auto sample=[&](albert::Vehicle& v,const auto& look,int count,float height,float throttle,double utc){unsigned surfaces=0;for(const auto& p:look.parts)surfaces=std::max(surfaces,p.surface);
        std::vector<float> p(look.engines.size()*3+surfaces+look.thrusters.size()+7);for(int i=0;i<count;++i){const int index=count==5&&i==4?8:i;p[3*index]=throttle;p[3*index+1]=i==0?.025f:0;}
        p[p.size()-7]=101325*std::exp(-height/8500);p.back()=-1;v.animate(&p,utc);};
    double utc=0;
    for(bool dusk:{false,true}){sun->position.set(-350,dusk?-12.f:350.f,-250);
        for(float height:{1000.f,100.f})for(bool isShip:{false,true}){
            auto& v=isShip?ship:booster;const auto& look=isShip?shipLook:boosterLook;booster.node()->visible=!isShip;ship.node()->visible=isShip;v.node()->position.y=height+float(look.length/2);
            for(int count:isShip?std::vector<int>{3,2}:std::vector<int>{13,5,3}){sample(v,look,count,height,.55f,utc);sample(v,look,count,height,.55f,utc+1);utc+=2;
                camera->position.set(95,height+45,145);camera->lookAt(Vector3(-4,height+30,0));
                const std::string name=std::string(isShip?"ship-":"booster-")+std::to_string(count)+"-"+std::to_string(int(height))+"m-"+(dusk?"dusk":"day");shot(name);
                if(!isShip&&count==3&&height==100&&dusk){
                    site.node()->visible=false;camera->position.set(15,height-17,23);camera->lookAt(Vector3(0,height-2,0));shot("booster-3-aft-dusk");
                    camera->position.set(0,height-23,0);camera->lookAt(Vector3(0,height,0));shot("booster-3-axial-dusk");
                    camera->position.set(0,height-3,35);camera->lookAt(Vector3(0,height-3,0));shot("booster-3-side-dusk");v.quality(0);shot("booster-3-side-dusk-low");v.quality(1);
                    site.node()->visible=true;camera->position.set(45,124,80);camera->lookAt(Vector3(-10,116,0));shot("tower-arms-lit-dusk");v.node()->getObjectByName("exhaust light")->visible=false;shot("tower-arms-unlit-dusk");v.node()->getObjectByName("exhaust light")->visible=true;
                }
                if(isShip&&count==2&&height==100&&!dusk){site.node()->visible=false;camera->position.set(12,height-12,18);camera->lookAt(Vector3(0,height,0));shot("ship-2-aft-day");site.node()->visible=true;}
            }
        }
    }
    // A pressure and start transient comparison, with the same actual engine layout.
    booster.node()->visible=true;ship.node()->visible=false;site.node()->visible=false;ground->visible=false;sun->position.set(-350,350,-250);booster.node()->position.y=1000+float(boosterLook.length/2);
    camera->position.set(0,1000,55);camera->lookAt(Vector3(0,990,0));sample(booster,boosterLook,3,30000,.55f,utc);sample(booster,boosterLook,3,30000,.55f,utc+1);shot("booster-3-low-pressure");
    sample(booster,boosterLook,0,1000,0,utc+2);sample(booster,boosterLook,3,1000,.5f,utc+3);sample(booster,boosterLook,3,1000,.5f,utc+3.03);shot("booster-startup-flash");
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<'\n';require(albert::gpu::errors()==0,"Plume captures produced GPU validation errors");
}
#endif
