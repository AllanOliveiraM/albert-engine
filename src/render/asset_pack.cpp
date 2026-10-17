#include "asset_pack.hpp"
#include "platform.hpp"
#include "sha256.hpp"
#include <nlohmann/json.hpp>
#include "stb_image.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
// Keep the opened archive, so an atomic asset update cannot pair an old index
// with bytes from a new file. Only the short seek/read is serialized.
class ArchiveFile {
public:
    explicit ArchiveFile(const fs::path& path){
#ifdef _WIN32
        handle_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_RANDOM_ACCESS,nullptr);
        LARGE_INTEGER length{};
        if(handle_==INVALID_HANDLE_VALUE)throw std::runtime_error("Biblioteca visuals.pak ausente");
        if(!GetFileSizeEx(handle_,&length)||length.QuadPart<0){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;throw std::runtime_error("Tamanho da biblioteca indisponivel");}
        size=static_cast<std::uint64_t>(length.QuadPart);
#else
        stream_.open(path,std::ios::binary|std::ios::ate);
        if(!stream_)throw std::runtime_error("Biblioteca visuals.pak ausente. Execute make assets e copie a distribuicao completa.");
        const auto length=stream_.tellg();if(length<0)throw std::runtime_error("Tamanho da biblioteca indisponivel");size=static_cast<std::uint64_t>(length);
#endif
    }
    ~ArchiveFile(){
#ifdef _WIN32
        if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);
#endif
    }
    void read(std::uint64_t offset,void* output,std::size_t amount) const {
        if(offset>size||amount>size-offset)throw std::runtime_error("Leitura fora da biblioteca visual");
        std::lock_guard lock(mutex_);
#ifdef _WIN32
        LARGE_INTEGER position{};position.QuadPart=static_cast<LONGLONG>(offset);DWORD count=0;
        if(amount>MAXDWORD||!SetFilePointerEx(handle_,position,nullptr,FILE_BEGIN)||!ReadFile(handle_,output,static_cast<DWORD>(amount),&count,nullptr)||count!=amount)throw std::runtime_error("Biblioteca visual incompleta");
#else
        stream_.clear();stream_.seekg(static_cast<std::streamoff>(offset));
        if(!stream_.read(static_cast<char*>(output),static_cast<std::streamsize>(amount)))throw std::runtime_error("Biblioteca visual incompleta");
#endif
    }
    std::string read(std::uint64_t offset,std::size_t amount) const {std::string bytes(amount,'\0');read(offset,bytes.data(),amount);return bytes;}
    std::uint64_t size=0;
private:
    mutable std::mutex mutex_;
#ifdef _WIN32
    HANDLE handle_=INVALID_HANDLE_VALUE;
#else
    mutable std::ifstream stream_;
#endif
};
struct Entry { std::uint64_t offset, bytes; std::string sha; std::size_t archive=0; };
struct Layer {
    int width,height,tile,gutter,level;std::string extension;
    bool solar=false,elevation=false,sixteen=false;
    double heightScale=1,heightOffset=0,nodata=-1;
    int available=-1;
    bool sameAs(const Layer& o) const {return width==o.width&&height==o.height&&tile==o.tile&&gutter==o.gutter&&level==o.level&&extension==o.extension&&solar==o.solar&&elevation==o.elevation&&sixteen==o.sixteen&&heightScale==o.heightScale&&heightOffset==o.heightOffset&&nodata==o.nodata;}
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
    std::vector<std::shared_ptr<ArchiveFile>> archives;
    std::uint64_t size=0;
    std::map<std::string,Entry> files;
    std::map<std::string,Layer> layers;
    void load(const fs::path& path);
    std::string read(const std::string& name) const {
        const auto found=files.find(name);
        if(found==files.end())throw std::runtime_error("Bloco visual ainda nao disponivel: "+name);
        const auto& entry=found->second;
        auto data=archives[entry.archive]->read(entry.offset,static_cast<size_t>(entry.bytes));
        if (sha256(data)!=entry.sha)
            throw std::runtime_error("Biblioteca visual corrompida: "+name+". Restaure visuals.pak.");
        return data;
    }
};

