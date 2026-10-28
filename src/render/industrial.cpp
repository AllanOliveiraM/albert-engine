#include "industrial.hpp"
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
bool industrialGeometry(IndustrialPieces& painted,IndustrialPieces& metal,IndustrialPieces& fine,const core::SceneryPart& p){
    if(!p.name.starts_with("farm "))return false;
    constexpr std::uint32_t steel=0xaab5bd,dark=0x434f57,black=0x26343c,concrete=0x99968e,frost=0xe5edef;
    const float w=float(p.size.x),d=float(p.size.y),h=float(p.size.z);
    auto add=[&](std::shared_ptr<BufferGeometry> g,Vector3 at,std::uint32_t colour,int group){g->translate(at.x,at.y,at.z);const Color c(colour);std::vector<float> colors;
        for(int i=0;i<g->getAttribute<float>("position")->count();++i)colors.insert(colors.end(),{c.r,c.g,c.b});
        g->setAttribute("color",FloatBufferAttribute::create(std::move(colors),3));(group==2?fine:group==1?metal:painted).push_back(std::move(g));};
    auto box=[&](Vector3 at,Vector3 size,std::uint32_t colour,int group=0){add(BoxGeometry::create(size.x,size.y,size.z),at,colour,group);};
    auto tube=[&](Vector3 a,Vector3 b,float diameter,std::uint32_t colour,int group=1){Vector3 v=Vector3(b).sub(a);const float length=v.length();if(length<.001f)return;
        auto g=CylinderGeometry::create(diameter/2,diameter/2,length,group==2?8:24);Quaternion q;q.setFromUnitVectors(Vector3(0,1,0),v.normalize());g->applyQuaternion(q);add(g,Vector3(a).add(b).multiplyScalar(.5f),colour,group);};
    auto beam=[&](Vector3 a,Vector3 b,float width,std::uint32_t colour,int group=1){Vector3 v=Vector3(b).sub(a);const float length=v.length();if(length<.001f)return;
        auto g=BoxGeometry::create(width,length,width);Quaternion q;q.setFromUnitVectors(Vector3(0,1,0),v.normalize());g->applyQuaternion(q);add(g,Vector3(a).add(b).multiplyScalar(.5f),colour,group);};
    auto cap=[&](Vector3 at,Vector3 scale,std::uint32_t colour,int group){auto g=SphereGeometry::create(1,24,12);g->scale(scale.x,scale.y,scale.z);add(g,at,colour,group);};
    auto ladder=[&](float x,float y,float bottom,float top){for(float side:{-1.f,1.f})tube({x+side*.28f,y,bottom},{x+side*.28f,y,top+.5f},.065f,steel,2);
        for(float z=bottom+.2f;z<top+.1f;z+=.3f)tube({x-.28f,y,z},{x+.28f,y,z},.045f,steel,2);};
    auto rail=[&](Vector3 a,Vector3 b){for(float z:{.55f,1.05f})tube(Vector3(a).add({0,0,z}),Vector3(b).add({0,0,z}),.045f,steel,2);
        const Vector3 v=Vector3(b).sub(a);const int bays=std::max(1,int(std::ceil(v.length()/3)));for(int i=0;i<=bays;++i){auto at=Vector3(a).add(Vector3(v).multiplyScalar(float(i)/bays));tube(at,Vector3(at).add({0,0,1.08f}),.06f,steel,2);}};
    auto valve=[&](float x,float y,float z,bool cold){box({x,y,z},{.38f,.45f,.4f},cold?frost:steel,2);tube({x,y,z},{x,y,z+.55f},.075f,steel,2);
        // A handwheel lies horizontally; its rim and spokes remain readable in oblique views.
        auto g=RingGeometry::create(.19f,.25f,12);g->translate(0,0,.015f);add(g,{x,y,z+.55f},0x5b777d,2);
        tube({x-.23f,y,z+.55f},{x+.23f,y,z+.55f},.035f,dark,2);tube({x,y-.23f,z+.55f},{x,y+.23f,z+.55f},.035f,dark,2);};
    if(p.name=="farm horizontal tank"){
        const float r=w/2,z=r+.6f,end=r*.48f,body=d-2*end;const bool insulated=p.color!=0xb9bdc0,cold=p.color==0xe4e4e2||p.color==0xb9bdc0;const int shell=insulated?0:1;
        tube({0,-body/2,z},{0,body/2,z},w,p.color,shell);
        for(float side:{-1.f,1.f}){cap({0,side*body/2,z},{r,end,r},p.color,shell);
            box({0,side*d*.3f,.36f},{w*.86f,1.8f,.72f},concrete);
            box({0,side*d*.3f,.77f},{w*.72f,1.15f,.45f},dark,1);
            // Steel saddle cheeks follow the lower shell, rather than floating below it.
            for(float x:{-.36f,.36f}){const float top=.65f+.31f*r;box({x*w,side*d*.3f,(top+.36f)/2},{.2f,1.12f,top-.36f},dark,1);}
            tube({0,side*(d/2-.05f),z},{0,side*(d/2+1.f),z},.32f,cold?frost:steel,1);
            tube({0,side*(d/2+.85f),z},{0,side*(d/2+.85f),1.05f},.26f,cold?frost:steel,1);
            valve(0,side*(d/2+.85f),1.2f,cold);
        }
        // Narrow bands and a catwalk: high quality only, with positive offsets from the shell.
        for(float y=-body/2+3;y<body/2;y+=5){auto g=CylinderGeometry::create(r+.025f,r+.025f,.09f,32,1,true);add(g,{0,y,z},insulated?0xc3cbce:0x89969f,2);}
        box({0,0,w+.76f},{.9f,body,.14f},dark,2);rail({-.5f,-body/2,w+.83f},{-.5f,body/2,w+.83f});rail({.5f,-body/2,w+.83f},{.5f,body/2,w+.83f});
        for(float y=-body/2+1;y<body/2;y+=6)box({0,y,w+.56f},{.13f,.13f,.4f},steel,2);
        const float landing=end+.5f;
        box({0,-body/2-landing/2,w+.76f},{.9f,landing,.14f},dark,2);
        for(float side:{-.5f,.5f})rail({side,-body/2-landing,w+.83f},{side,-body/2,w+.83f});
        ladder(0,-d/2-.58f,.4f,w+.85f);
        tube({0,body*.25f,w+.57f},{0,body*.25f,w+.76f},.7f,steel,2);
        tube({-r-.2f,-d/2-.85f,1.05f},{r+.2f,-d/2-.85f,1.05f},.22f,cold?frost:steel,1);
        tube({0,body/2-.5f,w+.5f},{0,body/2-.5f,w+1.6f},.16f,steel,2);
        tube({0,body/2-.5f,w+1.6f},{.4f,body/2-.5f,w+1.6f},.16f,steel,2);
    }else if(p.name=="farm vertical tank"){
        const float r=w/2,base=.4f,capHeight=.25f;const int shell=p.color==0xb9bdc0?1:0;
        box({0,0,.18f},{w+.6f,d+.6f,.36f},concrete);
        tube({0,0,base+capHeight},{0,0,h-capHeight},w,p.color,shell);
        cap({0,0,base+capHeight},{r,r,capHeight},p.color,shell);cap({0,0,h-capHeight},{r,r,capHeight},p.color,shell);
        for(float z:{1.1f,h-1.1f})tube({0,0,z-.045f},{0,0,z+.045f},w+.04f,steel,2);
        ladder(0,-r-.17f,.4f,h+.15f);
        box({0,-r-.65f,.8f},{1.2f,1.15f,.12f},dark,2);rail({-.6f,-r-1.2f,.86f},{.6f,-r-1.2f,.86f});
        for(int i=0;i<4;++i)box({.9f+i*.24f,-r-.65f,.1f+i*.2f},{.25f,1.05f,.16f},steel,2);
        tube({0,-r+.1f,1.3f},{0,-r-.8f,1.3f},.22f,frost,1);valve(0,-r-.7f,1.3f,true);
        tube({0,0,h},{0,0,h+.9f},.13f,steel,2);
    }else if(p.name=="farm vaporizer"){
        box({0,0,.12f},{w+.25f,d+.25f,.24f},concrete);
        for(float x:{-.35f,.35f})box({x*w,0,.42f},{.16f,d,.5f},dark,1);
        // Open aluminium extrusions, grouped into four separate fin banks. The lower inlet bank carries frost.
        for(int ix=0;ix<4;++ix)for(int iy=0;iy<3;++iy){const float x=-w*.37f+ix*w*.245f,y=-d*.34f+iy*d*.34f;
            tube({x,y,.65f},{x,y,h-.12f},.11f,steel,1);
            for(int fin=0;fin<3;++fin){const float offset=(fin-1)*.1f;
                box({x+offset,y,h/2+.25f},{.025f,.46f,h-.75f},steel,1);
                box({x,y+offset,h/2+.25f},{.46f,.025f,h-.75f},steel,1);
                box({x+offset,y,1.18f},{.04f,.48f,1.05f},frost,2);}}
        for(float z:{.7f,h-.3f})tube({-w*.4f,-d*.5f,z},{w*.4f,-d*.5f,z},.14f,z<1?frost:steel,1);
        valve(w*.35f,-d*.5f,.75f,true);
    }else if(p.name=="farm canopy"){
        // Repeated shallow gables across the rows; outer columns stand beyond both tank ends.
        const int bays=14;const float pitch=w/bays;
        for(int i=0;i<bays;++i){const float x=-w/2+(i+.5f)*pitch;
            for(float side:{-1.f,1.f}){auto g=BoxGeometry::create(pitch/2+.08f,d,.14f);g->rotateY(side*.085f);add(g,{x+side*pitch/4,0,h+.15f},side<0?p.color:0xb9c2c6,0);}
            box({x,0,h+.34f},{.18f,d,.08f},0x939fa8,2);
            for(float side:{-1.f,1.f})box({x+side*pitch*.25f,0,h+.25f},{.055f,d,.07f},0x9fabb2,2);
            beam({x-pitch/2,-d/2,h-.3f},{x+pitch/2,-d/2,h-.3f},.18f,dark);beam({x-pitch/2,d/2,h-.3f},{x+pitch/2,d/2,h-.3f},.18f,dark);
            for(float y=-d/2+6;y<d/2;y+=6)box({x,y,h-.12f},{pitch,.1f,.18f},steel,2);
        }
        for(int i=0;i<=bays;i+=2)for(float side:{-1.f,1.f}){const float x=-w/2+i*pitch,y=side*d/2;
            box({x,y,.18f},{.85f,.85f,.36f},concrete);box({x,y,h/2},{.22f,.22f,h},dark,1);
            beam({x,y,h-2},{x+((i==bays)?-1.f:1.f)*1.4f,y,h-.2f},.1f,steel,2);}
        for(float side:{-1.f,1.f}){box({0,side*d/2,h-.2f},{w,.18f,.45f},dark,1);tube({-w/2,side*d/2,.4f},{-w/2,side*d/2,h},.12f,steel,2);}
    }else if(p.name=="farm pipe trestle"){
        const int bays=std::max(1,int(std::ceil(d/8)));for(int i=0;i<=bays;++i){const float y=-d/2+d*float(i)/bays;
            for(float side:{-1.f,1.f}){box({side*(w/2-.25f),y,.2f},{.9f,.85f,.4f},concrete);box({side*(w/2-.25f),y,h/2},{.18f,.22f,h},dark,1);}
            box({0,y,h-.15f},{w,.22f,.3f},steel,1);
            beam({-w/2+.25f,y,.5f},{w/2-.25f,y,h-.3f},.085f,steel,2);
            for(int j=0;j<4;++j)box({-1.8f+j*1.2f,y,h+.03f},{.3f,.38f,.16f},dark,2);
        }
        for(float side:{-1.f,1.f})box({side*(w/2-.25f),0,h-.45f},{.15f,d,.18f},steel,1);
    }else if(p.name=="farm light pole"){
        box({0,0,.3f},{.85f,.85f,.6f},concrete);tube({0,0,.4f},{0,0,h},.16f,steel,1);
        beam({0,0,h},{1.4f,0,h},.1f,steel);box({1.35f,0,h-.12f},{.7f,.45f,.2f},dark,1);
        box({1.35f,0,h-.23f},{.6f,.35f,.035f},0xd6dfd7,2);
    }else if(p.name=="farm manifold"){
        box({0,0,.18f},{w,d,.36f},concrete);for(float x:{-w*.4f,w*.4f})box({x,0,.75f},{.18f,d,1.15f},dark,1);
        for(int i=0;i<4;++i){float x=-1.8f+i*1.2f;tube({x,-d/2,1.4f},{x,d/2,1.4f},i<2?.45f:.18f,i<2?frost:steel,1);valve(x,0,1.4f,i<2);}
        box({w*.35f,0,2.2f},{.65f,.8f,1.1f},0xbac1bf);box({w*.35f,-.41f,2.3f},{.4f,.04f,.3f},black,2);
    }else if(p.name.starts_with("farm building ")){
        const bool bunker=p.name.find("unker")!=std::string::npos,control=p.name.find("Control")!=std::string::npos||p.name.find("Security")!=std::string::npos;
        box({0,0,h/2},{w,d,h},p.color);box({0,0,.25f},{w+.35f,d+.35f,.5f},concrete);
        box({0,0,h+.12f},{w+.35f,d+.35f,.24f},bunker?0x97968e:0xc1c6c6);
        // Openings follow the long faces. Large dark recesses and jambs survive the low setting.
        const int doors=control?1:std::max(1,int(d/18));const float doorW=control?1.1f:std::min(3.6f,d/float(doors)*.55f),doorH=std::min(control?2.3f:3.8f,h-.55f);
        for(float side:{-1.f,1.f}){
            for(int i=0;i<doors;++i){const float y=control?-d/2+1.2f:-d/2+(i+.5f)*d/doors;
                box({side*(w/2+.025f),y,doorH/2+.15f},{.065f,doorW+.25f,doorH+.25f},black);
                box({side*(w/2+.065f),y,doorH/2+.15f},{.045f,doorW,doorH},0x78858c);
                for(float z=.45f;z<doorH;z+=.28f)box({side*(w/2+.095f),y,z},{.028f,doorW,.035f},dark,2);
                for(float sign:{-1.f,1.f})box({side*(w/2+.12f),y+sign*(doorW/2+.1f),doorH/2+.15f},{.15f,.13f,doorH+.35f},steel,2);
            }
            for(float y=-d/2+1.5f;y<d/2;y+=3.f){
                if(!bunker)box({side*(w/2+.045f),y,h/2},{.05f,.05f,h-.1f},0x78848a,2);
                if(control&&y>-d/2+2.5f){box({side*(w/2+.06f),y,h*.69f},{.1f,1.2f,.85f},steel);box({side*(w/2+.12f),y,h*.69f},{.035f,1.03f,.68f},0x344e60);}
            }
            // Louver banks on the short end, clear of the roll-up doors.
            box({side*w*.28f,d/2+.035f,h*.58f},{std::min(1.5f,w*.3f),.08f,1.2f},black);
            for(int i=0;i<7;++i)box({side*w*.28f,d/2+.1f,h*.58f-.5f+i*.16f},{std::min(1.5f,w*.3f),.14f,.065f},steel,2);
            if(bunker)for(float z=1.2f;z<h;z+=1.2f)box({side*(w/2+.018f),0,z},{.035f,d,.04f},0x76766f,2);
        }
        const int units=std::max(1,int(d/16));for(int i=0;i<units;++i){const float y=-d/2+(i+.5f)*d/units;
            box({0,y,h+.55f},{std::min(2.f,w*.45f),2.2f,.85f},0x8c999f);
            tube({0,y,h+.98f},{0,y,h+1.02f},.85f,dark,2);
            for(int j=0;j<4;++j)box({-.5f+j*.33f,y,h+1.055f},{.035f,.65f,.03f},steel,2);
        }
        for(float x=-w/2+1;x<w/2;x+=2.f)box({x,0,h+.26f},{.045f,d,.045f},0x909da4,2);
        ladder(-w/2-.3f,-d/2+.5f,.3f,h+.3f);
    }else return false;
    return true;
}
}
