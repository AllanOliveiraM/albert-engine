#include "contact_shading.hpp"
#include "threepp/textures/DataTexture.hpp"
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
namespace {
struct Block {float x,y,bottom,top,w,d,sn,cs;bool round;};
std::vector<unsigned char> bake(const std::vector<Block>& blocks,unsigned size,float west,float south,float span){
    const float cell=span/size;std::vector<unsigned char> pixels(size*size*4,255);
    constexpr float heights[]{0,2,8,24};
    for(const auto& b:blocks){
        const float spread=std::clamp(std::min(b.w,b.d)*.6f,.45f,3.f),reach=spread*4;
        const float rx=std::abs(b.cs)*b.w+std::abs(b.sn)*b.d+reach,ry=std::abs(b.sn)*b.w+std::abs(b.cs)*b.d+reach;
        const int x0=std::max(0,int(std::floor((b.x-rx-west)/cell))),x1=std::min(int(size)-1,int(std::ceil((b.x+rx-west)/cell)));
        const int y0=std::max(0,int(std::floor((b.y-ry-south)/cell))),y1=std::min(int(size)-1,int(std::ceil((b.y+ry-south)/cell)));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            const float dx=west+(x+.5f)*cell-b.x,dy=south+(y+.5f)*cell-b.y;
            const float u=std::abs(dx*b.cs-dy*b.sn),v=std::abs(dx*b.sn+dy*b.cs);
            const float distance=b.round?(std::sqrt(u*u/(b.w*b.w)+v*v/(b.d*b.d))-1)*std::min(b.w,b.d):std::max(u-b.w,v-b.d);
            const float horizontal=std::max(distance,0.f),weight=.68f*std::exp(-horizontal/spread);
            for(unsigned c=0;c<4;++c){const float z=heights[c];if(z>=b.top-.05f)continue;
                // Interior samples must not paint the object's own upper surfaces.
                if(distance<-.25f&&z>b.bottom+.4f)continue;
                const float gap=std::max(b.bottom-z,0.f),radius=std::max(spread,std::min(b.w,b.d));
                const float visibility=1-weight/(1+gap*gap/(radius*radius));
                auto& dst=pixels[(y*size+x)*4+c];dst=std::min(dst,static_cast<unsigned char>(std::clamp(visibility,0.f,1.f)*255));}
        }
    }
    return pixels;
}
}
std::shared_ptr<ShaderMaterial> contactField(const core::Scenery& s,std::shared_ptr<DataTexture>& texture){
    std::vector<Block> blocks;
    auto add=[&](float x,float y,float bottom,float top,float w,float d,double heading,bool round=false){if(w>.05f&&d>.05f&&top>bottom+.2f)
        blocks.push_back({x,y,bottom,top,w/2,d/2,float(std::sin(heading)),float(std::cos(heading)),round});};
    for(const auto& p:s.parts){
        if(p.structure&&p.structure<=s.bars.size()&&s.bars[p.structure-1].hinged)continue;
        if(p.name.find("vent")!=std::string::npos||p.name.find("apron")!=std::string::npos||p.name.find("trench")!=std::string::npos||p.name.find("beacon")!=std::string::npos)continue;
        float x=p.centre.x,y=p.centre.y,z=p.centre.z,w=p.size.x,d=p.size.y,h=p.size.z;
        if(p.name=="launch tower lattice"){const float cs=std::cos(p.heading),sn=std::sin(p.heading);
            for(float a:{-1.f,1.f})for(float b:{-1.f,1.f})add(x+a*4.39f*cs+b*4.39f*sn,y-a*4.39f*sn+b*4.39f*cs,z-h/2,z+h/2,.72f,.72f,p.heading);
            continue;}
        if(p.shape==core::SceneryPart::Shape::Truss||std::abs(p.tilt)>.05||h<.4f)continue;
        if(p.name=="detail pickup"||p.name=="detail truck"||p.name=="detail car"){w=p.name=="detail truck"?2.5f:2.f;d=p.name=="detail truck"?7.f:5.f;h=1.8f;z+=h/2;}
        const bool farm=p.name.starts_with("farm ");if(farm)z+=h/2;
        if(p.name=="farm horizontal tank"){h=w+.6f;z=p.centre.z+h/2;}
        add(x,y,z-h/2,z+h/2,w,d,p.heading,p.shape==core::SceneryPart::Shape::Cylinder&&std::abs(w-d)<.1f);
    }
    for(const auto& b:s.buildings)add(b.centre.x,b.centre.y,b.centre.z,b.centre.z+b.size.z,b.size.x,b.size.y,b.heading,b.style==core::SceneryBuilding::Style::Tank);
    auto m=ShaderMaterial::create();m->name="albert:contact-field";
    // One atlas keeps the road pipeline within WebGPU's 16 fragment texture limit.
    auto near=bake(blocks,1024,-512,-384,1024),far=bake(blocks,512,-5632,-3072,6144);
    std::vector<unsigned char> pixels(1536*1024*4,255);
    for(unsigned y=0;y<1024;++y){std::copy_n(near.data()+y*1024*4,1024*4,pixels.data()+y*1536*4);
        if(y<512)std::copy_n(far.data()+y*512*4,512*4,pixels.data()+(y*1536+1024)*4);}
    texture=DataTexture::create(ImageData(std::move(pixels)),1536,1024);texture->minFilter=Filter::LinearMipmapLinear;texture->magFilter=Filter::Linear;texture->generateMipmaps=true;
    m->uniforms={{"contactMap",Uniform(static_cast<Texture*>(texture.get()))},{"strength",Uniform(1.f)}};return m;
}
}