void albert::AssetPack::Index::load(const fs::path& path) {
    const auto archive=std::make_shared<ArchiveFile>(path);const auto which=archives.size();archives.push_back(archive);
    const auto fileSize=archive->size;size+=fileSize;
    auto integer=[](const unsigned char* p){std::uint64_t n=0;for(int i=0;i<8;++i)n|=std::uint64_t(p[i])<<(i*8);return n;};
    std::vector<std::pair<std::uint64_t,std::uint64_t>> spansIn;
    std::uint64_t payloadEnd=fileSize;
    if(fileSize>=16){
        unsigned char footer[16]{};archive->read(fileSize-16,footer,16);
        if(std::string_view(reinterpret_cast<char*>(footer+8),8)=="ALBRTPX1"){
            const auto length=integer(footer);
            if(length>16*1024*1024||length>fileSize-16)throw std::runtime_error("Indice da colecao visual invalido");
            payloadEnd=fileSize-16-length;
            const auto data=archive->read(payloadEnd,static_cast<size_t>(length));
            const auto collection=json::parse(data);
            if(collection.at("container")!="albert-pack-collection-v1"||collection.at("archives").empty()||collection.at("archives").size()>256)throw std::runtime_error("Colecao visual incompativel");
            std::uint64_t end=0;
            for(const auto& item:collection.at("archives")){
                const std::uint64_t offset=item.at("offset"),bytes=item.at("bytes");
                if(offset!=end||offset>payloadEnd||bytes>payloadEnd-offset)throw std::runtime_error("Arquivo da colecao visual fora dos limites");
                spansIn.emplace_back(offset,bytes);end=offset+bytes;
            }
            if(end!=payloadEnd)throw std::runtime_error("Colecao visual com lacunas");
        }
    }
    if(spansIn.empty())spansIn.emplace_back(0,fileSize);
    for(const auto& [offset,archiveSize]:spansIn){
        if(archiveSize<16)throw std::runtime_error("Arquivo visual incompleto");
        unsigned char header[16]{};archive->read(offset,header,16);const auto indexBytes=integer(header+8);
        if(std::string_view(reinterpret_cast<char*>(header),8)!="ALBRTPK1"||indexBytes>16*1024*1024||indexBytes>archiveSize-16)throw std::runtime_error("Formato de visuals.pak invalido");
        const auto data=archive->read(offset+16,static_cast<size_t>(indexBytes));
        const auto manifest=json::parse(data);if(manifest.at("container")!="albert-pack-v1")throw std::runtime_error("Versao visual incompativel");
        const auto start=offset+16+indexBytes,available=archiveSize-16-indexBytes;
        std::uint64_t declaredEnd=0;
        for(const auto& entry:manifest.at("files")){
            Entry value{entry.at("offset"),entry.at("bytes"),entry.at("sha256"),which};
            if(value.bytes>32*1024*1024||value.offset>available||value.bytes>available-value.offset)throw std::runtime_error("Bloco visual fora do arquivo");
            if(value.offset!=declaredEnd)throw std::runtime_error("Blocos visuais com lacunas ou sobreposicao");
            declaredEnd=value.offset+value.bytes;
            value.offset+=start;
            if(!files.emplace(entry.at("path").get<std::string>(),std::move(value)).second)throw std::runtime_error("Bloco visual duplicado");
        }
        if(declaredEnd!=available)throw std::runtime_error("Dados extras ou colecao visual incompleta");
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
            // Download containers repeat a layer's description; it must agree exactly.
            if(const auto known=layers.find(name);known!=layers.end()){if(!known->second.sameAs(value))throw std::runtime_error("Camada visual divergente entre arquivos: "+name);}
            else layers.emplace(name,std::move(value));
        }
    }
}

albert::AssetPack::AssetPack(fs::path path):AssetPack(std::vector<fs::path>{std::move(path)}){}

