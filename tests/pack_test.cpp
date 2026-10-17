#include "asset_pack.hpp"
#include "platform.hpp"
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    try {
        require(argc>=2 && argc<=4,"Pack fixture argument missing");
        if(argc==4&&std::string(argv[1])=="--split"){
            namespace fs=std::filesystem;const fs::path directory(argv[2]);albert::AssetPack original{fs::path(argv[3])};albert::DownloadProgress progress;
            const auto catalog=albert::readAssetCatalog(directory/"catalog.json");const auto essential=directory/catalog.containers.front().file;
            auto same=[](const threepp::Image& a,const threepp::Image& b){return a.width()==b.width()&&a.height()==b.height()&&a.isFloat()==b.isFloat()&&(a.isFloat()?a.data<float>()==b.data<float>():a.data()==b.data());};
            albert::AssetPack first(std::vector<fs::path>{essential});
            require(first.availableLevel("earth-day")==0&&first.maximumLevel("earth-day")==1&&first.contains("sun"),"Essential container must stop at the startup level");
            require(same(first.image("earth-day",{},8,progress),original.image("earth-day",{},4,progress)),"Missing detail must fall back to the coarse level, never invent pixels");
            require(albert::AssetPack(std::vector<fs::path>{essential,directory/"moons-2.pak"}).availableLevel("jupiter-day")==0,"A level gap exposed finer tiles");
            std::vector<fs::path> paths;for(const auto& container:catalog.containers)paths.push_back(directory/container.file);
            albert::AssetPack all(paths);
            for(const std::string layer:{"earth-day","earth-night","earth-height","moon-day","moon-height","jupiter-day","sun","mercury-day","mercury-height"}){
                require(all.availableLevel(layer)==original.availableLevel(layer)&&all.availableLevel(layer)==all.maximumLevel(layer),"Containers lost a resolution level");
                const albert::GeoRegion region=layer=="sun"?albert::GeoRegion{0,0,1,1}:albert::GeoRegion{};
                for(unsigned size:{2u,8u,32u})require(same(all.image(layer,region,size,progress),original.image(layer,region,size,progress)),"Container images differ from the complete library");
            }
            auto rejects=[](std::vector<fs::path> files){try{albert::AssetPack value(files);}catch(const std::exception&){return true;}return false;};
            require(rejects({essential,essential}),"Duplicate container accepted");
            require(rejects({essential,directory/"divergent.pak"}),"Divergent layer description accepted");
            // Library holder: installs publish new snapshots; a rejected container changes nothing.
            require(!albert::visualAssetsComplete()&&albert::visualAssetPack()->availableLevel("earth-day")==0,"Holder ignored the catalogued containers");
            const auto generation=albert::visualAssetGeneration();const auto held=albert::visualAssetPack();
            albert::installVisualAssetContainer(directory/"earth-1.pak");
            require(albert::visualAssetGeneration()==generation+1&&albert::visualAssetPack()->availableLevel("earth-day")==1&&held->availableLevel("earth-day")==0,"Install did not publish a new immutable snapshot");
            bool rejected=false;try{albert::installVisualAssetContainer(directory/"divergent.pak");}catch(const std::exception&){rejected=true;}
            require(rejected&&albert::visualAssetGeneration()==generation+1&&albert::visualAssetPack()->availableLevel("earth-day")==1,"Rejected container replaced the library");
            for(const auto& file:paths)albert::installVisualAssetContainer(file);
            require(albert::visualAssetsComplete()&&albert::visualAssetPack()->availableLevel("jupiter-day")==2,"Holder did not complete the catalogue");
            std::cout<<"Pack: download containers, level caps, exact images, rejection and atomic library install OK\n";return 0;
        }
        const std::filesystem::path path(argv[1]);
        if(argc==3&&std::string(argv[2])=="--invalid"){
            bool rejected=false;try{albert::AssetPack invalid(path);}catch(const std::exception&){rejected=true;}
            require(rejected,"Corrupted collection trailer accepted");return 0;
        }
        albert::AssetPack pack(path);albert::DownloadProgress progress;
        if(argc==4&&std::string(argv[2])=="--replace"){
            const auto before=pack.image("earth-day",{},4,progress);const std::filesystem::path replacement(argv[3]);
#ifdef _WIN32
            if(!ReplaceFileW(path.c_str(),replacement.c_str(),nullptr,0,nullptr,nullptr))throw std::runtime_error("Atomic replacement failed: "+std::to_string(GetLastError()));
#else
            std::filesystem::rename(replacement,path);
#endif
            albert::AssetPack fresh(path);
            require(pack.image("earth-day",{},4,progress).data()==before.data(),"Running reader combined an old index with a new archive");
            require(fresh.image("earth-day",{},4,progress).data()[0]==before.data()[0]+20,"New reader missed the replacement archive");
            std::cout<<"Pack: atomic replacement preserves existing readers and updates new readers\n";return 0;
        }
        progress.stage("Superficie da Terra",5);
        const auto globe=pack.image("earth-day",{},4,progress);
        const auto loaded=progress.snapshot();
        require(loaded.completed==5,"Tile reads overwrote the caller's global progress stage");
        require(loaded.total>1&&loaded.received==loaded.total,"Individual image progress did not finish");
        require(globe.width()==4 && globe.height()==2,"Coarse LOD dimensions");
        require(globe.data()[0]==40 && globe.data()[2]==21,"South row orientation");
        require(globe.data()[4*4+2]==20,"North row orientation");
        const auto nonPowerOfTwo=pack.image("jupiter-day",{},8,progress);
        require(nonPowerOfTwo.width()==8&&nonPowerOfTwo.height()==4&&nonPowerOfTwo.data()[0]>=120,"Non-power-of-two mosaic underfilled the requested image budget");
        const auto native=pack.image("jupiter-day",{},32,progress);
        require(native.width()==12&&native.height()==6,"Source imagery was artificially upscaled");
        std::atomic_bool stable=true;
        auto read=[&](const char* layer,unsigned size){try{albert::DownloadProgress p;const auto reference=pack.image(layer,{},size,p);for(int i=0;i<100;++i){const auto image=pack.image(layer,{},size,p);if(image.isFloat()?image.data<float>()!=reference.data<float>():image.data()!=reference.data())stable=false;}}catch(const std::exception&){stable=false;}};
        {std::jthread one(read,"earth-day",4),two(read,"moon-height",4);}
        require(stable,"Concurrent archive reads mixed file positions");
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
