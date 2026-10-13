#pragma once
#include <nlohmann/json.hpp>
#include <bit>
#include <vector>
namespace model_fixture {
using J=nlohmann::json;
inline J document(){return J::parse(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,0],"max":[1,2,0]}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.1,0.8,0.2,1],"metallicFactor":0,"roughnessFactor":0.6},"doubleSided":true}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],"nodes":[{"mesh":0,"translation":[0,2,0]}],"scenes":[{"nodes":[0]}],"scene":0})");}
inline void append(std::vector<unsigned char>& bytes,unsigned v){for(int i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(v>>(i*8)));}
inline std::vector<unsigned char> binary(){std::vector<unsigned char> b;for(float f:{-1.f,0.f,0.f,1.f,0.f,0.f,0.f,2.f,0.f})append(b,std::bit_cast<unsigned>(f));return b;}
inline std::vector<unsigned char> glb(J json=document(),std::vector<unsigned char> data=binary()){auto text=json.dump();while(text.size()%4)text+=' ';while(data.size()%4)data.push_back(0);std::vector<unsigned char> b;append(b,0x46546c67);append(b,2);append(b,28+text.size()+data.size());append(b,text.size());append(b,0x4e4f534a);b.insert(b.end(),text.begin(),text.end());append(b,data.size());append(b,0x004e4942);b.insert(b.end(),data.begin(),data.end());return b;}
}
