#include "wms.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include <stdexcept>
#include <sstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2) return 1;
    albert::DownloadProgress progress;
    auto url=[&](const albert::GeoRegion& r,unsigned w,unsigned h) {
        std::ostringstream out;out<<argv[1]<<"?west="<<r.west<<"&east="<<r.east<<"&width="<<w<<"&height="<<h;return out.str();
    };
    for(const auto region:{albert::GeoRegion{170,-10,190,10},albert::GeoRegion{-190,-10,-170,10}}) {
        auto bytes=albert::fetchWms(url,region,64,32,progress);
        auto image=threepp::ImageLoader().load(std::vector<unsigned char>(bytes.begin(),bytes.end()),4,false);
        if(!image || image->width()!=64 || image->height()!=32) throw std::runtime_error("Wrapped image dimensions");
        for(unsigned y:{0u,31u}) {
            const auto& p=image->data();
            if(p[4*(y*64)]!=255 || p[4*(y*64+63)+2]!=255) throw std::runtime_error("Antimeridian pixels lost or reversed");
        }
    }
    std::cout<<"WMS antimeridian coverage and stitching OK\n";
}
