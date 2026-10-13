#include "wms.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "stb_image_write.h"
#include <vector>
#include <stdexcept>

std::string albert::fetchWms(const WmsURL& url,const GeoRegion& region,unsigned width,unsigned height,DownloadProgress& progress) {
    GeoRegion normalized=region;
    normalized.west=longitude(region.west);normalized.east=normalized.west+(region.east-region.west);
    if(normalized.east<=180.) return httpGet(url(normalized,width,height),&progress);
    const unsigned left=std::clamp(static_cast<unsigned>(std::lround(width*(180.-normalized.west)/(normalized.east-normalized.west))),1u,width-1);
    const GeoRegion bounds[]={ {normalized.west,region.south,180.,region.north}, {-180.,region.south,normalized.east-360.,region.north} };
    std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);
    for(unsigned part=0;part<2;++part) {
        const unsigned columns=part ? width-left : left;
        const auto bytes=httpGet(url(bounds[part],columns,height),&progress);
        auto image=threepp::ImageLoader().load(std::vector<unsigned char>(bytes.begin(),bytes.end()),4,false);
        if(!image || image->width()!=columns || image->height()!=height) throw std::runtime_error("Recorte WMS invalido");
        for(unsigned y=0;y<height;++y) std::copy_n(image->data().data()+y*columns*4,columns*4,pixels.data()+(static_cast<size_t>(y)*width+(part ? left : 0))*4);
    }
    struct Encoded {std::string bytes;bool failed=false;} encoded;
    const auto write=[](void* context,void* data,int size) noexcept {
        auto& output=*static_cast<Encoded*>(context);
        try {output.bytes.append(static_cast<const char*>(data),size);}
        catch(...) {output.failed=true;}
    };
    progress.checkCancelled();
    if(!stbi_write_png_to_func(write,&encoded,width,height,4,pixels.data(),width*4) || encoded.failed) throw std::runtime_error("Recorte WMS nao pode ser combinado");
    return std::move(encoded.bytes);
}
