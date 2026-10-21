#include "broadcast.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace albert {
namespace {
std::string grouped(long long v){ // 12345 as "12.345".
    std::string d=std::to_string(std::llabs(v)),out;for(std::size_t i=0;i<d.size();++i){if(i&&(d.size()-i)%3==0)out+='.';out+=d[i];}return v<0?"-"+out:out;
}
void text(ImDrawList* d,float size,ImVec2 at,ImU32 colour,const std::string& s,float align=0){ // align: 0 left, .5 centre, 1 right.
    ImFont* f=ImGui::GetFont();const ImVec2 w=f->CalcTextSizeA(size,1e9f,0,s.c_str());
    d->AddText(f,size,ImVec2(at.x-w.x*align+1,at.y+1),IM_COL32(0,0,0,140),s.c_str());d->AddText(f,size,ImVec2(at.x-w.x*align,at.y),colour,s.c_str());
}
void vehicle(ImDrawList* d,const BroadcastVehicle& v,float x,float bottom,float k){
    const ImU32 white=IM_COL32(245,247,250,240),grey=IM_COL32(170,178,188,220);
    // The engines from below, scaled to fit the ring.
    const ImVec2 c(x+62*k,bottom-78*k);const float ring=56*k;float reach=.1f;
    for(const auto& e:v.engines)reach=std::max(reach,std::hypot(e.y,e.z)+e.radius);
    const float s=(ring-6*k)/reach;d->AddCircleFilled(c,ring,IM_COL32(8,10,14,150),64);d->AddCircle(c,ring,IM_COL32(200,206,214,170),64,1.5f*k);
    for(const auto& e:v.engines){const ImVec2 p(c.x+e.y*s,c.y-e.z*s);const float r=std::max(e.radius*s,2.f);
        if(e.failed){d->AddCircleFilled(p,r,IM_COL32(220,50,50,235),20);continue;}
        if(e.on){const int a=static_cast<int>(150+105*std::clamp(e.throttle,0.f,1.f));d->AddCircleFilled(p,r,IM_COL32(255,255,255,a),20);}
        else{d->AddCircleFilled(p,r,IM_COL32(28,30,36,230),20);d->AddCircle(p,r,IM_COL32(120,126,136,200),20,1.f);}}
    // Name, speed and height.
    const float tx=x+138*k;std::string name=v.label;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char ch){return static_cast<char>(std::toupper(ch));});
    text(d,15*k,ImVec2(tx,bottom-142*k),white,name);
    const bool slow=v.speed<100,low=v.height<1000;char b[32];
    text(d,11*k,ImVec2(tx,bottom-116*k),grey,"VELOCIDADE");
    if(slow)std::snprintf(b,sizeof b,"%.1f",v.speed);const std::string speed=slow?std::string(b):grouped(std::llround(v.speed*3.6));
    text(d,30*k,ImVec2(tx,bottom-102*k),white,speed);text(d,12*k,ImVec2(tx+ImGui::GetFont()->CalcTextSizeA(30*k,1e9f,0,speed.c_str()).x+6*k,bottom-86*k),grey,slow?"M/S":"KM/H");
    text(d,11*k,ImVec2(tx,bottom-62*k),grey,"ALTITUDE");
    if(low)std::snprintf(b,sizeof b,"%.0f",v.height);else std::snprintf(b,sizeof b,"%.1f",v.height/1e3);const std::string height=b;
    text(d,30*k,ImVec2(tx,bottom-48*k),white,height);text(d,12*k,ImVec2(tx+ImGui::GetFont()->CalcTextSizeA(30*k,1e9f,0,height.c_str()).x+6*k,bottom-32*k),grey,low?"M":"KM");
    // The tanks.
    for(int i=0;i<2;++i){const float bx=x+300*k+i*22*k,top=bottom-136*k,h=96*k,f=static_cast<float>(std::clamp(v.propellant,0.,1.));
        d->AddRectFilled(ImVec2(bx,top),ImVec2(bx+7*k,top+h),IM_COL32(40,44,52,200),3*k);d->AddRectFilled(ImVec2(bx,top+h*(1-f)),ImVec2(bx+7*k,top+h),IM_COL32(235,238,242,230),3*k);
        text(d,10*k,ImVec2(bx+3.5f*k,top+h+6*k),grey,i?"CH4":"LOX",.5f);}
}
}
void drawBroadcast(const std::vector<BroadcastVehicle>& vehicles,double missionTime,const std::vector<BroadcastEvent>& events){
    ImDrawList* d=ImGui::GetBackgroundDrawList();const ImVec2 size=ImGui::GetIO().DisplaySize;const float k=std::clamp(size.x/1600.f,.7f,1.4f),bottom=size.y-18*k;
    d->AddRectFilledMultiColor(ImVec2(0,size.y-210*k),size,IM_COL32(0,0,0,0),IM_COL32(0,0,0,0),IM_COL32(0,0,0,170),IM_COL32(0,0,0,170)); // Shade under the panels.
    for(const auto& v:vehicles)vehicle(d,v,v.right?size.x-360*k-28*k:28*k,bottom,k);
    // The clock and the events within five minutes of now.
    const float cx=size.x*.5f,line=bottom-92*k,half=std::min(330*k,size.x*.5f-410*k);
    if(half>80*k){d->AddLine(ImVec2(cx-half,line),ImVec2(cx+half,line),IM_COL32(200,206,214,120),1.5f*k);
        std::vector<const BroadcastEvent*> shown;for(const auto& e:events)if(std::abs(e.time-missionTime)<300)shown.push_back(&e);
        std::sort(shown.begin(),shown.end(),[](const BroadcastEvent* a,const BroadcastEvent* b){return a->time<b->time;});
        float ends[2]{-1e9f,-1e9f}; // Where the last name above and below ends: a name goes where it fits, else only its dot.
        for(const auto* e:shown){const float px=cx+static_cast<float>((e->time-missionTime)/300)*half;const bool past=e->time<=missionTime;
            if(past)d->AddCircleFilled(ImVec2(px,line),5*k,IM_COL32(245,247,250,200),16);else{d->AddCircleFilled(ImVec2(px,line),5*k,IM_COL32(12,14,18,230),16);d->AddCircle(ImVec2(px,line),5*k,IM_COL32(220,224,230,220),16,1.5f*k);}
            const float w=ImGui::GetFont()->CalcTextSizeA(10.5f*k,1e9f,0,e->name.c_str()).x;
            for(int side=0;side<2;++side)if(px-w/2>ends[side]+8*k){ends[side]=px+w/2;
                text(d,10.5f*k,ImVec2(px,side==0?line-24*k:line+10*k),past?IM_COL32(170,178,188,170):IM_COL32(235,238,242,235),e->name,.5f);break;}}
        d->AddTriangleFilled(ImVec2(cx-6*k,line-14*k),ImVec2(cx+6*k,line-14*k),ImVec2(cx,line-6*k),IM_COL32(245,247,250,230));} // Now.
    const double t=std::abs(missionTime);const long long s=static_cast<long long>(t);char b[48];
    std::snprintf(b,sizeof b,"T%c %02lld:%02lld:%02lld",missionTime<0?'-':'+',s/3600,s/60%60,s%60);text(d,30*k,ImVec2(cx,bottom-50*k),IM_COL32(245,247,250,245),b,.5f);
}
}
