#include "providers/terrain.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
    std::cout<<"terrain_test: ok\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
