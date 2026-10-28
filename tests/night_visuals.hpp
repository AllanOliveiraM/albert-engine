#pragma once
void nightGraphics(Target& renderer){
    albert::core::Scenery scenery;
    auto part=[&](const char* name,albert::core::Vector at,albert::core::Vector size){albert::core::SceneryPart p;p.name=name;p.centre=at;p.size=size;p.color=0x999999;scenery.parts.push_back(p);};
    for(double x:{-48.,48.})for(double y:{-38.,38.}){part("pad floodlight pole",{x,y,14},{.5,.5,26});part("pad floodlight",{x,y,27},{1,.4,.8});}
    part("tower aviation beacon",{0,0,160},{.28,.28,.32});part("farm horizontal tank",{0,80,0},{5,24,6});
    albert::Site site(scenery);auto world=Scene::create();world->background=Color(0);world->add(site.node());
    auto groundMaterial=MeshStandardMaterial::create();groundMaterial->color=Color(.5f,.5f,.5f);groundMaterial->roughness=.8f;
    auto ground=Mesh::create(PlaneGeometry::create(500,500),groundMaterial);world->add(ground);
    albert::core::VehicleLook look;look.length=60;look.diameter=9;look.sections.push_back({0,60,4.5,4.5,0xb7bec3,0});
    albert::Vehicle steelVehicle(look);auto receiver=steelVehicle.node();receiver->rotation.x=std::numbers::pi/2;receiver->position.z=30;world->add(receiver);
    auto camera=OrthographicCamera::create(-100,100,100,-100,.1,1000);camera->up.set(0,0,1);camera->position.set(150,-200,160);camera->lookAt(Vector3(0,0,20));
    const Vector3 day(0,0,10000),dusk(10000,0,0),night(0,0,-10000);
    site.lighting(day);require(site.night()==0,"Site lights stayed on by day");renderer.render(*world,*camera);const auto off=renderer.readRGBPixels();
    site.lighting(dusk);require(site.night()>.3f&&site.night()<.6f,"Dusk transition is not driven by solar elevation");
    site.lighting(night);require(site.night()==1,"Site lights failed to turn on at night");renderer.render(*world,*camera);const auto lit=renderer.readRGBPixels();
    int brighter=0,clipped=0;for(std::size_t i=0;i<lit.size();i+=3){brighter+=lit[i]>off[i]+15;clipped+=lit[i]>=254&&lit[i+1]>=254&&lit[i+2]>=254;}
    std::cout<<"Night lighting pixels "<<brighter<<", clipped white "<<clipped<<"\n";
    require(brighter>1200&&clipped<400,"Night receivers stayed black or site exposure blew out");
    ground->visible=false;renderer.render(*world,*camera);const auto steel=renderer.readRGBPixels();receiver->visible=false;renderer.render(*world,*camera);const auto noSteel=renderer.readRGBPixels();
    int metalPixels=0;for(std::size_t i=0;i<steel.size();i+=3)metalPixels+=steel[i]>noSteel[i]+15;
    require(metalPixels>80,"Detailed stainless vehicle failed to receive the floodlights");ground->visible=receiver->visible=true;
    site.quality(0);unsigned active=0;site.node()->traverseVisible([&](Object3D& o){if(dynamic_cast<PointLight*>(&o))++active;});require(active==1,"Low quality did not reduce pad sources");renderer.render(*world,*camera);
    site.quality(1);site.lighting(day);renderer.render(*world,*camera);require(renderer.readRGBPixels()==off,"Day failed to restore lamp-off rendering");
    // A floating origin, tilted ENU frame and Earth-radius scale must agree on dusk.
    site.node()->position.set(3,-5,2);site.node()->scale.setScalar(1.f/6378137);site.node()->quaternion.setFromAxisAngle(Vector3(1,0,0),1.2f);site.node()->updateMatrixWorld();
    Vector3 transformed=night;transformed.applyMatrix4(*site.node()->matrixWorld);site.lighting(transformed);require(site.night()>.999f,"Night switch lost the scaled/rotated site frame");
    site.node()->position.set(0,0,0);site.node()->scale.setScalar(1);site.node()->quaternion.identity();site.lighting(night);
    auto distantCamera=PerspectiveCamera::create(35,1,.1,10000);distantCamera->up.set(0,0,1);distantCamera->position.set(0,-5000,160);distantCamera->lookAt(Vector3(0,0,160));
    ground->visible=receiver->visible=false;renderer.render(*world,*distantCamera);const auto distant=renderer.readRGBPixels();int red=0;for(std::size_t i=0;i<distant.size();i+=3)red+=distant[i]>60&&distant[i]>distant[i+1]*3;
    std::cout<<"Distant red pixels "<<red<<", peak "<<int(*std::max_element(distant.begin(),distant.end()))<<"\n";
    require(red>=2,"Aviation lights disappeared at five kilometres");
}
