// Diagnostic samples only: dimensions and engine/actuator ordering come from the real definitions.
#ifdef ALBERT_TEST_RPC
std::vector<float> geometrySample(const albert::core::VehicleLook& look,float angle=0,float flow=0){
    unsigned surfaces=0;for(const auto& p:look.parts)surfaces=std::max(surfaces,p.surface);
    std::vector<float> sample(look.engines.size()*3+surfaces+look.thrusters.size()+7);
    for(std::size_t i=0;i<look.engines.size();++i)sample[3*i]=flow;
    for(unsigned i=0;i<surfaces;++i)sample[look.engines.size()*3+i]=i%2?-angle:angle;
    sample[sample.size()-7]=101325;sample.back()=-1;return sample;
}
// The hardware batch as the GPU poses it: each vertex by its piece's rows in the poses texture (VehicleGeometry).
std::vector<float> posedHardware(threepp::Mesh& mesh){using namespace threepp;const auto& g=*mesh.geometry();const auto* p=g.getAttribute<float>("position");const auto* k=g.getAttribute<float>("piece");
    auto* poses=mesh.material()->as<ShaderMaterial>()->uniforms.at("poses").value<Texture*>();const auto& d=poses->image().data<float>();const unsigned row=4*albert::VehicleGeometry::texels;std::vector<float> out(3*p->count());
    for(int i=0;i<p->count();++i){const float* m=d.data()+row*static_cast<unsigned>(k->getX(i)+.5f);const float x=p->getX(i),y=p->getY(i),z=p->getZ(i);
        for(int r=0;r<3;++r)out[3*i+r]=m[4*r]*x+m[4*r+1]*y+m[4*r+2]*z+m[4*r+3];}
    return out;
}
// Each vertex's red glow as the GPU lights it: its piece's bell burn and lip blended by its weights, over the ambient glow.
std::vector<float> glowingHardware(threepp::Mesh& mesh){using namespace threepp;const auto& g=*mesh.geometry();const auto* w=g.getAttribute<float>("weights");const auto* k=g.getAttribute<float>("piece");
    auto* poses=mesh.material()->as<ShaderMaterial>()->uniforms.at("poses").value<Texture*>();const auto& d=poses->image().data<float>();const unsigned row=4*albert::VehicleGeometry::texels;std::vector<float> out(w->count());
    for(int i=0;i<w->count();++i){const float* m=d.data()+row*static_cast<unsigned>(k->getX(i)+.5f);out[i]=(m[12]+(m[16]-m[12])*w->getY(i))*w->getX(i)+m[20];}
    return out;
}
void vehicleGeometryControls(){
    namespace ex=albert::example::starship;const auto look=captureLook(ex::superHeavy());albert::Vehicle booster(look);
    require(booster.engineCount()==33&&std::count_if(look.parts.begin(),look.parts.end(),[](const auto& p){return p.grid;})==3,"V3 drawn engine or fin count");
    unsigned centre=0,middle=0,outer=0;for(const auto& e:look.engines){const double r=std::hypot(e.position.y,e.position.z);if(r<1)++centre;else if(r<3)++middle;else ++outer;}
    require(centre==3&&middle==10&&outer==20,"Booster engine ring ordering changed");
    auto* lod=booster.node()->getObjectByName("vehicle detail LOD")->as<LOD>();require(lod&&lod->getLevels().size()==2,"Vehicle geometry LOD missing");
    auto* near=lod->getLevels()[0].object->as<Mesh>();auto* far=lod->getLevels()[1].object->as<Mesh>();
    require(near->geometry()->getIndex()->count()>far->geometry()->getIndex()->count()*2,"Coarse vehicle geometry did not reduce triangles");
    auto sample=geometrySample(look);booster.animate(&sample,0);auto* poses=near->material()->as<ShaderMaterial>()->uniforms.at("poses").value<Texture*>();const auto neutral=posedHardware(*near);
    sample=geometrySample(look,.43f);sample[1]=.07f;sample[2]=-.05f;booster.animate(&sample,1);
    require(posedHardware(*near)!=neutral&&albert::Vehicle::gimballing(*booster.node(),look)>.08f,"Reported fins and nozzle gimbals did not pose geometry");
    const auto version=poses->version();booster.animate(&sample,1);require(poses->version()==version,"Frozen geometry uploaded unchanged joint poses");
    const auto* rest=near->geometry()->getAttribute<float>("position");const auto restVersion=rest->version;booster.animate(&sample,2);
    require(rest->version==restVersion,"Posing the hardware rewrote its vertices");
    auto posedNear=Mesh::create(BufferGeometry::create(),near->material());posedNear->geometry()->setIndex(near->geometry()->getIndex()->array());
    posedNear->geometry()->setAttribute("position",FloatBufferAttribute::create(posedHardware(*near),3));posedNear->updateMatrixWorld();
    // A ray through a fin cell must pass; the adjacent steel blade must occlude it.
    auto* fin=booster.node()->getObjectByName("grid fin");booster.node()->updateMatrixWorld();
    const auto& spec=*std::find_if(look.parts.begin(),look.parts.end(),[](const auto& p){return p.grid;});
    auto hits=[&](float x,float z=0){Vector3 q(x,0,z),normal(0,1,0);q.applyQuaternion(fin->quaternion).add(fin->position);normal.applyQuaternion(fin->quaternion);
        Raycaster ray(Vector3(q).add(Vector3(normal).multiplyScalar(1)),Vector3(normal).negate());ray.farPlane=2;return !ray.intersectObject(*posedNear).empty();};
    require(!hits(.30f*.70710678f)&&hits(0)&&hits(0,-float(spec.span)/2+.1f),"Diamond cells or thick grid root are missing");
    require(!hits(float(spec.width)*.49f,float(spec.span)*.49f),"Grid fin retained a rectangular corner");
    // The white actuator hoods must stay on the hull while their adjacent steel sheets turn.
    const auto* finish=near->geometry()->getAttribute<float>("finish");const auto posed=posedHardware(*near);sample=geometrySample(look,-.3f);booster.animate(&sample,2);const auto turned=posedHardware(*near);unsigned fixed=0;
    for(int i=0;i<finish->count();++i)if(std::abs(finish->getY(i)-.12f)<1e-5f){++fixed;for(int j=0;j<3;++j)require(posed[3*i+j]==turned[3*i+j],"Grid actuator hood turned with the spindle");}
    require(fixed>100,"Grid root lost its fixed white hood");
    const auto* colour=near->geometry()->getAttribute<float>("color");unsigned lips=0,wells=0;
    for(int i=0;i<finish->count();++i)if(std::abs(finish->getX(i)-.29f)<1e-5f&&std::abs(finish->getY(i)-.92f)<1e-5f){lips+=colour->getX(i)>.17f;wells+=colour->getX(i)<.03f;}
    require(lips>100&&wells>100,"Batched grid fins lost their bright lips or dark inner wells");
    // A lateral ray above the concave lip passes at mid-cell, where the straight LOD sheet still blocks it.
    auto sheet=MeshBasicMaterial::create();sheet->side=Side::Double;
    for(int tier=0;tier<2;++tier){const auto grid=albert::gridFin(float(spec.width),float(spec.span),float(spec.thickness),tier);auto mesh=Mesh::create(grid.geometry,sheet);mesh->updateMatrixWorld();
        const Vector3 across(-.70710678f,0,.70710678f);auto lip=[&](float y){const Vector3 centre(.30f*.35355339f,y,.30f*.35355339f);Raycaster ray(Vector3(centre).addScaledVector(across,.08f),Vector3(across).negate());ray.farPlane=.16f;return !ray.intersectObject(*mesh).empty();};
        require(lip(0)&&lip(.24f)==(tier==1),"Grid sheets lost their open concave lip or straight LOD");
        const auto* vertices=grid.geometry->getAttribute<float>("position");for(int i=0;i<vertices->count();++i)
            require(std::abs(vertices->getX(i))<=spec.width/2+.0001&&std::abs(vertices->getY(i))<=spec.thickness/2+.0001&&std::abs(vertices->getZ(i))<=spec.span/2+.0001,"Grid sheets exceed the declared fin envelope");}
    // Radial rays cross an opening above the dome, then hit its neighbouring ligament.
    const auto& ring=look.sections.front();const float y=float(look.length/2-ring.from-.45),r=float(look.diameter/2);
    auto radial=[&](float angle){Vector3 d(std::sin(angle),0,std::cos(angle));Raycaster ray(Vector3(d).multiplyScalar(r+1).setY(y),Vector3(d).negate());ray.farPlane=1.5f;return !ray.intersectObject(*near).empty();};
    require(!radial(3.14159265f/20)&&radial(0),"Hot-staging apertures do not pass rays");
    booster.quality(0);require(!near->visible&&far->visible&&!lod->autoUpdate,"Low quality retained near hardware");booster.quality(1);require(lod->autoUpdate,"High quality failed to restore automatic LOD");
    const auto shipLook=captureLook(ex::stack(),1);albert::Vehicle ship(shipLook);require(ship.engineCount()==6,"Ship engine count changed");
    auto flapSample=geometrySample(shipLook,.4f);ship.animate(&flapSample,0);unsigned flaps=0;
    for(auto* p:ship.node()->children)if(p->name.starts_with("flap ")&&p->name!="flap fairing"){++flaps;require(p->children.size()==2,"Flap lost its tile face or plasma sheath");}
    require(flaps==4,"Forward or aft flap missing");
    std::cout<<"Vehicle geometry: booster "<<near->geometry()->getIndex()->count()/3<<" / "<<far->geometry()->getIndex()->count()/3<<" near/far hardware triangles, 3 fins, 33 engines, open lattice/vents, actuator samples verified\n";
}
void vehicleGeometryGraphics(GpuTarget& target){
    namespace ex=albert::example::starship;const auto look=captureLook(ex::superHeavy());albert::Vehicle booster(look);auto world=Scene::create();world->background=Color(.04f,.07f,.12f);world->add(booster.node());
    auto sun=DirectionalLight::create(Color(0xffffff),2);sun->position.set(30,40,50);world->add(sun);world->add(AmbientLight::create(Color(0xbacbdf),1));
    auto camera=PerspectiveCamera::create(50,1,.05,10000);camera->position.set(30,35,50);camera->lookAt(Vector3(0,28,0));
    auto sample=geometrySample(look);booster.animate(&sample,0);target.render(*world,*camera);const auto straight=target.readRGBPixels();
    sample=geometrySample(look,.45f);booster.animate(&sample,1);target.render(*world,*camera);const auto turned=target.readRGBPixels();require(turned!=straight,"Fin deflection did not reach the WebGPU image");
    target.render(*world,*camera);require(target.readRGBPixels()==turned,"Frozen detailed geometry changed between frames");
    auto* lod=booster.node()->getObjectByName("vehicle detail LOD")->as<LOD>();require(lod->getCurrentLevel()==0,"Close vehicle selected far geometry");
    sample[0]=1;booster.animate(&sample,1.01);const auto glow=glowingHardware(*lod->getLevels()[0].object->as<Mesh>());unsigned glowing=0;
    for(const float e:glow)if(e>.01f)++glowing;
    require(glowing>10&&glowing<static_cast<unsigned>(glow.size()/10),"Individual bell heat leaked to unlit neighbours in the batch");
    camera->position.set(0,0,2000);camera->lookAt(Vector3());target.render(*world,*camera);target.readRGBPixels();require(lod->getCurrentLevel()==1,"2 km vehicle retained full geometry");
    const float scale=1.f/6378137;booster.node()->scale.setScalar(scale);camera->position.multiplyScalar(scale);camera->nearPlane*=scale;camera->updateProjectionMatrix();target.render(*world,*camera);target.readRGBPixels();require(lod->getCurrentLevel()==1,"Earth-unit scale changed vehicle LOD");
    camera->position.set(30*scale,35*scale,50*scale);camera->lookAt(Vector3(0,28*scale,0));target.render(*world,*camera);target.readRGBPixels();require(lod->getCurrentLevel()==0,"Earth-unit close geometry failed to restore");
    booster.quality(0);target.render(*world,*camera);target.readRGBPixels();require(!lod->getLevels()[0].object->visible&&lod->getLevels()[1].object->visible,"Low-quality geometry was overridden by camera LOD");
}
void vehicleGeometryCaptures(const std::string& directory){
    namespace ex=albert::example::starship;std::filesystem::create_directories(directory);GpuTarget target(1280,960);target.scene.toneMapping=true;target.scene.samples=4;
    auto world=Scene::create();world->background=Color(.035f,.055f,.085f);
    auto sun=DirectionalLight::create(Color(0xffead1),3);sun->position.set(30,40,50);world->add(sun);world->add(AmbientLight::create(Color(0xbacbdf),1.2f));
    const auto boosterLook=captureLook(ex::superHeavy()),shipLook=captureLook(ex::stack(),1),stackLook=captureLook(ex::stack());
    albert::Vehicle booster(boosterLook),ship(shipLook),stack(stackLook);world->add(booster.node());world->add(ship.node());world->add(stack.node());
    auto bs=geometrySample(boosterLook,.45f),ss=geometrySample(shipLook,.35f),ts=geometrySample(stackLook);booster.animate(&bs,0);ship.animate(&ss,0);stack.animate(&ts,0);
    auto camera=PerspectiveCamera::create(48,4.f/3,.03,20000);
    auto view=[&](albert::Vehicle& vehicle,Vector3 aim,Vector3 eye,float fov=48){booster.node()->visible=&vehicle==&booster;ship.node()->visible=&vehicle==&ship;stack.node()->visible=&vehicle==&stack;
        camera->fov=fov;camera->position.copy(eye);camera->lookAt(aim);camera->updateProjectionMatrix();};
    auto report=[&](const char* name,bool save=true){std::vector<double> times;unsigned triangles=0;std::size_t bytes=0;
        for(int i=0;i<8;++i){target.render(*world,*camera);const auto pixels=target.readRGBPixels();if(i>=3&&target.scene.gpuMilliseconds()>=0)times.push_back(target.scene.gpuMilliseconds());
            if(i==7&&save){const auto path=directory+"/"+name+".ppm";std::ofstream out(path,std::ios::binary);out<<"P6\n"<<target.width<<' '<<target.height<<"\n255\n";
                for(unsigned y=0;y<target.height;++y)out.write(reinterpret_cast<const char*>(pixels.data()+(target.height-1-y)*target.width*3),target.width*3);}}
        world->traverseVisible([&](Object3D& o){if(auto* mesh=o.as<Mesh>();mesh&&mesh->geometry()&&mesh->geometry()->hasAttribute("position")){
            const auto& g=*mesh->geometry();triangles+=(g.getIndex()?g.getIndex()->count():g.getAttribute<float>("position")->count())/3;
            for(const auto* name:{"position","normal","color","emission","finish","uv"})if(const auto* a=g.getAttribute(name))bytes+=a->count()*a->itemSize()*sizeof(float);if(g.getIndex())bytes+=g.getIndex()->count()*sizeof(unsigned);}});
        std::sort(times.begin(),times.end());std::cout<<name<<": "<<target.scene.drawn()<<" draws, "<<triangles<<" triangles, "<<bytes<<" geometry bytes, GPU median "<<(times.empty()?-1:times[times.size()/2])<<" ms\n";
    };
    const float bh=float(boosterLook.length/2),sh=float(shipLook.length/2);
    view(booster,{-6.35f,bh-7.5f,0},{-13,bh+4,-12},55);report("grid-fins-deflected");booster.quality(0);report("grid-fins-low");booster.quality(1);
    view(booster,{0,bh-1,0},{13,bh+1,19},48);report("hot-staging-ring");
    view(booster,{3,bh-43,0},{28,bh-40,30},50);report("booster-chine");
    view(booster,{-3,bh-43,0},{-28,bh-40,-30},50);report("booster-chine-opposite");
    view(ship,{-4,sh-8,-1},{-18,sh-3,-17},47);report("ship-forward-flap");
    view(ship,{-6,sh-44.5f,1},{-14,sh-41,12},50);report("ship-aft-flap");
    sun->position.set(10,-35,20);view(booster,{0,-bh+1,0},{4,-bh-12,5},54);report("booster-engine-bay");
    bs[1]=.16f;bs[2]=-.12f;booster.animate(&bs,1);report("booster-engine-gimbal");
    view(booster,{0,-bh+.5f,0},{4,-bh+.8f,0},105);report("booster-engine-bay-close");
    view(ship,{0,-sh+.2f,0},{4,-sh+.8f,0},105);report("ship-engine-bay-close");
    view(ship,{0,-sh+1,0},{4,-sh-10,5},58);report("ship-engine-bay");
    ss[1]=.14f;ss[2]=-.10f;ss[0]=1;ss[9]=1;ship.animate(&ss,1);ship.animate(&ss,2);report("ship-engines-selective");
    // Hot staging: the same detached bodies, with an explicitly posed ship sample above the ring.
    ss=geometrySample(shipLook,0,1);ship.animate(&ss,3);ship.animate(&ss,4);ship.node()->position.y=bh+sh+2.0f;booster.node()->visible=ship.node()->visible=true;stack.node()->visible=false;
    sun->position.set(30,40,50);camera->position.set(13,bh+1,19);camera->lookAt(Vector3(0,bh,0));camera->fov=52;camera->updateProjectionMatrix();report("hot-staging-open-plumes");ship.node()->position.y=0;
    view(stack,{0,0,0},{120,35,156.12495f},48);require(std::abs(camera->position.length()-200)<.1f,"Stack capture is not 200 m away");report("stack-200m");
    stack.quality(0);report("stack-200m-low");stack.quality(1);
    view(stack,{0,0,0},{1200,350,1561.2495f},48);report("stack-2km");
    // Same 2 km framing forced to high geometry, a controlled hardware LOD comparison.
    auto* lod=stack.node()->getObjectByName("vehicle detail LOD")->as<LOD>();lod->autoUpdate=false;lod->getLevels()[0].object->visible=true;lod->getLevels()[1].object->visible=false;report("stack-2km-forced-high");lod->autoUpdate=true;
    // Isolate the incremental hardware draw at a close aft view. Both runs keep the same hull and lighting.
    view(booster,{0,-bh+1,0},{4,-bh-12,5},54);sun->position.set(10,-35,20);report("cost-booster-full",false);
    auto* boosterLod=booster.node()->getObjectByName("vehicle detail LOD");boosterLod->visible=false;report("cost-booster-hull-only",false);boosterLod->visible=true;
    auto cpu=[&](bool moving){std::vector<double> us;auto sample=geometrySample(boosterLook);
        for(int i=0;i<100;++i){if(moving){const float angle=.4f*std::sin(i*.13f);for(unsigned j=0;j<3;++j)sample[boosterLook.engines.size()*3+j]=angle*(j%2?-1:1);
                for(unsigned j=0;j<13;++j){sample[j*3+1]=.08f*std::sin(i*.17f+j);sample[j*3+2]=.08f*std::cos(i*.11f+j);}}
            const auto start=std::chrono::steady_clock::now();booster.animate(&sample,10);const double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();if(i>9)us.push_back(elapsed);}
        std::sort(us.begin(),us.end());std::cout<<"Booster animate "<<(moving?"moving 3 fins / 13 engines":"frozen joints")<<" median "<<us[us.size()/2]<<" us (both geometry tiers and existing engine effects)\n";};cpu(false);cpu(true);
    std::cout<<"Adapter "<<target.context.adapter()<<", validation errors "<<albert::gpu::errors()<<'\n';require(albert::gpu::errors()==0,"Geometry captures produced GPU validation errors");
}
#endif