albert::AssetPack::AssetPack(const std::vector<fs::path>& paths) {
    if(paths.empty()||paths.size()>256)throw std::runtime_error("Biblioteca visual sem arquivos");
    auto index=std::make_shared<Index>();
    for(const auto& path:paths)index->load(path);
    // A level is usable only when every tile of it and of all coarser levels exists.
    for(auto& [name,layer]:index->layers){
        for(int level=0;level<=layer.level;++level){
            const int width=layer.width>>(layer.level-level),height=layer.height>>(layer.level-level);bool complete=true;
            for(int row=0;complete&&row<(height+layer.tile-1)/layer.tile;++row)
                for(int column=0;complete&&column<(width+layer.tile-1)/layer.tile;++column)
                    complete=index->files.contains("v1/"+name+"/"+std::to_string(level)+"/"+std::to_string(row)+"/"+std::to_string(column)+"."+layer.extension);
            if(!complete)break;
            layer.available=level;
        }
    }
    index_=std::move(index);
}

bool albert::AssetPack::contains(const std::string& layer) const{return availableLevel(layer)>=0;}
int albert::AssetPack::availableLevel(const std::string& layer) const{const auto found=index_->layers.find(layer);return found==index_->layers.end()?-1:found->second.available;}
int albert::AssetPack::maximumLevel(const std::string& layer) const{const auto found=index_->layers.find(layer);return found==index_->layers.end()?-1:found->second.level;}
bool albert::AssetPack::hasPayload(const std::string& path) const{return index_->files.contains(path);}
std::string albert::AssetPack::payload(const std::string& path) const{return index_->read(path);}

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
    // Choose the smallest source level that can fill the requested texture,
    // then resample into the fixed budget. Choosing the level below the budget
    // loses detail, especially with non-power-of-two planetary mosaics.
    // Download containers may not have the finer levels yet: never read past them.
    if(layer.available<0)throw std::runtime_error("Camada visual ainda nao disponivel: "+name);
    int level=0;
    while(level<layer.available && std::max(spanX/fullWidth*(layer.width>>(layer.level-level)),spanY/fullHeight*(layer.height>>(layer.level-level)))<maximum)++level;
    const int width=layer.width>>(layer.level-level),height=layer.height>>(layer.level-level);
    const double scale=std::min({1.,maximum/(spanX/fullWidth*width),maximum/(spanY/fullHeight*height)});
    const unsigned outWidth=std::max(1u,std::min(maximum,static_cast<unsigned>(std::ceil(spanX/fullWidth*width*scale))));
    const unsigned outHeight=std::max(1u,std::min(maximum,static_cast<unsigned>(std::ceil(spanY/fullHeight*height*scale))));
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
    // Tiles are work within the caller's stage, not additional global stages.
    progress.stage("Lendo biblioteca: "+name,progress.snapshot().completed);
    for (const auto& row:rows) for (const auto& col:columns) {
        progress.checkCancelled();
        const int column=solar ? std::clamp(col.tile,0,totalColumns-1) : (col.tile%totalColumns+totalColumns)%totalColumns;
        const auto key="v1/"+name+"/"+std::to_string(level)+"/"+std::to_string(row.tile)+"/"+std::to_string(column)+"."+layer.extension;
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

albert::AssetCatalog albert::readAssetCatalog(const fs::path& path) {
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Catalogo de assets ausente");
    const auto value=json::parse(stream);AssetCatalog catalog;
    if(value.at("schema")!="albert-asset-catalog-v1")throw std::runtime_error("Catalogo de assets incompativel");
    catalog.id=value.at("id");catalog.release=value.at("release");
    const auto hex=[](const std::string& text){return text.size()==64&&std::all_of(text.begin(),text.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});};
    if(!hex(catalog.id)||catalog.release.empty()||catalog.release.size()>64||catalog.release.find_first_of("/\\?#")!=std::string::npos)throw std::runtime_error("Identificacao do catalogo invalida");
    for(const auto& item:value.at("containers")){
        AssetCatalog::Container container{item.at("file"),item.at("group"),item.at("sha256"),item.at("level"),item.at("essential"),item.at("bytes")};
        const auto& file=container.file;
        if(file.size()<5||file.size()>64||file.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=std::string::npos||file.ends_with(".pak")==false||file.front()=='.'||!hex(container.sha256)||container.bytes<16||container.bytes>=(std::uint64_t(1)<<31))throw std::runtime_error("Container de assets invalido no catalogo");
        catalog.containers.push_back(std::move(container));
    }
    if(catalog.containers.empty()||catalog.containers.size()>256||!catalog.containers.front().essential||std::count_if(catalog.containers.begin(),catalog.containers.end(),[](const auto& c){return c.essential;})!=1)throw std::runtime_error("Catalogo de assets sem container essencial unico");
    return catalog;
}

