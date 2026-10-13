#include "core/layers.hpp"
#include <cmath>
#include <iostream>
#include <limits>
using namespace albert::core;
void need(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class F>void rejects(F fn){bool bad=false;try{fn();}catch(const std::exception&){bad=true;}need(bad,"Invalid layer accepted");}
int main(){try{
    const auto equator=earthSurfacePoint({0,0}),pole=earthSurfacePoint({0,90}),east=earthSurfacePoint({90,0});need(equator.x==6378137&&std::abs(pole.z-6356752.314245)<1e-6&&std::abs(east.y-6378137)<1e-6,"WGS84 coordinate conversion failed");
    Layer line;line.id="dateline";line.name="Dateline";line.frame=LayerFrame::EarthSurface;line.kind=LayerKind::Polyline;
    std::vector<Geographic> geo{{179,10},{-179,10}};auto prepared=prepareLayer(line,{},geo);need(prepared.anchor=="earth"&&prepared.data->positions.size()==41,"Dateline took the long path");for(const auto& p:prepared.data->positions)need(p.x<0,"Geographic layer crossed Greenwich incorrectly");
    Scene scene;scene.createLayer(prepared);const auto revision=scene.revision();rejects([&]{scene.createLayer(prepared);});need(scene.revision()==revision,"Rejected layer changed revision");scene.layerVisible(line.id,false);need(!scene.snapshot(0,{}).layers.front().visible,"Layer visibility lost in snapshot");scene.removeLayer(line.id);
    std::vector<Vector> points{{1e11+.25,0,0}};Layer spatial;spatial.id="space";spatial.name="Spatial";auto absolute=prepareLayer(spatial,points);need(absolute.data->positions.front().x==1e11+.25,"Layer lost SI double precision");scene.createLayer(absolute);scene.removeLayer(spatial.id);
    spatial.frame=LayerFrame::ObjectLocal;spatial.anchor="missing";rejects([&]{scene.createLayer(prepareLayer(spatial,points));});Object vehicle;vehicle.id="vehicle";vehicle.name="Vehicle";vehicle.samples={{100,{}}};scene.create(vehicle);spatial.anchor=vehicle.id;scene.createLayer(prepareLayer(spatial,points));scene.remove(vehicle.id);need(scene.layers().empty(),"Removing anchor retained dependent layer");
    geo[0].latitude=91;rejects([&]{prepareLayer(line,{},geo);});geo[0].latitude=10;
    rejects([&]{prepareLayer(line,points,geo);});points.front().x=std::numeric_limits<double>::quiet_NaN();rejects([&]{prepareLayer(spatial,points);});points.front().x=0;
    spatial.frame=LayerFrame::GeocentricICRF;spatial.anchor.clear();spatial.widthPixels=0;rejects([&]{scene.createLayer(prepareLayer(spatial,points));});spatial.widthPixels=6;
    for(unsigned i=0;i<Scene::maxLayers;++i){spatial.id="point"+std::to_string(i);scene.createLayer(prepareLayer(spatial,points));}spatial.id="overflow";rejects([&]{scene.createLayer(prepareLayer(spatial,points));});
    std::cout<<"Layers OK: WGS84, dateline, SI precision, ownership, atomic validation, visibility and bounded resources\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
