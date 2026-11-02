#include "cloud_weather.hpp"
#include "platform.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

threepp::Image albert::simulatedCloudImage(unsigned width,unsigned height){
    if(width<4||height<2||width>8192||height>4096)throw std::invalid_argument("Invalid cloud weather dimensions");
    auto path=assetDirectory()/"clouds/structure.png";
#ifdef ALBERT_DEVELOPMENT_CLOUD_PATH
    if(!std::filesystem::exists(path))path=ALBERT_DEVELOPMENT_CLOUD_PATH;
#endif
    threepp::ImageLoader loader;auto reference=loader.load(path.string(),4,true);
    if(!reference)throw std::runtime_error("Referência visual de nuvens indisponível: "+path.string());
    if(reference->width()==width&&reference->height()==height)return std::move(*reference);
    const auto& source=reference->data();const int w=static_cast<int>(reference->width()),h=static_cast<int>(reference->height());
    std::vector<unsigned char> rgba(static_cast<std::size_t>(width)*height*4);
    auto at=[&](int x,int y){return source[(static_cast<std::size_t>(std::clamp(y,0,h-1))*w+(x%w+w)%w)*4];};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const double u=(x+.5)*w/width-.5,v=(y+.5)*h/height-.5;const int a=static_cast<int>(std::floor(u)),b=static_cast<int>(std::floor(v));
        const auto d=static_cast<unsigned char>(std::lround(std::lerp(std::lerp(double(at(a,b)),double(at(a+1,b)),u-a),std::lerp(double(at(a,b+1)),double(at(a+1,b+1)),u-a),v-b)));
        const auto i=(static_cast<std::size_t>(y)*width+x)*4;rgba[i]=rgba[i+1]=rgba[i+2]=d;rgba[i+3]=255;
    }
    return threepp::Image(std::move(rgba),width,height);
}
