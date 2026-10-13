#include "asset_pack.hpp"
#include "platform.hpp"
#include "sha256.hpp"
#include <nlohmann/json.hpp>
#include "stb_image.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
struct Entry { std::uint64_t offset, bytes; std::string sha; };
struct Layer {
    int width,height,tile,gutter,level;std::string extension;
    bool solar=false,elevation=false,sixteen=false;
    double heightScale=1,heightOffset=0,nodata=-1;
};
struct Axis { int tile; double pixel; };
struct Span { unsigned begin, end; int tile; };
std::vector<Span> spans(const std::vector<Axis>& coordinates) {
    std::vector<Span> result;
    for (unsigned start=0;start<coordinates.size();) {
        unsigned end=start+1;
        while (end<coordinates.size() && coordinates[end].tile==coordinates[start].tile) ++end;
        result.push_back({start,end,coordinates[start].tile});start=end;
    }
    return result;
}
}

struct albert::AssetPack::Index {
    fs::path path;
    std::uint64_t size=0;
    std::map<std::string,Entry> files;
    std::map<std::string,Layer> layers;
    std::string read(const std::string& name) const {
        const auto& entry=files.at(name);
        std::ifstream stream(path,std::ios::binary);
        stream.seekg(static_cast<std::streamoff>(entry.offset));
        std::string data(static_cast<size_t>(entry.bytes),'\0');
        if (!stream.read(data.data(),static_cast<std::streamsize>(data.size())) || sha256(data)!=entry.sha)
            throw std::runtime_error("Biblioteca visual corrompida: "+name+". Restaure visuals.pak.");
        return data;
    }
};

