#include "terrain.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
extern "C" int stbi_zlib_decode_buffer(char* out,int outLength,const char* in,int inLength);
namespace albert::providers {
namespace {
template<class T>T read(const std::string& b,std::size_t at){if(at+sizeof(T)>b.size())throw std::runtime_error("Truncated GeoTIFF header");T v;std::memcpy(&v,b.data()+at,sizeof v);return v;}
std::string tileName(int lat,int lon){
    char s[96];std::snprintf(s,sizeof s,"Copernicus_DSM_COG_10_%c%02d_00_%c%03d_00_DEM",lat<0?'S':'N',std::abs(lat),lon<0?'W':'E',std::abs(lon));return s;
}
}
std::vector<float> decodeFloatTile(const std::string& compressed,unsigned w,unsigned h){
    const std::size_t bytes=static_cast<std::size_t>(w)*h*4;std::string raw(bytes,'\0');
    if(stbi_zlib_decode_buffer(raw.data(),static_cast<int>(bytes),compressed.data(),static_cast<int>(compressed.size()))!=static_cast<int>(bytes))throw std::runtime_error("Corrupt DEM tile");
    std::vector<float> out(static_cast<std::size_t>(w)*h);const std::size_t row=static_cast<std::size_t>(w)*4;
    for(unsigned y=0;y<h;++y){
        auto* b=reinterpret_cast<unsigned char*>(raw.data())+y*row;
        for(std::size_t i=1;i<row;++i)b[i]=static_cast<unsigned char>(b[i]+b[i-1]); // Horizontal byte differencing.
        for(unsigned x=0;x<w;++x){const std::uint32_t v=std::uint32_t(b[x])<<24|std::uint32_t(b[w+x])<<16|std::uint32_t(b[2*w+x])<<8|b[3*w+x];std::memcpy(&out[static_cast<std::size_t>(y)*w+x],&v,4);} // Byte planes, most significant first.
    }
    return out;
}
struct CopernicusDem::File {std::string url;unsigned width=0,height=0,tileWidth=0,tileHeight=0;double west=0,north=0,stepX=0,stepY=0;std::vector<std::uint64_t> offsets,counts;bool ocean=false;};
CopernicusDem::CopernicusDem(std::filesystem::path cache,Transport transport):cache_(std::move(cache)),transport_(std::move(transport)){
    if(!transport_)transport_=[](const std::string& url,const std::string& range,DownloadProgress* p){return httpGet(url,p,{16*1024*1024,60,range});};
}
const CopernicusDem::File* CopernicusDem::file(int lat,int lon,DownloadProgress& progress){
    const auto key=std::make_pair(lat,lon);if(const auto i=files_.find(key);i!=files_.end())return i->second.get();
    auto f=std::make_shared<File>();const std::string name=tileName(lat,lon);
    f->url="https://copernicus-dem-30m.s3.amazonaws.com/"+name+"/"+name+".tif";
    const auto path=cache_/(name+".header");std::string head;
    if(std::ifstream in{path,std::ios::binary})head.assign(std::istreambuf_iterator<char>(in),{});
    if(head.empty()){
        try{head=transport_(f->url,"0-65535",&progress);}
        catch(const std::exception& e){if(std::string(e.what()).find("404")==std::string::npos&&std::string(e.what()).find("403")==std::string::npos)throw;head="ocean";} // Absent: no land.
        std::error_code ec;std::filesystem::create_directories(cache_,ec);std::ofstream(path,std::ios::binary)<<head;
    }
    if(head=="ocean"){f->ocean=true;return (files_[key]=f).get();}
    if(head.size()<8||head.compare(0,2,"II")!=0||read<std::uint16_t>(head,2)!=42)throw std::runtime_error("Unsupported DEM GeoTIFF");
    const auto ifd=read<std::uint32_t>(head,4);const auto n=read<std::uint16_t>(head,ifd);
    double scaleX=0,scaleY=0,tieX=0,tieY=0;unsigned compression=0,predictor=0,bits=0;
    auto values=[&](std::uint16_t type,std::uint32_t count,std::size_t field){
        std::vector<std::uint64_t> v;const std::size_t size=type==3?2:type==4?4:8;const std::size_t at=count*size<=4?field:read<std::uint32_t>(head,field);
        for(std::uint32_t i=0;i<count;++i)v.push_back(type==3?read<std::uint16_t>(head,at+2*i):type==4?read<std::uint32_t>(head,at+4*i):read<std::uint64_t>(head,at+8*i));return v;};
    for(std::uint16_t i=0;i<n;++i){
        const std::size_t e=ifd+2+12u*i;const auto tag=read<std::uint16_t>(head,e),type=read<std::uint16_t>(head,e+2);const auto count=read<std::uint32_t>(head,e+4);
        switch(tag){
        case 256:f->width=static_cast<unsigned>(values(type,1,e+8)[0]);break;case 257:f->height=static_cast<unsigned>(values(type,1,e+8)[0]);break;
        case 258:bits=static_cast<unsigned>(values(type,1,e+8)[0]);break;case 259:compression=static_cast<unsigned>(values(type,1,e+8)[0]);break;case 317:predictor=static_cast<unsigned>(values(type,1,e+8)[0]);break;
        case 322:f->tileWidth=static_cast<unsigned>(values(type,1,e+8)[0]);break;case 323:f->tileHeight=static_cast<unsigned>(values(type,1,e+8)[0]);break;
        case 324:f->offsets=values(type,count,e+8);break;case 325:f->counts=values(type,count,e+8);break;
        case 33550:{const auto at=read<std::uint32_t>(head,e+8);scaleX=read<double>(head,at);scaleY=read<double>(head,at+8);break;}
        case 33922:{const auto at=read<std::uint32_t>(head,e+8);tieX=read<double>(head,at+24);tieY=read<double>(head,at+32);break;}
        }
    }
    if(compression!=8||predictor!=3||bits!=32||!f->tileWidth||!f->tileHeight||!(scaleX>0)||!(scaleY>0)||f->offsets.size()!=f->counts.size()||f->offsets.empty())throw std::runtime_error("Unexpected DEM layout");
    f->west=tieX;f->north=tieY;f->stepX=scaleX;f->stepY=scaleY;
    return (files_[key]=f).get();
}
const std::vector<float>& CopernicusDem::block(const File& f,int index,DownloadProgress& progress){
    const auto key=std::make_pair(&f,index);if(const auto i=blocks_.find(key);i!=blocks_.end())return i->second;
    if(blocks_.size()>64)blocks_.clear(); // Bounded: 64 × 4 MiB.
    std::ostringstream name;name<<f.url.substr(f.url.rfind('/')+1)<<"."<<index;const auto path=cache_/name.str();std::string bytes;
    if(std::ifstream in{path,std::ios::binary})bytes.assign(std::istreambuf_iterator<char>(in),{});
    const auto offset=f.offsets.at(static_cast<std::size_t>(index)),count=f.counts.at(static_cast<std::size_t>(index));
    if(bytes.size()!=count){
        bytes=transport_(f.url,std::to_string(offset)+"-"+std::to_string(offset+count-1),&progress);
        if(bytes.size()!=count)throw std::runtime_error("Short DEM tile");
        std::ofstream(path,std::ios::binary)<<bytes;
    }
    return blocks_[key]=decodeFloatTile(bytes,f.tileWidth,f.tileHeight);
}
double CopernicusDem::sample(double lat,double lon,DownloadProgress& progress){
    const int la=static_cast<int>(std::floor(lat)),lo=static_cast<int>(std::floor(lon));const File* f=file(la,lo,progress);
    if(f->ocean)return 0;
    auto post=[&](long x,long y){ // Clamped to this tile's posts; neighbours share the edge rows.
        x=std::clamp<long>(x,0,f->width-1);y=std::clamp<long>(y,0,f->height-1);
        const unsigned across=(f->width+f->tileWidth-1)/f->tileWidth;const int index=static_cast<int>((y/f->tileHeight)*across+x/f->tileWidth);
        const auto& b=block(*f,index,progress);const float v=b[static_cast<std::size_t>((y%f->tileHeight)*f->tileWidth+x%f->tileWidth)];
        return v<-1000?0.:static_cast<double>(v);};
    const double sx=(lon-f->west)/f->stepX,sy=(f->north-lat)/f->stepY,ix=std::floor(sx),fx=sx-ix,iy=std::floor(sy),fy=sy-iy;
    return (1-fx)*((1-fy)*post(static_cast<long>(ix),static_cast<long>(iy))+fy*post(static_cast<long>(ix),static_cast<long>(iy)+1))
        +fx*((1-fy)*post(static_cast<long>(ix)+1,static_cast<long>(iy))+fy*post(static_cast<long>(ix)+1,static_cast<long>(iy)+1));
}
double CopernicusDem::height(double lat,double lon,DownloadProgress& progress){std::lock_guard lock(mutex_);return sample(lat,lon,progress);}
std::vector<float> CopernicusDem::heights(double west,double south,double east,double north,unsigned w,unsigned h,DownloadProgress& progress){
    if(w<2||h<2||w>8192||h>8192)throw std::invalid_argument("Invalid DEM grid");
    std::lock_guard lock(mutex_);std::vector<float> out(static_cast<std::size_t>(w)*h);
    for(unsigned y=0;y<h;++y){progress.checkCancelled();const double lat=south+(north-south)*y/(h-1);
        for(unsigned x=0;x<w;++x){const double lon=west+(east-west)*x/(w-1);out[static_cast<std::size_t>(y)*w+x]=static_cast<float>(sample(lat,lon-360.*std::floor((lon+180.)/360.),progress));}}
    return out;
}
CopernicusDem& copernicusDem(){static CopernicusDem value(dataDirectory()/"terrain");return value;}
}
