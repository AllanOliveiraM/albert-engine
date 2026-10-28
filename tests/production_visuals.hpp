#pragma once
#ifdef ALBERT_TEST_RPC
std::vector<albert::core::SceneryBuilding> productionBuildings(const albert::v1::CreateFlightRequest& request){
    std::vector<albert::core::SceneryBuilding> buildings;
    auto cv=[](const albert::v1::Vector3& v){return albert::core::Vector{v.x(),v.y(),v.z()};};
    for(const auto& b:request.buildings()){albert::core::SceneryBuilding q;q.centre=cv(b.centre_enu_meters());q.size=cv(b.size_meters());q.heading=b.heading_degrees()*std::numbers::pi/180;
        q.style=static_cast<albert::core::SceneryBuilding::Style>(b.style());q.name=b.name();q.osm=b.osm_way();
        for(const auto& r:b.rings()){auto& ring=q.rings.emplace_back();for(const auto& p:r.points_enu_meters())ring.push_back(cv(p));}buildings.push_back(std::move(q));}
    return buildings;
}
void productionData(){
    namespace ex=albert::example::starship;albert::v1::CreateFlightRequest request;ex::addTower(request,true);
    require(request.scenery_size()==1259&&request.structures_size()==9,"Production scenery changed the pad/contact fixture");
    require(request.buildings_size()==458,"Lost a production building, disjoint outer ring, or tank");
    require(request.roads_size()<=512,"Production road API budget exceeded");
    std::size_t points=0;for(const auto& r:request.roads())points+=r.points_enu_meters_size();require(points<=4096,"Production road point budget exceeded");
    unsigned factory=0;bool courtyard=false;for(const auto& b:request.buildings()){
        if(b.name()=="Starfactory"){++factory;require(std::abs(b.size_meters().z()-16.3)<.001,"Factory tagged height lost");}
        if(b.name()=="Mega Bay"||b.name()=="Mega Bay 2")require(b.size_meters().z()==100,"Mega Bay tagged height lost");
        if(b.name()=="Gigabay")require(b.size_meters().z()==116,"Gigabay tagged height lost");
        if(b.name()=="Hikaru")courtyard=b.rings_size()==2;
        require(b.centre_enu_meters().x()<-900,"Production coordinates are not west of the pads");}
    require(factory==2&&courtyard,"OSM multipolygon assembly lost a factory wing or courtyard");
}
#endif
void productionGraphics(Target& renderer){
    albert::core::Scenery scenery;auto building=[&](std::vector<albert::core::Vector> ring){albert::core::SceneryBuilding b;b.rings.push_back(std::move(ring));b.size={20,20,10};scenery.buildings.push_back(std::move(b));};
    building({{0,0,0},{20,0,0},{20,8,0},{8,8,0},{8,20,0},{0,20,0}});
    building({{30,0,0},{48,0,0},{48,18,0},{30,18,0}});scenery.buildings.back().rings.push_back({{34,4,0},{34,14,0},{44,14,0},{44,4,0}});
    albert::Site site(scenery);auto world=Scene::create();world->background=Color(0);world->add(site.node());auto sun=DirectionalLight::create(Color(0xffffff),3);sun->position.set(20,0,200);world->add(sun);
    auto camera=OrthographicCamera::create(-10,50,30,-30,.1,500);camera->position.set(0,0,100);camera->lookAt(Vector3());site.view(*camera);renderer.render(*world,*camera);
    auto lit=renderer.readRGBPixels();auto value=[&](const auto& pixels,float x,float y){const int ix=int((x+10)/60*192),iy=int((y+30)/60*192),i=3*(iy*192+ix);return int(pixels[i])+pixels[i+1]+pixels[i+2];};
    require(value(lit,4,15)>50&&value(lit,15,15)<6,"Concave building roof filled its L-shaped notch");
    require(value(lit,31.5f,9)>50&&value(lit,39,9)<6,"Building roof filled its courtyard hole");
    auto* near=site.node()->getObjectByName("production near");auto* far=site.node()->getObjectByName("production distant");require(near&&far&&near->visible&&!far->visible,"Near production LOD missing");
    site.quality(0);require(!near->visible&&far->visible,"Low quality retained production detail");renderer.render(*world,*camera);const auto low=renderer.readRGBPixels();
    require(value(low,4,15)>50&&value(low,15,15)<6&&value(low,39,9)<6,"Low LOD lost polygon footprints");
    site.quality(1);site.node()->position.set(10,30,0);site.node()->scale.setScalar(.001f);camera->position.set(10,30,3);site.view(*camera);
    require(!near->visible&&far->visible,"Distant LOD ignored the site's world scale");camera->position.z=1;site.view(*camera);require(near->visible&&!far->visible,"Approaching failed to restore production detail");
    site.node()->scale.setScalar(1);site.node()->position.set(0,0,0);camera->position.set(0,0,100);site.view(*camera);sun->intensity=0;renderer.render(*world,*camera);const auto night=renderer.readRGBPixels();
    require(std::count_if(night.begin(),night.end(),[](auto c){return c>20;})==0,"Production buildings emit light without the sun");
}
