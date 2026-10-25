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
void text(ImDrawList* d,float size,ImVec2 at,ImU32 colour,const std::string& s,float align=0,float fade=1){ // align: 0 left, .5 centre, 1 right.
    ImFont* f=ImGui::GetFont();const ImVec2 w=f->CalcTextSizeA(size,1e9f,0,s.c_str());
    colour=(colour&~IM_COL32_A_MASK)|(static_cast<ImU32>((colour>>IM_COL32_A_SHIFT)*fade)<<IM_COL32_A_SHIFT);
    d->AddText(f,size,ImVec2(at.x-w.x*align+1,at.y+1),IM_COL32(0,0,0,static_cast<int>(140*fade)),s.c_str());d->AddText(f,size,ImVec2(at.x-w.x*align,at.y),colour,s.c_str());
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
void drawBroadcast(const std::vector<BroadcastVehicle>& vehicles,double missionTime,const std::vector<BroadcastEvent>& events,const BroadcastDecision& decision,double pace,std::optional<double> followedHeight,const BroadcastCount& count){
    ImDrawList* d=ImGui::GetBackgroundDrawList();const ImVec2 size=ImGui::GetIO().DisplaySize;const float k=std::clamp(size.x/1600.f,.7f,1.4f),bottom=size.y-18*k;
    if(!decision.what.empty()){
        const double age=missionTime-decision.time;const float u=static_cast<float>(std::clamp(age/.4,0.,1.)),entry=u*u*(3-2*u),fade=entry*static_cast<float>(1-.55*std::clamp((age-25.4)/1.5,0.,1.)); // In over 0.4 s, dimmed to 45% over 1.5 s after 25 s.
        const float wrap=420*k;
        const ImU32 white=IM_COL32(245,247,250,240),grey=IM_COL32(170,178,188,220);ImFont* f=ImGui::GetFont();
        std::string label=decision.vehicle;std::transform(label.begin(),label.end(),label.begin(),[](unsigned char ch){return static_cast<char>(std::toupper(ch));});
        const long long seconds=static_cast<long long>(std::abs(decision.time));char stamp[48];
        std::snprintf(stamp,sizeof stamp,"  T%c %02lld:%02lld",decision.time<0?'-':'+',seconds/60,seconds%60);label+=stamp;
        std::vector<std::string> lines;const char* p=decision.why.c_str(),*end=p+decision.why.size();
        while(p<end&&lines.size()<2){
            while(p<end&&(*p=='\r'||*p=='\n'))++p;if(p==end)break;
            const char* stop=f->CalcWordWrapPosition(12*k,p,end,wrap);
            stop=std::find(p,stop,'\n');
            if(stop==p){stop=p+1;while(stop<end&&(static_cast<unsigned char>(*stop)&0xc0)==0x80)++stop;}
            lines.emplace_back(p,stop);p=stop;while(p<end&&(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'))++p;
        }
        float width=std::max(f->CalcTextSizeA(11*k,1e9f,0,label.c_str()).x,f->CalcTextSizeA(15*k,1e9f,0,decision.what.c_str()).x);
        for(const auto& s:lines)width=std::max(width,f->CalcTextSizeA(12*k,1e9f,0,s.c_str()).x);
        const ImVec2 at((size.x-width)*.5f,24*k-12*k*(1-entry));const float height=lines.empty()?35*k:(42+14*static_cast<float>(lines.size()))*k; // Keep the panel clear of the inset.
        d->AddRectFilled(ImVec2(at.x-10*k,at.y-8*k),ImVec2(at.x+width+10*k,at.y+height+8*k),IM_COL32(0,0,0,static_cast<int>(110*fade)),6*k);
        text(d,11*k,at,grey,label,0,fade);text(d,15*k,ImVec2(at.x,at.y+18*k),white,decision.what,0,fade);
        for(std::size_t i=0;i<lines.size();++i)text(d,12*k,ImVec2(at.x,at.y+(42+14*static_cast<float>(i))*k),grey,lines[i],0,fade);
    }
    d->AddRectFilledMultiColor(ImVec2(0,size.y-210*k),size,IM_COL32(0,0,0,0),IM_COL32(0,0,0,0),IM_COL32(0,0,0,170),IM_COL32(0,0,0,170)); // Shade under the panels.
    for(const auto& v:vehicles)vehicle(d,v,v.right?size.x-360*k-28*k:28*k,bottom,k);
    // The clock and the events within five minutes of now.
    const float cx=size.x*.5f,line=bottom-92*k,half=std::min(330*k,size.x*.5f-410*k);
    if(half>80*k){d->AddLine(ImVec2(cx-half,line),ImVec2(cx,line),IM_COL32(200,206,214,170),1.5f*k);d->AddLine(ImVec2(cx,line),ImVec2(cx+half,line),IM_COL32(200,206,214,120),1.5f*k);
        std::vector<const BroadcastEvent*> shown;for(const auto& e:events)if(std::abs(e.time-missionTime)<300)shown.push_back(&e);
        std::sort(shown.begin(),shown.end(),[](const BroadcastEvent* a,const BroadcastEvent* b){return a->time<b->time;});
        float ends[2]{-1e9f,-1e9f}; // Where the last name above and below ends: a name goes where it fits, else only its dot.
        for(const auto* e:shown){const double age=missionTime-e->time;const float px=cx-static_cast<float>(age/300)*half,edge=std::clamp(static_cast<float>((300-std::abs(age))/45),0.f,1.f),fade=edge*edge*(3-2*edge),u=static_cast<float>(std::clamp(age/.6,0.,1.)),fill=u*u*(3-2*u);
            d->AddCircleFilled(ImVec2(px,line),5*k,IM_COL32(12,14,18,static_cast<int>(230*(1-fill)*fade)),16);d->AddCircle(ImVec2(px,line),5*k,IM_COL32(220,224,230,static_cast<int>(220*(1-fill)*fade)),16,1.5f*k);
            if(fill>0)d->AddCircleFilled(ImVec2(px,line),5*k*fill,IM_COL32(245,247,250,static_cast<int>(200*fade)),16);
            if(age>=0&&age<1){const float pulse=static_cast<float>(age);d->AddCircle(ImVec2(px,line),(5+14*pulse)*k,IM_COL32(245,247,250,static_cast<int>(70*std::sin(pulse*3.14159265f)*fade)),32,2*k);} // One pulse, from the event's age.
            const float w=ImGui::GetFont()->CalcTextSizeA(10.5f*k,1e9f,0,e->name.c_str()).x;
            for(int side=0;side<2;++side)if(px-w/2>ends[side]+8*k){ends[side]=px+w/2;
                text(d,10.5f*k,ImVec2(px,side==0?line-24*k:line+10*k),IM_COL32(static_cast<int>(235-65*fill),static_cast<int>(238-60*fill),static_cast<int>(242-54*fill),static_cast<int>(235-65*fill)),e->name,.5f,fade);break;}}
        d->AddTriangleFilled(ImVec2(cx-6*k,line-14*k),ImVec2(cx+6*k,line-14*k),ImVec2(cx,line-6*k),IM_COL32(245,247,250,230));} // Now.
    const double t=std::abs(missionTime);const long long s=static_cast<long long>(t);char b[48];
    std::snprintf(b,sizeof b,"T%c %02lld:%02lld:%02lld",missionTime<0?'-':'+',s/3600,s/60%60,s%60);
    ImFont* f=ImGui::GetFont();float digit=0,width=0;for(char ch='0';ch<='9';++ch)digit=std::max(digit,f->CalcTextSizeA(30*k,1e9f,0,&ch,&ch+1).x); // Fixed cells stop the clock wandering.
    for(const char* p=b;*p;++p)width+=*p>='0'&&*p<='9'?digit:f->CalcTextSizeA(30*k,1e9f,0,p,p+1).x;
    if(count.state=="HOLD"||count.state=="SCRUB"){
        const bool hold=count.state=="HOLD";const float glow=hold?static_cast<float>(.75+.25*std::sin(ImGui::GetTime()*6.28318530718)):1; // Wall time pulses even while the count stands.
        text(d,30*k,ImVec2(cx,bottom-50*k),hold?IM_COL32(static_cast<int>(255*glow),static_cast<int>(190*glow),static_cast<int>(60*glow),245):IM_COL32(235,65,65,245),count.state,.5f);
        const long long held=static_cast<long long>(std::abs(count.time));std::snprintf(b,sizeof b,"T%c%02lld:%02lld:%02lld",count.time<0?'-':'+',held/3600,held/60%60,held%60);
        text(d,12*k,ImVec2(cx,bottom-16*k),IM_COL32(170,178,188,220),b,.5f);
    }else{float x=cx-width/2;for(const char* p=b;*p;++p){const float w=f->CalcTextSizeA(30*k,1e9f,0,p,p+1).x,cell=*p>='0'&&*p<='9'?digit:w;char ch[2]{*p,0};text(d,30*k,ImVec2(x+(cell-w)/2,bottom-50*k),IM_COL32(245,247,250,245),ch);x+=cell;}}
    static float notice=0,held=0;static int lastFrame=-2;static double noticePace=1,noticeHeight=0;
    const int frame=ImGui::GetFrameCount();if(frame!=lastFrame+1)notice=held=0;lastFrame=frame; // A hidden broadcast must not retain a visible notice.
    // Shown once the clock has run over 3x for 1.5 s, gone once under 2x: a flight's measured pace wanders about 1x in real time.
    held=pace>3&&followedHeight?held+ImGui::GetIO().DeltaTime:pace<2||!followedHeight?0:held;const bool accelerated=held>1.5f||(notice>0&&pace>=2&&followedHeight);
    if(accelerated&&followedHeight){noticePace=pace;noticeHeight=*followedHeight;}
    notice=std::clamp(notice+(accelerated?1.f:-1.f)*ImGui::GetIO().DeltaTime/.4f,0.f,1.f); // Wall time keeps the fade steady while the mission clock races.
    if(notice>0){
        const float fade=notice*notice*(3-2*notice),wrap=std::min(600*k,std::max(1.f,size.x-96*k));
        const std::string title="Tempo acelerado  "+std::to_string(std::llround(noticePace))+"×",why=decision.what.rfind("planeio",0)==0?"Depois da queima de saída de órbita, a nave cai em direção ao ar, sem motores. O tempo corre mais rápido até a reentrada; a reentrada e o pouso acontecem em tempo real."
            :noticeHeight>100000?"A nave está em órbita: uma volta leva cerca de 90 minutos. O tempo corre mais rápido até a manobra de saída de órbita, mostrada em tempo real.":"A nave desce da órbita até o ar. A reentrada e o pouso acontecem em tempo real.";
        std::vector<std::string> lines;const char* p=why.c_str(),*end=p+why.size();
        while(p<end){const char* stop=f->CalcWordWrapPosition(15*k,p,end,wrap);if(stop==p){stop=p+1;while(stop<end&&(static_cast<unsigned char>(*stop)&0xc0)==0x80)++stop;}
            lines.emplace_back(p,stop);p=stop;while(p<end&&(*p==' '||*p=='\t'))++p;}
        const float height=(96+18*static_cast<float>(lines.size()))*k,top=(size.y-height)*.5f;
        d->AddRectFilled(ImVec2(cx-wrap*.5f-24*k,top),ImVec2(cx+wrap*.5f+24*k,top+height),IM_COL32(0,0,0,static_cast<int>(110*fade)),6*k);
        text(d,30*k,ImVec2(cx,top+24*k),IM_COL32(245,247,250,240),title,.5f,fade);
        for(std::size_t i=0;i<lines.size();++i)text(d,15*k,ImVec2(cx,top+(72+18*static_cast<float>(i))*k),IM_COL32(170,178,188,220),lines[i],.5f,fade);
    }
}
}