std::vector<fs::path> albert::assetContainerDirectories() {
    std::vector<fs::path> result{assetDirectory()/"packs"};
    try{const auto data=dataDirectory()/"packs";if(data!=result.front())result.push_back(data);}catch(const std::exception&){}
    return result;
}

namespace {
struct Library {
    std::mutex mutex,install;std::shared_ptr<const albert::AssetPack> pack;std::vector<fs::path> paths;
    std::optional<albert::AssetCatalog> catalog;bool ready=false,complete=false;
};
Library& library(){static Library value;return value;}
std::atomic<std::uint64_t> generation{0};
std::optional<albert::AssetCatalog> installedCatalog(){
    const auto path=albert::assetDirectory()/"catalog.json";
    return fs::is_regular_file(path)?std::optional(albert::readAssetCatalog(path)):std::nullopt;
}
bool completeFor(const std::optional<albert::AssetCatalog>& catalog,const std::vector<fs::path>& paths){
    if(!catalog)return true;
    return std::all_of(catalog->containers.begin(),catalog->containers.end(),[&](const auto& c){return std::any_of(paths.begin(),paths.end(),[&](const fs::path& p){return p.filename()==c.file;});});
}
// Complete library first (explicit path, distribution, catalog-less development
// build); otherwise every catalogued container already verified on disk.
void initialize(Library& state){
    if(state.ready)return;
    auto path=albert::environmentPath("ALBERT_PACK_PATH");
    if(path.empty()&&fs::is_regular_file(albert::assetDirectory()/"visuals.pak"))path=albert::assetDirectory()/"visuals.pak";
    state.catalog=path.empty()?installedCatalog():std::nullopt;
#ifdef ALBERT_DEVELOPMENT_PACK_PATH
    if(path.empty()&&!state.catalog)path=fs::path(ALBERT_DEVELOPMENT_PACK_PATH);
#endif
    if(path.empty()&&!state.catalog)path=albert::assetDirectory()/"visuals.pak";
    std::vector<fs::path> paths;
    if(!path.empty())paths.push_back(path);
    else for(const auto& container:state.catalog->containers)
        for(const auto& directory:albert::assetContainerDirectories()){
            std::error_code error;const auto candidate=directory/container.file;
            if(fs::is_regular_file(candidate,error)&&fs::file_size(candidate,error)==container.bytes){paths.push_back(candidate);break;}
        }
    if(state.catalog&&(paths.empty()||paths.front().filename()!=state.catalog->containers.front().file))
        throw std::runtime_error("Dados visuais essenciais ainda nao baixados");
    state.pack=std::make_shared<const albert::AssetPack>(paths);state.paths=std::move(paths);
    state.complete=completeFor(state.catalog,state.paths);state.ready=true;
}
}

std::shared_ptr<const albert::AssetPack> albert::visualAssetPack() {
    auto& state=library();std::lock_guard lock(state.mutex);initialize(state);return state.pack;
}
std::uint64_t albert::visualAssetGeneration(){return generation.load();}
bool albert::visualAssetsComplete(){auto& state=library();std::lock_guard lock(state.mutex);initialize(state);return state.complete;}

void albert::installVisualAssetContainer(const fs::path& path) {
    auto& state=library();std::lock_guard serial(state.install);
    std::vector<fs::path> paths;std::optional<AssetCatalog> catalog;
    {std::lock_guard lock(state.mutex);if(state.ready){paths=state.paths;catalog=state.catalog;}else catalog=installedCatalog();}
    // Catalogue file names are unique; the same container is never indexed twice.
    if(std::any_of(paths.begin(),paths.end(),[&](const fs::path& p){return p.filename()==path.filename();}))return;
    paths.push_back(path);
    // Index outside the reader lock; a rejected container leaves the snapshot as it was.
    auto pack=std::make_shared<const AssetPack>(paths);
    {std::lock_guard lock(state.mutex);state.pack=std::move(pack);state.paths=std::move(paths);state.catalog=std::move(catalog);state.complete=completeFor(state.catalog,state.paths);state.ready=true;}
    ++generation;
}
