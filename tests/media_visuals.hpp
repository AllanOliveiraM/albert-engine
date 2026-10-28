// Compare the real reduced target to a native reference, including occlusion, inside rays,
// MSAA and reuse of the scratch target. These bypass the pending vehicle vent check.
void mediaGraphics(){
    GpuTarget target(1024,576);auto world=Scene::create();world->background=Color(0x243849);
    world->add(AmbientLight::create(Color(0xffffff),1));auto sun=DirectionalLight::create(Color(0xffffff),2);sun->position.set(-400,-300,700);world->add(sun);
    albert::LaunchCloud cloud({0,0,0},{0,1,0});world->add(cloud.node());for(int i=0;i<240;i++)cloud.update(1,41,1.f/30,{0,0,0},true);
    auto blocker=Mesh::create(BoxGeometry::create(20,20,130),MeshBasicMaterial::create());blocker->material()->as<MeshBasicMaterial>()->color=Color(0x203040);blocker->position.set(0,-80,65);world->add(blocker);
    auto camera=PerspectiveCamera::create(55,16.f/9,.1f,5000);camera->up.set(0,0,1);camera->position.set(230,-400,160);camera->lookAt(Vector3(0,0,45));
    for(unsigned samples:{1u,4u})for(bool inside:{false,true}){
        if(inside){camera->position.set(0,-20,25);camera->lookAt(Vector3(0,80,25));blocker->visible=false;}
        else{camera->position.set(230,-400,160);camera->lookAt(Vector3(0,0,45));blocker->visible=true;}
        target.scene.samples=samples;target.scene.reducedMedia=false;target.render(*world,*camera);const auto native=target.readRGBPixels();
        target.scene.reducedMedia=true;target.render(*world,*camera);const auto reduced=target.readRGBPixels();double error=0;unsigned changed=0;
        for(size_t i=0;i<native.size();++i){error+=std::abs(int(native[i])-int(reduced[i]));changed+=std::abs(int(native[i])-int(reduced[i]))>20;}
        error/=native.size();std::cout<<"Media reference: samples "<<samples<<", inside "<<inside<<", MAE "<<error<<", channels >20 "<<changed<<'\n';
        require(error<2.5&&changed<native.size()/50,"Reduced media changed the native cloud appearance");
        target.render(*world,*camera);require(target.readRGBPixels()==reduced,"Stationary reduced media flickered");
    }
    cloud.node()->visible=false;blocker->visible=false;
    albert::Splashdown steam(50,4.5f);world->add(steam.node());for(int i=0;i<=300;++i)steam.update({0,0,50},{0,0,1},.6f,false,i/30.,{3,0,0});
    camera->position.set(130,-200,90);camera->lookAt(Vector3(0,0,15));target.scene.reducedMedia=false;target.render(*world,*camera);const auto nativeSteam=target.readRGBPixels();
    target.scene.reducedMedia=true;target.render(*world,*camera);const auto lowSteam=target.readRGBPixels();double steamError=0;
    for(size_t i=0;i<lowSteam.size();++i)steamError+=std::abs(int(lowSteam[i])-int(nativeSteam[i]));steamError/=lowSteam.size();
    std::cout<<"Media steam reference MAE "<<steamError<<'\n';require(steamError<2.5,"Reduced steam changed the native appearance");steam.node()->visible=false;
    albert::core::VehicleLook look;look.length=32;look.diameter=6;look.sections={{0,32,3,3}};look.tanks={{2,30,false}};look.engines.push_back({{32,0,0},{1,0,0},.6});
    albert::Vehicle vehicle(look);vehicle.humidity(.95f);std::vector<float> parts(10);parts[3]=80000;parts[5]=.3f;parts[6]=.9f;parts[7]=3.14159265f;parts.back()=-1;vehicle.animate(&parts,0);
    for(auto* child:vehicle.node()->children)child->visible=child->name=="transonic condensation";world->add(vehicle.node());world->background=Color(0);
    camera->up.set(0,1,0);camera->position.set(18,8,35);camera->lookAt(Vector3(0,8,0));target.scene.reducedMedia=false;target.render(*world,*camera);const auto nativeMist=target.readRGBPixels();
    target.scene.reducedMedia=true;target.render(*world,*camera);const auto lowMist=target.readRGBPixels();double originalEnergy=0,reducedEnergy=0;
    for(size_t i=0;i<lowMist.size();++i){originalEnergy+=nativeMist[i];reducedEnergy+=lowMist[i];}
    std::cout<<"Media analytic mist energy ratio "<<reducedEnergy/std::max(1.,originalEnergy)<<'\n';require(originalEnergy>1000&&std::abs(reducedEnergy/originalEnergy-1)<.05,"Reduced media lost the thin analytic condensation cone");vehicle.node()->visible=false;
    // The scratch target is not created when the effects are invisible.
    cloud.node()->visible=false;blocker->visible=false;target.render(*world,*camera);const auto hidden=target.readRGBPixels();
    target.scene.reducedMedia=false;target.render(*world,*camera);require(target.readRGBPixels()==hidden,"Invisible media retained a composite");
    require(albert::gpu::errors()==0,"Reduced media produced WebGPU validation errors");
}
