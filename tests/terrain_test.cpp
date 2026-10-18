#include "providers/terrain.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <iostream>
#include <stdexcept>
#include <vector>
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data,int length,int* out,int quality);
namespace {
void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
template<class T>void put(std::string& b,T v){b.append(reinterpret_cast<const char*>(&v),sizeof v);}
template<class T>void at(std::string& b,std::size_t o,T v){std::memcpy(b.data()+o,&v,sizeof v);}
// A Copernicus-like COG: 8×8 posts over 1°, 4×4 tiles, DEFLATE + floating-point predictor,
// height = 100 + 3 x + 5 y (posts), pixel-is-point tied at (west, north).
std::string cog(int lat,int lon){
    constexpr unsigned n=8,t=4;std::vector<std::string> tiles;
    for(unsigned ty=0;ty<2;++ty)for(unsigned tx=0;tx<2;++tx){
        std::string raw;
        for(unsigned y=0;y<t;++y){std::string row(4*t,'\0');
            for(unsigned x=0;x<t;++x){const float v=100+3.f*(tx*t+x)+5.f*(ty*t+y);std::uint32_t u;std::memcpy(&u,&v,4);for(int k=0;k<4;++k)row[k*t+x]=static_cast<char>(u>>(24-8*k));}
            for(std::size_t i=row.size()-1;i>0;--i)row[i]=static_cast<char>(row[i]-row[i-1]);raw+=row;}
        int length=0;unsigned char* z=stbi_zlib_compress(reinterpret_cast<unsigned char*>(raw.data()),static_cast<int>(raw.size()),&length,8);
        tiles.emplace_back(reinterpret_cast<char*>(z),static_cast<std::size_t>(length));std::free(z);
    }
    std::string b="II";put<std::uint16_t>(b,42);put<std::uint32_t>(b,8);
    struct E {std::uint16_t tag,type;std::uint32_t count,value;};
    std::vector<E> e{{256,3,1,n},{257,3,1,n},{258,3,1,32},{259,3,1,8},{277,3,1,1},{317,3,1,3},{322,3,1,t},{323,3,1,t},{324,4,4,0},{325,4,4,0},{339,3,1,3},{33550,12,3,0},{33922,12,6,0}};
    const std::size_t ifd=8,data=ifd+2+12*e.size()+4;std::size_t extra=data;
    std::string tail;auto offset=[&](std::size_t bytes){const std::size_t o=extra;extra+=bytes;return static_cast<std::uint32_t>(o);};
    e[8].value=offset(16);e[9].value=offset(16);e[11].value=offset(24);e[12].value=offset(48);
    put<std::uint16_t>(b,static_cast<std::uint16_t>(e.size()));for(const auto& x:e){put(b,x.tag);put(b,x.type);put(b,x.count);put(b,x.value);}put<std::uint32_t>(b,0);
    b.resize(extra,'\0');std::size_t pos=b.size();
    for(int i=0;i<4;++i){at<std::uint32_t>(b,e[8].value+4*i,static_cast<std::uint32_t>(pos));at<std::uint32_t>(b,e[9].value+4*i,static_cast<std::uint32_t>(tiles[static_cast<std::size_t>(i)].size()));pos+=tiles[static_cast<std::size_t>(i)].size();}
    at<double>(b,e[11].value,1./n);at<double>(b,e[11].value+8,1./n);at<double>(b,e[12].value+24,static_cast<double>(lon));at<double>(b,e[12].value+32,static_cast<double>(lat+1));
    for(const auto& x:tiles)b+=x;
    return b;
}
}
int main(int argc,char** argv){try{
    const std::filesystem::path cache=argc>1?argv[1]:"terrain cache";std::filesystem::remove_all(cache);
    const std::string file=cog(10,20);int requests=0;
    albert::providers::CopernicusDem dem(cache,[&](const std::string& url,const std::string& range,albert::DownloadProgress*)->std::string{
        ++requests;if(url.find("N10_00_E020")==std::string::npos)throw std::runtime_error("HTTP: The requested URL returned error: 404");
        const auto dash=range.find('-');const std::size_t a=std::stoul(range.substr(0,dash)),z=std::stoul(range.substr(dash+1));return file.substr(a,std::min<std::size_t>(z+1,file.size())-a);});
    albert::DownloadProgress p;
    auto expected=[](double lat,double lon){return 100+3*(lon-20)*8+5*(11-lat)*8;}; // Linear in posts: bilinear is exact.
    for(const auto [lat,lon]:{std::pair{10.95,20.05},std::pair{10.5,20.5},std::pair{10.2,20.8},std::pair{10.6,20.37}}){
        const double h=dem.height(lat,lon,p);std::printf("%.3f %.3f: %.4f (expected %.4f)\n",lat,lon,h,expected(lat,lon));
        require(std::abs(h-expected(lat,lon))<1e-3,"Bilinear DEM height");
    }
    require(dem.height(12.5,20.5,p)==0,"Missing tile reads as ocean");
    const auto grid=dem.heights(20.25,10.25,20.75,10.75,3,3,p);require(std::abs(grid[4]-expected(10.5,20.5))<1e-3&&std::abs(grid[0]-expected(10.25,20.25))<1e-3,"Height grid");
    const int before=requests;albert::providers::CopernicusDem again(cache,[&](const std::string&,const std::string&,albert::DownloadProgress*)->std::string{throw std::runtime_error("offline");});
    require(std::abs(again.height(10.5,20.5,p)-expected(10.5,20.5))<1e-3&&requests==before,"Cached header and tiles reused offline");
    { // Review: a truncated cached header (a crash mid-write, or a reader racing the writer) is dropped and
      // fetched again; cachedHeight never throws it at the terrain's worker.
        std::filesystem::path header;for(const auto& e:std::filesystem::directory_iterator(cache))if(e.path().extension()==".header"&&e.path().string().find("N10_00_E020")!=std::string::npos)header=e.path();
        require(!header.empty(),"Cached header");{std::string head;{std::ifstream in(header,std::ios::binary);head.assign(std::istreambuf_iterator<char>(in),{});}std::ofstream(header,std::ios::binary)<<head.substr(0,100);}
        albert::providers::CopernicusDem fresh(cache,[&](const std::string& url,const std::string& range,albert::DownloadProgress*)->std::string{
            const auto dash=range.find('-');const std::size_t a=std::stoul(range.substr(0,dash)),z=std::stoul(range.substr(dash+1));(void)url;return file.substr(a,std::min<std::size_t>(z+1,file.size())-a);});
        bool threw=false;std::optional<double> first;try{first=fresh.cachedHeight(10.5,20.5);}catch(const std::exception&){threw=true;}
        require(!threw&&!first,"A truncated header reads as missing, not as an error");
        while(fresh.fetchMissing(p)){}const auto healed=fresh.cachedHeight(10.5,20.5);
        require(healed&&std::abs(*healed-expected(10.5,20.5))<1e-3,"The header is fetched again and heights return");
        for(const auto& e:std::filesystem::directory_iterator(cache))require(e.path().string().find(".part")==std::string::npos,"No partial cache file left");
    }
    { // Review: a portal's page answering a header request is not cached as one, and the fetch fails
      // (so it backs off) instead of reporting a piece that never landed (an unthrottled refetch loop).
        albert::providers::CopernicusDem portal(cache/"portal",[](const std::string&,const std::string&,albert::DownloadProgress*)->std::string{return "<html>Sign in to the network</html>";});
        require(!portal.cachedHeight(11.5,20.5),"Uncached header");bool threw=false;
        try{portal.fetchMissing(p);}catch(const std::runtime_error&){threw=true;}
        require(threw&&portal.generation()==0,"A non-GeoTIFF header fails its fetch, uncached");
    }
    { // Review: a header with absurd tile sizes (65535) is refused before any tile decode sizes itself on
      // them (16 GiB), and the decoder refuses such sizes before allocating.
        std::string bad=file;std::uint32_t ifd=0;std::memcpy(&ifd,bad.data()+4,4);std::uint16_t n=0;std::memcpy(&n,bad.data()+ifd,2);
        for(std::uint16_t i=0;i<n;++i){const std::size_t e=ifd+2+12u*i;std::uint16_t tag=0;std::memcpy(&tag,bad.data()+e,2);if(tag==322||tag==323){const std::uint16_t huge=65535;std::memcpy(bad.data()+e+8,&huge,2);}}
        albert::providers::CopernicusDem absurd(cache/"absurd",[&](const std::string&,const std::string& range,albert::DownloadProgress*)->std::string{
            const auto dash=range.find('-');const std::size_t a=std::stoul(range.substr(0,dash)),z=std::stoul(range.substr(dash+1));return bad.substr(a,std::min<std::size_t>(z+1,bad.size())-a);});
        require(!absurd.cachedHeight(10.5,20.5),"Uncached header");bool threw=false;try{absurd.fetchMissing(p);}catch(const std::runtime_error&){threw=true;}
        require(threw&&absurd.generation()==0,"A header with absurd tile sizes is refused, uncached");
        bool refused=false;try{(void)albert::providers::decodeFloatTile("xx",65536,65536);}catch(const std::exception&){refused=true;}
        require(refused,"The tile decoder refuses absurd sizes before allocating");
    }
    { // Review: a block of the right size but corrupt (two instances writing one cache) is fetched again.
        int corrupted=0;for(const auto& e:std::filesystem::directory_iterator(cache))if(e.path().string().find("N10_00_E020")!=std::string::npos&&e.path().string().find(".tif.")!=std::string::npos){
            const auto size=std::filesystem::file_size(e.path());std::ofstream(e.path(),std::ios::binary)<<std::string(size,'\x5a');++corrupted;}
        albert::providers::CopernicusDem fresh(cache,[&](const std::string&,const std::string& range,albert::DownloadProgress*)->std::string{
            const auto dash=range.find('-');const std::size_t a=std::stoul(range.substr(0,dash)),z=std::stoul(range.substr(dash+1));return file.substr(a,std::min<std::size_t>(z+1,file.size())-a);});
        bool threw=false;std::optional<double> first;try{first=fresh.cachedHeight(10.5,20.5);}catch(const std::exception&){threw=true;}
        require(corrupted>0&&!threw&&!first,"A corrupt block reads as missing");
        while(fresh.fetchMissing(p)){}const auto healed=fresh.cachedHeight(10.5,20.5);
        require(healed&&std::abs(*healed-expected(10.5,20.5))<1e-3,"The corrupt block is fetched again");
    }
    { // LOLA and MOLA by byte range: pixel-is-area centres, endianness, scaling, tile seams and the 360° wrap.
        auto dn=[](int row,int column){return static_cast<std::int16_t>((row*7+column*3)%20000-10000);};
        int pdsRequests=0;
        auto bytesOf=[&](const std::string& url,std::size_t first,std::size_t last){
            const bool moon=url.find("ldem_128")!=std::string::npos;std::string out;
            for(std::size_t at=first;at<=last;at+=2){
                int row,column;
                if(moon){row=static_cast<int>(at/2/46080);column=static_cast<int>(at/2%46080);}
                else{const std::string tile=url.substr(url.find("megr")+4,6);const int band=tile=="88n000"||tile.rfind("88n",0)==0?0:tile.rfind("44n",0)==0?1:tile.rfind("00n",0)==0?2:3;
                    row=band*5632+static_cast<int>(at/2/11520);column=std::stoi(tile.substr(3))*128+static_cast<int>(at/2%11520);}
                const auto v=static_cast<std::uint16_t>(dn(row,column));
                if(moon){out+=static_cast<char>(v&255);out+=static_cast<char>(v>>8);}else{out+=static_cast<char>(v>>8);out+=static_cast<char>(v&255);}
            }
            return out;};
        // One range: the bytes; several (terrain blocks): a multipart/byteranges answer, parts shuffled.
        auto transport=[&](const std::string& url,const std::string& range,albert::DownloadProgress*){
            ++pdsRequests;std::vector<std::pair<std::size_t,std::size_t>> spans;std::stringstream list(range);std::string item;
            while(std::getline(list,item,',')){const auto dash=item.find('-');spans.emplace_back(std::stoull(item.substr(0,dash)),std::stoull(item.substr(dash+1)));}
            if(spans.size()==1)return bytesOf(url,spans[0].first,spans[0].second);
            std::reverse(spans.begin(),spans.end());std::string out;
            for(const auto& [a,z]:spans)out+="--sep\r\nContent-Type: application/octet-stream\r\nContent-Range: bytes "+std::to_string(a)+"-"+std::to_string(z)+"/9999999999\r\n\r\n"+bytesOf(url,a,z)+"\r\n";
            return out+"--sep--\r\n";};
        const auto blocks=cache/"relief";albert::providers::PlanetaryRelief relief(transport,blocks);
        albert::DownloadProgress progress;
        auto lunar=[&](int row,int column){return relief.height(albert::physics::Center::moon,90-(row+.5)/128,(column+.5)/128,progress);};
        require(std::abs(lunar(100,200)-dn(100,200)*.5)<1e-9&&std::abs(lunar(23039,46079)-dn(23039,46079)*.5)<1e-9,"Lunar pixel centres");
        const double mid=relief.height(albert::physics::Center::moon,90-100.75/128,200.75/128,progress); // A quarter pixel in from (100, 200).
        require(std::abs(mid-.5*(.75*(.75*dn(100,200)+.25*dn(100,201))+.25*(.75*dn(101,200)+.25*dn(101,201))))<1e-9,"Lunar bilinear");
        const double wrap=relief.height(albert::physics::Center::moon,90-100.5/128,360-.25/128,progress); // Between the last and the first column.
        require(std::abs(wrap-.5*(.75*dn(100,46079)+.25*dn(100,0)))<1e-9,"Lunar 360° wrap");
        constexpr double f=(3396190.-3376200.)/3396190.,deg=3.14159265358979323846/180;
        auto martian=[&](int row,int column){ // Geodetic latitude whose ellipsoid point lies at the pixel's planetocentric latitude.
            const double c=(88-(row+.5)/128)*deg,g=std::atan(std::tan(c)/((1-f)*(1-f)));const double lon=(column+.5)/128;
            const auto p=albert::physics::fixedPosition(albert::physics::planet(albert::physics::Center::mars),{g,lon*deg,0});
            return relief.height(albert::physics::Center::mars,g/deg,lon,progress)-(dn(row,column)+3396000.-albert::physics::norm(p));};
        for(auto [r,c]:{std::pair{0,0},{5631,11519},{5632,11520},{11300,23100},{22527,46079},{17000,34600}})require(std::abs(martian(r,c))<1e-6,"Martian radii across MOLA tiles");
        bool threw=false;try{relief.height(albert::physics::Center::mars,89,0,progress);}catch(const std::invalid_argument&){threw=true;}require(threw,"MOLA polar gap");
        require(pdsRequests<=30,"Relief requests: two pixels per row");
        // Terrain blocks: nothing until fetched (the block is noted), then the same heights as by pixel, offline afterwards.
        const auto apollo=std::pair{.6741,23.4730},seam=std::pair{44.0-.25/128,90.0+.3/128}; // A lunar site; MOLA tile corner (rows 5631/5632, columns 11519/11520).
        require(!relief.cachedHeight(albert::physics::Center::moon,apollo.first,apollo.second),"No block before it is fetched");
        int fetched=0;while(relief.fetchMissing(progress))++fetched;
        const auto moon=relief.cachedHeight(albert::physics::Center::moon,apollo.first,apollo.second);
        require(fetched>=1&&moon&&std::abs(*moon-relief.height(albert::physics::Center::moon,apollo.first,apollo.second,progress))<1e-9,"Lunar block heights equal pixel heights");
        for(int pass=0;pass<2;++pass){if(!relief.cachedHeight(albert::physics::Center::mars,seam.first,seam.second))while(relief.fetchMissing(progress)){}}
        const auto mars=relief.cachedHeight(albert::physics::Center::mars,seam.first,seam.second);
        require(mars&&std::abs(*mars-relief.height(albert::physics::Center::mars,seam.first,seam.second,progress))<1e-6,"Martian block heights across tile corners");
        const int before=pdsRequests;albert::providers::PlanetaryRelief offline([](const std::string&,const std::string&,albert::DownloadProgress*)->std::string{throw std::runtime_error("offline");},blocks);
        const auto again=offline.cachedHeight(albert::physics::Center::moon,apollo.first,apollo.second);
        require(again&&std::abs(*again-*moon)<1e-12&&pdsRequests==before,"Blocks reused from disk");
        { // Review: a failed download is retried by the next fetchMissing, with no new lookup (a camera at rest).
            int failures=1;albert::providers::PlanetaryRelief flaky([&](const std::string& url,const std::string& range,albert::DownloadProgress* p)->std::string{
                if(failures-->0)throw std::runtime_error("network down");return transport(url,range,p);},cache/"relief-retry");
            const auto site=std::pair{-12.5,140.25};require(!flaky.cachedHeight(albert::physics::Center::moon,site.first,site.second),"No block before the retry test");
            bool threw=false;try{flaky.fetchMissing(progress);}catch(const std::exception&){threw=true;}
            require(threw&&flaky.fetchMissing(progress)&&flaky.cachedHeight(albert::physics::Center::moon,site.first,site.second),"A failed block is fetched on the next pass");
        }
        std::printf("relief blocks: %d fetched, generation %llu\n",fetched,static_cast<unsigned long long>(relief.generation()));
    }
    std::cout<<"terrain_test: ok\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
