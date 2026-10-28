#pragma once
#ifdef ALBERT_TEST_RPC
void industrialData(){
    namespace ex=albert::example::starship;albert::v1::CreateFlightRequest request;ex::addTower(request,true);
    unsigned tanks=0,buildings=0,canopies=0;for(const auto& p:request.scenery()){
        if(!p.name().starts_with("farm "))continue;
        require(p.structure()==0,"Industrial scenery acquired a flight contact carrier");
        if(p.name()=="farm canopy")++canopies;
        if(p.name().starts_with("farm building ")||p.name()=="farm vaporizer")++buildings;
        if(p.name()=="farm horizontal tank"||p.name()=="farm vertical tank"){
            ++tanks;const ex::OsmPart* source=nullptr;for(const auto& row:ex::starbaseOsm)if(std::abs(row.x-p.centre_enu_meters().x())<.001&&std::abs(row.y-p.centre_enu_meters().y())<.001)source=&row;
            require(source,"Tank lost its original OSM centre");const std::string contents=source->name;
            if(contents.find("nitrogen")!=std::string::npos)require(p.color()==0xb9bdc0,"Nitrogen tank lost its bare steel finish");
            if(contents.find("oxygen")!=std::string::npos||contents.find("methane")!=std::string::npos)require(p.color()==0xe4e4e2,"Cryogenic propellant tank lost white insulation");
        }
    }
    unsigned expected=0,expectedBuildings=0;for(const auto& row:ex::starbaseOsm){const std::string kind=row.kind;if(kind=="tank"||kind=="lying tank")++expected;if(kind=="building"&&std::string(row.name)!="Starhopper")++expectedBuildings;}
    require(tanks==expected&&buildings==expectedBuildings&&canopies==1,"Industrial expansion lost an OSM tank/building or the covered bank");
    require(request.structures_size()==9&&request.scenery_size()<=2048,"Industrial detail changed contact structures or exceeded scenery budget");
}
#endif
void industrialGraphics(Target& renderer){
    albert::core::Scenery scenery;
    auto part=[&](const char* name,albert::core::Vector at,albert::core::Vector size,std::uint32_t colour=0xe4e4e2){albert::core::SceneryPart p;p.name=name;p.centre=at;p.size=size;p.color=colour;scenery.parts.push_back(p);};
    part("farm horizontal tank",{-8,0,0},{5,24,5});part("farm horizontal tank",{0,0,0},{5,24,5},0xb9bdc0);
    part("farm vertical tank",{9,7,0},{3,3,6});part("farm vaporizer",{9,0,0},{2.2,2.5,6});
    part("farm building Control Center",{12,-10,0},{7,9,4});part("farm pipe trestle",{-8,-19,0},{5,8,2.6});
    albert::Site site(scenery);auto world=Scene::create();world->background=Color(0);world->add(site.node());
    auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(-80,-100,200);world->add(sun);
    auto camera=PerspectiveCamera::create(55,1,.1,500);camera->up.set(0,0,1);camera->position.set(37,-48,30);camera->lookAt(Vector3(1,-3,3));
    auto* shells=site.node()->getObjectByName("tank farm shells");auto* steel=site.node()->getObjectByName("tank farm steel");auto* fine=site.node()->getObjectByName("tank farm fittings");
    require(shells&&steel&&fine&&site.node()->children.size()==3,"Industrial scenery is not merged into three draws");
    for(auto* child:site.node()->children){auto* positions=child->as<Mesh>()->geometry()->getAttribute<float>("position");
        for(int i=0;i<positions->count();++i)require(std::isfinite(positions->getX(i))&&std::isfinite(positions->getY(i))&&std::isfinite(positions->getZ(i)),"Industrial mesh contains a nonfinite vertex");}
    renderer.render(*world,*camera);const auto lit=renderer.readRGBPixels();
    require(std::count_if(lit.begin(),lit.end(),[](auto x){return x>35;})>1200,"Industrial tanks/buildings are not visible under sunlight");
    site.quality(0);require(!fine->visible&&shells->visible&&steel->visible,"Low quality lost industrial silhouettes or retained fittings");
    renderer.render(*world,*camera);const auto low=renderer.readRGBPixels();require(low!=lit,"Industrial quality did not change rendered detail");
    require(std::count_if(low.begin(),low.end(),[](auto x){return x>35;})>1000,"Low quality removed the industrial site");
    site.quality(1);require(fine->visible,"Industrial fittings did not return at high quality");sun->intensity=0;renderer.render(*world,*camera);const auto night=renderer.readRGBPixels();
    require(std::none_of(night.begin(),night.end(),[](auto x){return x>20;}),"Industrial detail emits light without illumination");
}
