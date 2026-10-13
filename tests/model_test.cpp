#include "core/model.hpp"
#include "model_fixture.hpp"
#include <cmath>
#include <iostream>
#include <random>
using namespace albert::core;using namespace model_fixture;
void need(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
template<class F>void rejects(F fn){bool ok=false;try{fn();}catch(const std::exception&){ok=true;}need(ok,"Invalid GLB was accepted");}
int main(int argc,char**){try{
    const auto bytes=glb();const auto model=decodeModel("triangle","Fixture",bytes);need(model->vertices==3&&model->triangles==1&&model->parts.size()==1,"Model counts lost");need(model->radiusUnits==4&&model->sizeUnits.x==2&&model->sizeUnits.y==2,"Model hierarchy/bounds lost");need(model->parts[0].positions[1]==2&&model->parts[0].positions[7]==4,"Model translation lost");
    auto j=document();j["nodes"]=J::array({J{{"translation",{3,0,0}},{"children",{1}}},J{{"mesh",0},{"scale",{-2,3,1}}}});auto nested=decodeModel("nested","",glb(j));need(nested->parts[0].positions[0]==5&&nested->parts[0].positions[1]==0&&nested->parts[0].indices[1]==2,"Nested/reflected transform or winding lost");
    j=document();j["nodes"][0]["children"]={0};rejects([&]{decodeModel("cycle","",glb(j));});
    j=document();j["scenes"][0]["nodes"]={0,0};rejects([&]{decodeModel("duplicate","",glb(j));});
    j=document();j["buffers"][0]["uri"]="../../outside.bin";rejects([&]{decodeModel("external","",glb(j));});
    j=document();j["images"]=J::array({J{{"uri","https://example.test/image.png"}}});rejects([&]{decodeModel("image","",glb(j));});
    j=document();j["accessors"][0]["byteOffset"]=8;rejects([&]{decodeModel("range","",glb(j));});
    j=document();j["accessors"][0]["count"]=-1;rejects([&]{decodeModel("count","",glb(j));});
    j=document();j["nodes"][0]["scale"]={0,1,1};rejects([&]{decodeModel("singular","",glb(j));});
    j=document();j["meshes"][0]["primitives"][0]["extensions"]=J::object();rejects([&]{decodeModel("extension","",glb(j));});
    j=document();J depth=0;for(int i=0;i<80;++i)depth=J::array({depth});j["extras"]=depth;rejects([&]{decodeModel("depth","",glb(j));});
    auto nan=binary();nan[2]=0xc0;nan[3]=0x7f;rejects([&]{decodeModel("nan","",glb(document(),nan));});
    auto truncated=bytes;truncated.pop_back();rejects([&]{decodeModel("truncated","",truncated);});
    auto huge=bytes;huge.resize(maxModelBytes+1);rejects([&]{decodeModel("huge","",huge);});
    Scene scene;scene.installModel(model);Object o;o.id="vehicle";o.name="Vehicle";o.appearance.shape=Shape::Model;o.appearance.modelId=model->id;o.appearance.metersPerUnit=30;o.samples={{100,{1e11,0,0}}};scene.create(o);need(scene.get(o.id).radiusMeters==120,"Model physical scale lost");rejects([&]{scene.removeModel(model->id);});rejects([&]{scene.installModel(model);});need(scene.snapshot(100,{}).models.size()==1,"Model metadata missing from snapshot");scene.remove(o.id);scene.removeModel(model->id);need(scene.models().empty(),"Model resource not removed");
    if(argc>1){std::mt19937 random(0x6d6f6465);for(unsigned i=0;i<5000;++i){auto mutation=bytes;for(unsigned j=0;j<1+i%8;++j)mutation[random()%mutation.size()]=static_cast<unsigned char>(random());try{decodeModel("mutation","",mutation);}catch(const std::exception&){}}}
    std::cout<<"Models OK: bounded embedded GLB, hierarchy, winding, PBR, physical scale, invalid data and asset lifetime\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
