#include "asset_downloads.hpp"
#include "platform.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <map>
#include <stdexcept>
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Condition> bool waitFor(Condition condition,std::chrono::milliseconds timeout){
    const auto end=std::chrono::steady_clock::now()+timeout;
    while(!condition()){if(std::chrono::steady_clock::now()>end)return false;std::this_thread::sleep_for(5ms);}
    return true;
}
int main(int argc,char** argv){try{
    namespace fs=std::filesystem;require(argc==2,"Original library path missing");
    require(albert::sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"&&albert::sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 reference vectors");
    std::string block(1000,'q');albert::Sha256 split;for(size_t i=0;i<block.size();i+=37)split.update(std::string_view(block).substr(i,37));
    require(split.hex()==albert::sha256(block),"Incremental SHA-256 differs from one-shot");
    bool missing=false;try{albert::visualAssetPack();}catch(const std::exception&){missing=true;}
    require(missing,"Library opened before the essential container arrived");
    const auto catalog=albert::readAssetCatalog(albert::assetDirectory()/"catalog.json");
    albert::AssetDownloads::Options options;options.backoff.assign(1,50ms);options.reserveBytes=0;options.stallSeconds=5;
    std::map<std::string,int> finished;bool preempted=false;
    {
        albert::AssetDownloads downloads(catalog,options);downloads.start();
        require(waitFor([&]{return downloads.status().essentialReady;},20s),"Essential container did not arrive");
        require(albert::visualAssetPack()->availableLevel("earth-day")==0&&!albert::visualAssetsComplete(),"Essential library has the wrong level");
        require(waitFor([&]{const auto status=downloads.status();
            for(const auto& item:status.items){
                if(item.done)finished[item.file]=item.finished;
                // Switch focus during the slow resumed transfer of moon-1.
                if(item.active&&item.file=="moon-1.pak"&&item.received>item.bytes/2+64&&!preempted){downloads.prioritize("mercury");preempted=true;}
            }
            return status.complete;},60s),"Downloads did not complete");
    }
    require(preempted&&finished.at("essential.pak")==0&&finished.at("mercury-1.pak")<finished.at("moon-1.pak"),"Focused body did not overtake the running transfer");
    require(albert::visualAssetsComplete(),"Library incomplete after all downloads");
    albert::AssetPack original{fs::path(argv[1])};albert::DownloadProgress progress;const auto library=albert::visualAssetPack();
    for(const std::string layer:{"earth-day","moon-height","jupiter-day","mercury-height"}){
        const auto a=library->image(layer,{},32,progress),b=original.image(layer,{},32,progress);
        require(a.width()==b.width()&&(a.isFloat()?a.data<float>()==b.data<float>():a.data()==b.data()),"Downloaded library differs from the original");
    }
    // Fresh asset folder for the remaining checks: on Windows an installed
    // container stays open, so deleting it would only mark it pending.
    const auto second=albert::assetDirectory().parent_path()/"second";
#ifdef _WIN32
    _wputenv_s(L"ALBERT_ASSET_DIR",second.c_str());
#else
    setenv("ALBERT_ASSET_DIR",second.c_str(),1);
#endif
    const auto packs=albert::assetContainerDirectories().front();require(packs==second/"packs","Asset folder switch failed");
    // Disk reserve: refuse to start a container that would leave too little space.
    {auto low=options;low.reserveBytes=std::uint64_t(1)<<62;albert::AssetDownloads downloads(catalog,low);downloads.start();
     require(waitFor([&]{return downloads.status().error.find("Espaco")!=std::string::npos;},10s),"Disk reserve was not enforced");}
    require(!fs::exists(packs/"essential.pak.part"),"Download started despite the disk reserve");
    // Shutdown cancels a running transfer promptly and keeps it resumable.
    std::chrono::steady_clock::time_point stopped;
    {albert::AssetDownloads downloads(catalog,options);downloads.start();
     require(waitFor([&]{const auto s=downloads.status();return std::any_of(s.items.begin(),s.items.end(),[](const auto& i){return i.file=="earth-1.pak"&&i.active&&i.received>0;});},20s),"Cancellation target never started");
     stopped=std::chrono::steady_clock::now();}
    require(std::chrono::steady_clock::now()-stopped<3s,"Cancellation did not stop the transfer promptly");
    require(fs::exists(packs/"earth-1.pak.part")&&!fs::exists(packs/"earth-1.pak"),"Cancelled transfer was not kept resumable");
    std::cout<<"Asset downloads: essential first, focus preemption, verified install, disk reserve and cancellation OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
