#include "asset_pack.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    try {
        require(argc==2 || argc==3,"Pack fixture argument missing");
        const std::filesystem::path path(argv[1]);
        albert::AssetPack pack(path);albert::DownloadProgress progress;
        const auto globe=pack.image("earth-day",{},4,progress);
        require(globe.width()==4 && globe.height()==2,"Coarse LOD dimensions");
        require(globe.data()[0]==40 && globe.data()[2]==21,"South row orientation");
        require(globe.data()[4*4+2]==20,"North row orientation");
        const auto wrap=pack.image("earth-day",{90,-90,270,90},4,progress);
        require(wrap.width()==4 && wrap.height()==4,"Detailed LOD dimensions");
        require(wrap.data()[0]==86 && wrap.data()[8]==80,"Antimeridian coverage");
        const auto height=pack.image("moon-height",{},4,progress);
        require(height.isFloat(),"Elevation must keep precision");
        require(std::abs(height.data<float>()[0]-198.5f)<.01f,"16-bit south elevation");
        require(std::abs(height.data<float>()[4*4]-(-53.f))<.01f,"16-bit north elevation");
        if(argc==3){
            const auto mercury=pack.image("mercury-day",{},4,progress);
            require(mercury.data()[0]==40,"Appended body image missing");
            const auto terrain=pack.image("mercury-height",{},4,progress);
            require(terrain.isFloat()&&std::abs(terrain.data<float>()[0]-99.25f)<.01f,"Body-specific DEM encoding lost");
        }
        progress.cancelled=true;
        bool cancelled=false;
        try {pack.image("earth-day",{},4,progress);} catch(const std::exception&) {cancelled=true;}
        require(cancelled,"Read cancellation");progress.cancelled=false;
        // Change a payload after its index was parsed: SHA-256 must catch it.
        std::fstream stream(path,std::ios::in|std::ios::out|std::ios::binary);
        if(argc==3){
            stream.seekg(-16,std::ios::end);unsigned char footer[8]{};stream.read(reinterpret_cast<char*>(footer),8);
            std::uint64_t length=0;for(int i=0;i<8;++i)length|=std::uint64_t(footer[i])<<(i*8);
            stream.seekp(-static_cast<std::streamoff>(17+length),std::ios::end);
        }else stream.seekp(-1,std::ios::end);
        stream.put('\0');stream.close();
        bool corrupt=false;
        try {if(argc==3)pack.image("mercury-height",{},4,progress);else pack.image("sun",{0,0,1,1},4,progress);} catch(const std::exception&) {corrupt=true;}
        require(corrupt,"Corrupt payload accepted");
        std::ofstream invalid(path,std::ios::binary|std::ios::trunc);invalid<<"ALBRTPK1";invalid.close();
        bool truncated=false;
        try {albert::AssetPack broken(path);} catch(const std::exception&) {truncated=true;}
        require(truncated,"Truncated pack accepted");
        std::cout<<"Pack: LOD, north/south, antimeridian, 16-bit DEM, cancellation and integrity OK\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
