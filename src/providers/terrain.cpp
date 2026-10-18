#include "terrain.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <random>
#include <thread>
extern "C" int stbi_zlib_decode_buffer(char* out,int outLength,const char* in,int inLength);
namespace albert::providers {
namespace {
template<class T>T read(const std::string& b,std::size_t at){if(at+sizeof(T)>b.size())throw std::runtime_error("Truncated GeoTIFF header");T v;std::memcpy(&v,b.data()+at,sizeof v);return v;}
std::string tileName(int lat,int lon){
    char s[96];std::snprintf(s,sizeof s,"Copernicus_DSM_COG_10_%c%02d_00_%c%03d_00_DEM",lat<0?'S':'N',std::abs(lat),lon<0?'W':'E',std::abs(lon));return s;
}
}
std::vector<float> decodeFloatTile(const std::string& compressed,unsigned w,unsigned h){
    if(!w||!h||w>2048||h>2048||compressed.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))throw std::runtime_error("Unsupported DEM tile size"); // Before allocating.
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
namespace {
struct NotCached {}; // Thrown inside cachedHeight when a piece needs the network.
// Whole files only: written beside (a name per process and thread: two instances may share the
// cache) and renamed, so no reader or crash leaves a partial one; false when it did not land.
bool publish(const std::filesystem::path& path,const std::string& bytes){
    static const auto process=std::random_device{}();
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
    auto part=path;part+=".part"+std::to_string(process)+"-"+std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    {std::ofstream out(part,std::ios::binary);out<<bytes;if(!out.flush()){out.close();std::filesystem::remove(part,ec);return false;}}
    std::filesystem::rename(part,path,ec);if(ec){std::filesystem::remove(part,ec);return false;}
    return true;
}
void stored(const std::filesystem::path& path,const std::string& bytes){if(!publish(path,bytes))throw std::runtime_error("Cache write failed: "+path.filename().string());}
}
struct CopernicusDem::File {int latitude=0,longitude=0;std::string url;unsigned width=0,height=0,tileWidth=0,tileHeight=0;double west=0,north=0,stepX=0,stepY=0;std::vector<std::uint64_t> offsets,counts;bool ocean=false;};
CopernicusDem::CopernicusDem(std::filesystem::path cache,Transport transport):cache_(std::move(cache)),transport_(std::move(transport)){
    if(!transport_)transport_=[](const std::string& url,const std::string& range,DownloadProgress* p){return httpGet(url,p,{16*1024*1024,60,range});};
}
// A COG header's tiling and georeference; false when it is not one (truncated, corrupt).
bool CopernicusDem::parse(const std::string& head,File& out){
    try{auto* f=&out;
    if(head.size()<8||head.compare(0,2,"II")!=0||read<std::uint16_t>(head,2)!=42)return false;
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
    if(compression!=8||predictor!=3||bits!=32||!(scaleX>0)||!(scaleY>0)||f->offsets.size()!=f->counts.size()||f->offsets.empty())return false;
    // Tiles of at most 2048 posts a side (Copernicus: 1024), as many as the image needs: a corrupt size
    // would otherwise size the decode (65535² floats: 16 GiB).
    auto tile=[](unsigned t){return t>=1&&t<=2048;};
    if(!tile(f->tileWidth)||!tile(f->tileHeight)||!f->width||!f->height||f->width>100000||f->height>100000)return false;
    if(f->offsets.size()!=static_cast<std::size_t>((f->width+f->tileWidth-1)/f->tileWidth)*((f->height+f->tileHeight-1)/f->tileHeight))return false;
    for(const auto c:f->counts)if(c>static_cast<std::uint64_t>(f->tileWidth)*f->tileHeight*8+65536)return false; // A tile's bytes: at most twice the raw size.
    f->west=tieX;f->north=tieY;f->stepX=scaleX;f->stepY=scaleY;return true;
    }catch(const std::exception&){return false;}
}
const CopernicusDem::File* CopernicusDem::file(int lat,int lon,DownloadProgress& progress){
    const auto key=std::make_pair(lat,lon);if(const auto i=files_.find(key);i!=files_.end())return i->second.get();
    auto f=std::make_shared<File>();f->latitude=lat;f->longitude=lon;const std::string name=tileName(lat,lon);
    f->url="https://copernicus-dem-30m.s3.amazonaws.com/"+name+"/"+name+".tif";
    const auto path=cache_/(name+".header");std::string head;
    if(std::ifstream in{path,std::ios::binary})head.assign(std::istreambuf_iterator<char>(in),{});
    if(!head.empty()&&head!="ocean"&&!parse(head,*f)){std::error_code ec;std::filesystem::remove(path,ec);head.clear();} // Corrupt: fetched again.
    if(head.empty()){
        if(offline_){missing_.insert({lat,lon,-1});throw NotCached{};}
        try{head=transport_(f->url,"0-65535",&progress);}
        catch(const std::exception& e){if(std::string(e.what()).find("404")==std::string::npos&&std::string(e.what()).find("403")==std::string::npos)throw;head="ocean";} // Absent: no land.
        if(head!="ocean"&&!parse(head,*f))throw std::runtime_error("Unsupported DEM GeoTIFF");
        publish(path,head);
    }
    if(head=="ocean")f->ocean=true;
    return (files_[key]=f).get();
}
const std::vector<float>& CopernicusDem::block(const File& f,int index,DownloadProgress& progress){
    const auto key=std::make_pair(&f,index);if(const auto i=blocks_.find(key);i!=blocks_.end())return i->second;
    if(blocks_.size()>64)blocks_.clear(); // Bounded: 64 × 4 MiB.
    std::ostringstream name;name<<f.url.substr(f.url.rfind('/')+1)<<"."<<index;const auto path=cache_/name.str();std::string bytes;
    if(std::ifstream in{path,std::ios::binary})bytes.assign(std::istreambuf_iterator<char>(in),{});
    const auto offset=f.offsets.at(static_cast<std::size_t>(index)),count=f.counts.at(static_cast<std::size_t>(index));
    if(bytes.size()==count)try{return blocks_[key]=decodeFloatTile(bytes,f.tileWidth,f.tileHeight);} // Corrupt though sized right: fetched again.
        catch(const std::exception&){std::error_code ec;std::filesystem::remove(path,ec);}
    if(offline_){missing_.insert({f.latitude,f.longitude,index});throw NotCached{};}
    bytes=transport_(f.url,std::to_string(offset)+"-"+std::to_string(offset+count-1),&progress);
    if(bytes.size()!=count)throw std::runtime_error("Short DEM tile");
    auto tile=decodeFloatTile(bytes,f.tileWidth,f.tileHeight);publish(path,bytes); // Used even if the cache cannot keep it.
    return blocks_[key]=std::move(tile);
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
std::optional<double> CopernicusDem::cachedHeight(double lat,double lon){
    std::lock_guard lock(mutex_);offline_=true;DownloadProgress unused;
    try{const double h=sample(lat,lon,unused);offline_=false;return h;}catch(const NotCached&){offline_=false;return std::nullopt;}catch(...){offline_=false;throw;}
}
bool CopernicusDem::fetchMissing(DownloadProgress& progress){
    std::tuple<int,int,int> item;
    {std::lock_guard lock(mutex_);if(missing_.empty())missing_.swap(failed_); // Failed pieces again once the noted ones are done.
     if(missing_.empty())return false;item=*missing_.begin();missing_.erase(missing_.begin());}
    try{
    const auto [lat,lon,index]=item;const std::string name=tileName(lat,lon),url="https://copernicus-dem-30m.s3.amazonaws.com/"+name+"/"+name+".tif";
    std::error_code ec;std::filesystem::create_directories(cache_,ec);
    if(index<0){
        const auto path=cache_/(name+".header");if(std::filesystem::exists(path,ec)){++generation_;return true;}
        std::string head;
        try{head=transport_(url,"0-65535",&progress);}
        catch(const std::exception& e){if(std::string(e.what()).find("404")==std::string::npos&&std::string(e.what()).find("403")==std::string::npos)throw;head="ocean";}
        if(File probe;head!="ocean"&&!parse(head,probe))throw std::runtime_error("Unsupported DEM GeoTIFF"); // A portal's page, say: not cached.
        stored(path,head);
    }else{
        std::uint64_t offset=0,count=0;unsigned tileWidth=0,tileHeight=0;
        {std::lock_guard lock(mutex_);const File* f=file(lat,lon,progress); // The header is cached by now.
         offset=f->offsets.at(static_cast<std::size_t>(index));count=f->counts.at(static_cast<std::size_t>(index));tileWidth=f->tileWidth;tileHeight=f->tileHeight;}
        std::ostringstream block;block<<name<<".tif."<<index;const auto path=cache_/block.str();
        if(std::filesystem::file_size(path,ec)!=count){
            const auto bytes=transport_(url,std::to_string(offset)+"-"+std::to_string(offset+count-1),&progress);
            if(bytes.size()!=count)throw std::runtime_error("Short DEM tile");
            (void)decodeFloatTile(bytes,tileWidth,tileHeight); // Throws on a corrupt answer: not cached.
            stored(path,bytes);
        }
    }
    }catch(...){std::lock_guard lock(mutex_);failed_.insert(item);throw;} // Kept: a camera at rest would never note it again.
    ++generation_;return true;
}
std::vector<float> CopernicusDem::heights(double west,double south,double east,double north,unsigned w,unsigned h,DownloadProgress& progress){
    if(w<2||h<2||w>8192||h>8192)throw std::invalid_argument("Invalid DEM grid");
    std::lock_guard lock(mutex_);std::vector<float> out(static_cast<std::size_t>(w)*h);
    for(unsigned y=0;y<h;++y){progress.checkCancelled();const double lat=south+(north-south)*y/(h-1);
        for(unsigned x=0;x<w;++x){const double lon=west+(east-west)*x/(w-1);out[static_cast<std::size_t>(y)*w+x]=static_cast<float>(sample(lat,lon-360.*std::floor((lon+180.)/360.),progress));}}
    return out;
}
CopernicusDem& copernicusDem(){static CopernicusDem value(dataDirectory()/"terrain");return value;}
PlanetaryRelief::PlanetaryRelief(Transport transport,std::filesystem::path cache):cache_(std::move(cache)),transport_(std::move(transport)){
    if(cache_.empty())cache_=dataDirectory()/"relief";
    if(!transport_)transport_=[](const std::string& url,const std::string& range,DownloadProgress* p){return httpGet(url,p,{1u<<20,60,range});};
}
std::vector<std::string> byteRanges(const std::string& body){
    std::vector<std::pair<std::uint64_t,std::string>> parts;std::size_t at=0;
    while((at=body.find("Content-Range: bytes ",at))!=std::string::npos){
        const std::size_t dash=body.find('-',at+21),slash=body.find('/',at+21),data=body.find("\r\n\r\n",at); // After "Content-Range: bytes " (its own hyphen).
        if(dash==std::string::npos||slash==std::string::npos||data==std::string::npos)break;
        const auto first=std::stoull(body.substr(at+21,dash-at-21)),last=std::stoull(body.substr(dash+1,slash-dash-1));
        if(data+4+(last-first+1)>body.size())throw std::runtime_error("Truncated multipart answer");
        parts.emplace_back(first,body.substr(data+4,last-first+1));at=data+4+(last-first+1);
    }
    std::sort(parts.begin(),parts.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<std::string> out;for(auto& [first,part]:parts)out.push_back(std::move(part));return out;
}
namespace {
// File and byte offset of a post: LOLA in one image, MOLA in 44° × 90° tiles named by their northern
// edge and western longitude (blocks never straddle them: 5632 and 11520 are multiples of 128).
std::pair<std::string,std::size_t> reliefPost(bool moon,int row,int column){
    if(moon)return {"https://pds-geosciences.wustl.edu/lro/lro-l-lola-3-rdr-v1/lrolol_1xxx/data/lola_gdr/cylindrical/img/ldem_128.img",(static_cast<std::size_t>(row)*46080+static_cast<std::size_t>(column))*2};
    static constexpr const char* bands[]{"88n","44n","00n","44s"};static constexpr const char* sectors[]{"000","090","180","270"};
    return {std::string("https://pds-geosciences.wustl.edu/mgs/mgs-m-mola-5-megdr-l3-v1/mgsl_300x/meg128/megr")+bands[row/5632]+sectors[column/11520]+"hb.img",
        (static_cast<std::size_t>(row%5632)*11520+static_cast<std::size_t>(column%11520))*2};
}
double reliefValue(bool moon,unsigned char first,unsigned char second){ // LOLA: little-endian height / 2; MOLA: big-endian radius − 3396 km.
    const auto lo=moon?first:second,hi=moon?second:first;const auto dn=static_cast<std::int16_t>(static_cast<std::uint16_t>(hi<<8|lo));return moon?dn*.5:dn+3396000.;
}
std::string blockName(int body,int row,int column){return std::to_string(body)+"."+std::to_string(row)+"."+std::to_string(column);}
}
double PlanetaryRelief::pixel(physics::Center body,int row,int column,DownloadProgress& progress){
    if(const auto it=pixels_.find({static_cast<int>(body),row,column});it!=pixels_.end())return it->second;
    const bool moon=body==physics::Center::moon;const auto [url,offset]=reliefPost(moon,row,column);
    const bool pair=moon?column+1<46080:(column+1)%11520!=0; // The next pixel in the same request.
    const std::string b=transport_(url,std::to_string(offset)+"-"+std::to_string(offset+(pair?3:1)),&progress);
    if(b.size()!=(pair?4u:2u))throw std::runtime_error("Truncated relief sample");
    auto value=[&](std::size_t k){return reliefValue(moon,static_cast<unsigned char>(b[k]),static_cast<unsigned char>(b[k+1]));};
    pixels_[{static_cast<int>(body),row,column}]=value(0);if(pair)pixels_[{static_cast<int>(body),row,column+1}]=value(2);
    return value(0);
}
template<class Post> double PlanetaryRelief::sampleGrid(physics::Center body,double latitude,double longitude,Post post){
    const bool moon=body==physics::Center::moon;const double top=moon?90:88;const int rows=moon?23040:22528,columns=46080;
    if(!(std::abs(latitude)<=top))throw std::invalid_argument(moon?"Invalid latitude":"MOLA covers latitudes up to 88°");
    const double y=std::clamp((top-latitude)*128-.5,0.,rows-1.),x=(longitude-360*std::floor(longitude/360))*128-.5;
    const int i=std::min(static_cast<int>(y),rows-2);const double fy=y-i,fx0=x-std::floor(x);const int j=static_cast<int>(std::floor(x));
    auto at=[&](int r,int c){return post(r,(c%columns+columns)%columns);};
    return(1-fy)*((1-fx0)*at(i,j)+fx0*at(i,j+1))+fy*((1-fx0)*at(i+1,j)+fx0*at(i+1,j+1));
}
double PlanetaryRelief::bilinear(physics::Center body,double latitude,double longitude,DownloadProgress& progress){
    return sampleGrid(body,latitude,longitude,[&](int r,int c){return pixel(body,r,c,progress);});
}
double PlanetaryRelief::blockPost(physics::Center body,int row,int column){
    const BlockKey key{static_cast<int>(body),row/128,column/128};auto it=blocks_.find(key);
    if(it==blocks_.end()){
        std::string bytes;if(std::ifstream in{cache_/blockName(key.body,key.row,key.column),std::ios::binary})bytes.assign(std::istreambuf_iterator<char>(in),{});
        if(bytes.size()!=128*128*2){missingBlocks_.insert(key);throw NotCached{};}
        if(blocks_.size()>=512)blocks_.erase(blocks_.begin()); // Bounded: 512 × 64 KiB.
        const bool moon=body==physics::Center::moon;std::vector<float> posts(128*128);
        for(std::size_t i=0;i<posts.size();++i)posts[i]=static_cast<float>(reliefValue(moon,static_cast<unsigned char>(bytes[2*i]),static_cast<unsigned char>(bytes[2*i+1])));
        it=blocks_.emplace(key,std::move(posts)).first;
    }
    return it->second[static_cast<std::size_t>((row%128)*128+column%128)];
}
std::optional<double> PlanetaryRelief::cachedHeight(physics::Center body,double latitude,double longitude){
    if(body==physics::Center::earth)throw std::invalid_argument("Earth relief comes from Copernicus");
    std::lock_guard lock(mutex_);
    try{
        auto post=[&](int r,int c){return blockPost(body,r,c);};
        if(body==physics::Center::moon)return sampleGrid(body,latitude,longitude,post);
        constexpr double deg=3.14159265358979323846/180;const physics::V3 p=physics::fixedPosition(physics::planet(physics::Center::mars),{latitude*deg,longitude*deg,0});
        const double r=physics::norm(p);return sampleGrid(body,std::asin(p.z/r)/deg,longitude,post)-r;
    }catch(const NotCached&){return std::nullopt;}
}
bool PlanetaryRelief::fetchMissing(DownloadProgress& progress){
    BlockKey key{};{std::lock_guard lock(mutex_);if(missingBlocks_.empty())missingBlocks_.swap(failedBlocks_); // Failed blocks again once the noted ones are done.
     if(missingBlocks_.empty())return false;key=*missingBlocks_.begin();missingBlocks_.erase(missingBlocks_.begin());}
    try{
    const auto path=cache_/blockName(key.body,key.row,key.column);std::error_code ec;
    if(std::filesystem::file_size(path,ec)==128*128*2){++generation_;return true;}
    const bool moon=key.body==static_cast<int>(physics::Center::moon);std::string url,ranges;
    for(int r=0;r<128;++r){const auto [file,offset]=reliefPost(moon,key.row*128+r,key.column*128);url=file;ranges+=(r?",":"")+std::to_string(offset)+"-"+std::to_string(offset+255);}
    const auto parts=byteRanges(transport_(url,ranges,&progress));
    std::string bytes;for(const auto& part:parts)bytes+=part;
    if(parts.size()!=128||bytes.size()!=128*128*2)throw std::runtime_error("Unexpected relief block answer");
    stored(path,bytes);
    }catch(...){std::lock_guard lock(mutex_);failedBlocks_.insert(key);throw;} // Kept: a camera at rest would never note it again.
    ++generation_;return true;
}
double PlanetaryRelief::height(physics::Center body,double latitude,double longitude,DownloadProgress& progress){
    if(body==physics::Center::earth)throw std::invalid_argument("Earth relief comes from Copernicus");
    std::lock_guard lock(mutex_);
    if(body==physics::Center::moon)return bilinear(body,latitude,longitude,progress);
    // MOLA radii are planetocentric: sample under the ellipsoid point and subtract its radius.
    constexpr double deg=3.14159265358979323846/180;const physics::V3 p=physics::fixedPosition(physics::planet(physics::Center::mars),{latitude*deg,longitude*deg,0});
    const double r=physics::norm(p);return bilinear(body,std::asin(p.z/r)/deg,longitude,progress)-r;
}
PlanetaryRelief& planetaryRelief(){static PlanetaryRelief value;return value;}
}