albert::AssetPack::AssetPack(fs::path path) {
    auto index=std::make_shared<Index>();index->path=std::move(path);
    std::ifstream stream(index->path,std::ios::binary);
    if (!stream) throw std::runtime_error("Biblioteca visuals.pak ausente. Execute make assets e copie a distribuicao completa.");
    index->size=fs::file_size(index->path);
    auto integer=[](const unsigned char* p){std::uint64_t n=0;for(int i=0;i<8;++i)n|=std::uint64_t(p[i])<<(i*8);return n;};
    std::vector<std::pair<std::uint64_t,std::uint64_t>> archives;
    std::uint64_t payloadEnd=index->size;
    if(index->size>=16){
        unsigned char footer[16]{};stream.seekg(static_cast<std::streamoff>(index->size-16));stream.read(reinterpret_cast<char*>(footer),16);
        if(std::string_view(reinterpret_cast<char*>(footer+8),8)=="ALBRTPX1"){
            const auto length=integer(footer);
            if(length>16*1024*1024||length>index->size-16)throw std::runtime_error("Indice da colecao visual invalido");
            payloadEnd=index->size-16-length;stream.seekg(static_cast<std::streamoff>(payloadEnd));
            std::string data(static_cast<size_t>(length),'\0');if(!stream.read(data.data(),static_cast<std::streamsize>(length)))throw std::runtime_error("Colecao visual incompleta");
            const auto collection=json::parse(data);
            if(collection.at("container")!="albert-pack-collection-v1"||collection.at("archives").empty()||collection.at("archives").size()>256)throw std::runtime_error("Colecao visual incompativel");
            std::uint64_t end=0;
            for(const auto& item:collection.at("archives")){
                const std::uint64_t offset=item.at("offset"),bytes=item.at("bytes");
                if(offset!=end||offset>payloadEnd||bytes>payloadEnd-offset)throw std::runtime_error("Arquivo da colecao visual fora dos limites");
                archives.emplace_back(offset,bytes);end=offset+bytes;
            }
            if(end!=payloadEnd)throw std::runtime_error("Colecao visual com lacunas");
        }
    }
    if(archives.empty())archives.emplace_back(0,index->size);
    for(const auto& [offset,archiveSize]:archives){
        if(archiveSize<16)throw std::runtime_error("Arquivo visual incompleto");
        stream.clear();stream.seekg(static_cast<std::streamoff>(offset));char magic[8]{};unsigned char length[8]{};
        stream.read(magic,8);stream.read(reinterpret_cast<char*>(length),8);const auto size=integer(length);
        if(std::string_view(magic,8)!="ALBRTPK1"||size>16*1024*1024||size>archiveSize-16)throw std::runtime_error("Formato de visuals.pak invalido");
        std::string data(static_cast<size_t>(size),'\0');
        if(!stream.read(data.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("Indice visual incompleto");
        const auto manifest=json::parse(data);if(manifest.at("container")!="albert-pack-v1")throw std::runtime_error("Versao visual incompativel");
        const auto start=offset+16+size,available=archiveSize-16-size;
        for(const auto& entry:manifest.at("files")){
            Entry value{entry.at("offset"),entry.at("bytes"),entry.at("sha256")};
            if(value.bytes>32*1024*1024||value.offset>available||value.bytes>available-value.offset)throw std::runtime_error("Bloco visual fora do arquivo");
            value.offset+=start;
            if(!index->files.emplace(entry.at("path").get<std::string>(),std::move(value)).second)throw std::runtime_error("Bloco visual duplicado");
        }
        for(const auto& layer:manifest.at("layers")){
            const auto name=layer.at("id").get<std::string>();
            Layer value{layer.at("width"),layer.at("height"),layer.at("tile_size"),layer.at("gutter"),layer.at("max_level"),layer.at("format")};
            if(value.width<1||value.width>131072||value.height<1||value.height>65536||value.tile<1||value.tile>4096||value.gutter!=2||value.level<0||value.level>8||value.width<(1<<value.level)||value.height<(1<<value.level))throw std::runtime_error("Dimensoes visuais invalidas");
            value.solar=layer.value("projection",std::string{})=="orthographic-solar-image"||name=="sun";
            value.elevation=layer.value("kind",std::string{})=="elevation"||name=="earth-height"||name=="moon-height";
            value.sixteen=layer.value("sample_type",std::string{})=="uint16"||name=="moon-height";
            value.nodata=layer.value("nodata_sample",-1.);
            value.heightScale=layer.value("height_scale_m",name=="moon-height"?.5:6400./255.);
            value.heightOffset=layer.value("height_offset_m",name=="moon-height"?-10000.:0.);
            if(!std::isfinite(value.heightScale)||value.heightScale<=0||value.heightScale>1e4||!std::isfinite(value.heightOffset)||std::abs(value.heightOffset)>1e7||!std::isfinite(value.nodata)||value.nodata< -1||value.nodata>65535||(value.sixteen&&!value.elevation))throw std::runtime_error("Escala de relevo invalida");
            if(!index->layers.emplace(name,std::move(value)).second)throw std::runtime_error("Camada visual duplicada");
        }
    }
    index_=std::move(index);
}

bool albert::AssetPack::contains(const std::string& layer) const{return index_->layers.contains(layer);}

std::uint64_t albert::AssetPack::bytes() const { return index_->size; }

threepp::Image albert::AssetPack::image(const std::string& name,const GeoRegion& region,unsigned maximum,DownloadProgress& progress) const {
    progress.checkCancelled();
    const auto& layer=index_->layers.at(name);
    const bool solar=layer.solar,elevation=layer.elevation,sixteen=layer.sixteen;
    const double fullWidth=solar ? 1. : 360., fullHeight=solar ? 1. : 180.;
    const double west=solar ? region.west : region.west+180.;
    const double north=solar ? 1.-region.north : 90.-region.north;
    const double spanX=region.east-region.west,spanY=region.north-region.south;
    if (!maximum || maximum>4096 || !std::isfinite(west) || !std::isfinite(north) || !std::isfinite(spanX) || !std::isfinite(spanY) || std::abs(west)>720 || spanX<=0 || spanX>fullWidth+1e-8 || spanY<=0 || spanY>fullHeight+1e-8)
        throw std::runtime_error("Regiao visual invalida");
    int level=layer.level;
    while (level>0 && (std::ceil(spanX/fullWidth*(layer.width>>(layer.level-level)))>maximum || std::ceil(spanY/fullHeight*(layer.height>>(layer.level-level)))>maximum)) --level;
    const int width=layer.width>>(layer.level-level),height=layer.height>>(layer.level-level);
    const double scale=std::min({1.,maximum/(spanX/fullWidth*width),maximum/(spanY/fullHeight*height)});
    const unsigned outWidth=std::max(1u,static_cast<unsigned>(std::ceil(spanX/fullWidth*width*scale)));
    const unsigned outHeight=std::max(1u,static_cast<unsigned>(std::ceil(spanY/fullHeight*height*scale)));
    std::vector<Axis> xs(outWidth),ys(outHeight);
    for (unsigned x=0;x<outWidth;++x) {
        const double pixel=(west+(x+.5)*spanX/outWidth)/fullWidth*width-.5;
        const int tile=static_cast<int>(std::floor((pixel+.5)/layer.tile));
        xs[x]={tile,pixel-tile*layer.tile+layer.gutter};
    }
    for (unsigned y=0;y<outHeight;++y) {
        const double pixel=std::clamp((north+(y+.5)*spanY/outHeight)/fullHeight*height-.5,-.5,height-.5);
        const int tile=std::clamp(static_cast<int>(std::floor((pixel+.5)/layer.tile)),0,(height-1)/layer.tile);
        ys[y]={tile,pixel-tile*layer.tile+layer.gutter};
    }
    const auto columns=spans(xs),rows=spans(ys);
    const int totalColumns=(width+layer.tile-1)/layer.tile;
    std::vector<unsigned char> pixels(elevation ? 0 : static_cast<size_t>(outWidth)*outHeight*4);
    std::vector<float> heights(elevation ? static_cast<size_t>(outWidth)*outHeight*4 : 0);
    unsigned completed=0;
    for (const auto& row:rows) for (const auto& col:columns) {
        progress.checkCancelled();
        const int column=solar ? std::clamp(col.tile,0,totalColumns-1) : (col.tile%totalColumns+totalColumns)%totalColumns;
        const auto key="v1/"+name+"/"+std::to_string(level)+"/"+std::to_string(row.tile)+"/"+std::to_string(column)+"."+layer.extension;
        progress.stage("Lendo biblioteca: "+name,static_cast<int>(completed));
        progress.transfer(completed,rows.size()*columns.size());
        const auto bytes=index_->read(key);
        int tw=0,th=0;
        const int channels=sixteen ? 1 : 4;
        std::unique_ptr<void,decltype(&stbi_image_free)> decoded(sixteen ? static_cast<void*>(stbi_load_16_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),static_cast<int>(bytes.size()),&tw,&th,nullptr,1)) : static_cast<void*>(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),static_cast<int>(bytes.size()),&tw,&th,nullptr,4)),stbi_image_free);
        if (!decoded || tw!=std::min(layer.tile,width-column*layer.tile)+2*layer.gutter || th!=std::min(layer.tile,height-row.tile*layer.tile)+2*layer.gutter)
            throw std::runtime_error("Imagem empacotada invalida: "+key);
        auto sample=[&](int x,int y,int channel) {
            const size_t at=(static_cast<size_t>(y)*tw+x)*channels+channel;
            const float value=sixteen ? static_cast<float>(static_cast<const stbi_us*>(decoded.get())[at]) : static_cast<float>(static_cast<const stbi_uc*>(decoded.get())[at]);
            return elevation&&value==layer.nodata?static_cast<float>(-layer.heightOffset/layer.heightScale):value;
        };
        for (unsigned y=row.begin;y<row.end;++y) {
            if ((y&63u)==0) progress.checkCancelled();
            const double py=std::clamp(ys[y].pixel,0.,static_cast<double>(th-1));
            const int y0=std::min(static_cast<int>(py),th-2);const float fy=static_cast<float>(py-y0);
            for (unsigned x=col.begin;x<col.end;++x) {
                const double px=std::clamp(xs[x].pixel,0.,static_cast<double>(tw-1));
                const int x0=std::min(static_cast<int>(px),tw-2);const float fx=static_cast<float>(px-x0);
                const size_t at=4*(static_cast<size_t>(outHeight-1-y)*outWidth+x);
                for (int c=0;c<(elevation ? 1 : 4);++c) {
                    const float top=sample(x0,y0,c)*(1-fx)+sample(x0+1,y0,c)*fx;
                    const float bottom=sample(x0,y0+1,c)*(1-fx)+sample(x0+1,y0+1,c)*fx;
                    const float value=top*(1-fy)+bottom*fy;
                    if (elevation) {
                        const float meters=static_cast<float>(layer.heightOffset+value*layer.heightScale);
                        heights[at]=heights[at+1]=heights[at+2]=meters;heights[at+3]=1.f;
                    } else pixels[at+c]=static_cast<unsigned char>(std::clamp(std::round(value),0.f,255.f));
                }
            }
        }
        ++completed;
    }
    progress.transfer(completed,completed);
    if (elevation) return threepp::Image(std::move(heights),outWidth,outHeight);
    return threepp::Image(std::move(pixels),outWidth,outHeight);
}

std::shared_ptr<const albert::AssetPack> albert::visualAssetPack() {
    static const auto pack=[] {
        auto path=environmentPath("ALBERT_PACK_PATH");
        if (path.empty()) path=assetDirectory()/"visuals.pak";
#ifdef ALBERT_DEVELOPMENT_PACK_PATH
        if (!fs::is_regular_file(path) && environmentPath("ALBERT_PACK_PATH").empty()) path=fs::path(ALBERT_DEVELOPMENT_PACK_PATH);
#endif
        return std::make_shared<const AssetPack>(std::move(path));
    }();
    return pack;
}
