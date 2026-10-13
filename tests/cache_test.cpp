#include "cache.hpp"
#include "sha256.hpp"
#include "geography.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <source_location>

void require(bool value, std::source_location where=std::source_location::current()) { if(!value) throw std::runtime_error("Cache/integrity regression at line "+std::to_string(where.line())); }
int main(int argc,char**argv) {
    require(argc==2);
    require(albert::sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    require(albert::sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    require(albert::sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    namespace fs=std::filesystem;
    const fs::path root=argv[1];fs::remove_all(root);fs::create_directories(root/"assets");
    fs::create_directories(root/"clouds");
    fs::create_directories(root/"ephemerides");fs::create_directories(root/"catalog");
    std::ofstream(root/"ephemerides/current.json")<<"{}";std::ofstream(root/"catalog/current.json")<<"{}";
    auto sparse=[&](const char* name,uintmax_t mb) {
        std::ofstream f(root/name,std::ios::binary);f.seekp(static_cast<std::streamoff>(mb*1024*1024-1));f.put('x');
    };
    sparse("assets/old.bin",80);sparse("assets/recent.bin",60);sparse("clouds/inflight.tmp",10);
    sparse("unrelated.bin",150);
    fs::last_write_time(root/"assets/old.bin",fs::file_time_type::clock::now()-std::chrono::hours(1));
    albert::Cache cache(root);cache.setLimit(128);
    require(!fs::exists(root/"assets/old.bin"));require(fs::exists(root/"assets/recent.bin"));
    require(fs::exists(root/"clouds/inflight.tmp"));require(fs::exists(root/"unrelated.bin"));
    require(cache.bytes==70*1024*1024+4);
    albert::Cache restored(root);require(restored.limitMB==128);
    restored.clear();require(fs::exists(root/"unrelated.bin"));require(restored.bytes==0);
    require(!fs::exists(root/"ephemerides")&&!fs::exists(root/"catalog"));
    for(double lon:{-180.,-179.9,0.,179.9,180.}) for(double lat:{-90.,0.,90.}) {
        const auto region=albert::cloudRegion(lon,lat,1.02);
        require(region.south>=-90 && region.north<=90 && region.east-region.west<=360);
    }
    const auto lunar=albert::viewRegion(0,0,.3,1737.4/6378.137,32./9.,60.,1.40625);
    require(lunar.west < -4 && lunar.east > 4 && lunar.south < -3 && lunar.north > 3);
    require(albert::viewRegion(180,0,.3,1737.4/6378.137)==albert::viewRegion(-180,0,.3,1737.4/6378.137));
    // The previous cell selector failed here: the viewport crossed the cell
    // edge even though its center remained in the cell. Include both sides.
    const auto view=albert::cloudRegion(0,0,1.05,32./9.);
    require(view.west < -4 && view.east > 4 && view.south < -3 && view.north > 3);
    const auto seam=albert::cloudRegion(179.9,0,1.05);
    require(seam.west<179.9 && seam.east>180.);
    const auto pole=albert::cloudRegion(0,89,1.05);
    require(pole.east-pole.west==360 && pole.north==90.);
    fs::remove_all(root);std::cout<<"Cache LRU, limits, isolation, SHA-256 and geographic boundaries OK\n";
}
