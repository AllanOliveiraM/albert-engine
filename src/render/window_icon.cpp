#include "window_icon.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
void albert::setWindowIcon(void* window,const std::filesystem::path& path){
    const auto source=threepp::ImageLoader().load(path,4,false);
    if(!source||!source->width()||!source->height()||!window)return;
    // Several small native icons avoid oversized X11 property requests, while
    // giving title bars and HiDPI docks a suitably sized, sharp image.
    constexpr std::array<int,3> sizes{32,64,128};
    std::array<std::vector<unsigned char>,3> buffers;
    std::array<GLFWimage,3> icons;
    const auto& pixels=source->data();
    for(size_t i=0;i<sizes.size();++i){
        const int size=sizes[i];auto& output=buffers[i];output.resize(size*size*4);
        for(int y=0;y<size;++y)for(int x=0;x<size;++x){
            const unsigned x0=unsigned(x)*source->width()/size,x1=std::max(x0+1,unsigned(x+1)*source->width()/size);
            const unsigned y0=unsigned(y)*source->height()/size,y1=std::max(y0+1,unsigned(y+1)*source->height()/size);
            double rgb[3]{},alpha=0;unsigned count=0;
            for(unsigned row=y0;row<std::min(y1,source->height());++row)for(unsigned col=x0;col<std::min(x1,source->width());++col){
                const auto at=(row*source->width()+col)*4;const double a=pixels[at+3];alpha+=a;++count;
                for(int c=0;c<3;++c)rgb[c]+=pixels[at+c]*a;
            }
            const auto at=(y*size+x)*4;
            for(int c=0;c<3;++c)output[at+c]=static_cast<unsigned char>(alpha>0?std::round(rgb[c]/alpha):0);
            output[at+3]=static_cast<unsigned char>(count?std::round(alpha/count):0);
        }
        icons[i]={size,size,output.data()};
    }
    glfwSetWindowIcon(static_cast<GLFWwindow*>(window),int(icons.size()),icons.data());
}
