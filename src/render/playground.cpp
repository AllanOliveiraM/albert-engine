#include "playground.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
namespace albert {
namespace {
namespace api=::albert::v1;
using Clock=std::chrono::steady_clock;
// A joystick: a disc the knob is dragged in (north up, east right), its value up to `reach`; true while it changed.
bool joystick(const char* id,float& north,float& east,float reach,float size){
    ImDrawList* d=ImGui::GetWindowDrawList();const ImVec2 p=ImGui::GetCursorScreenPos(),c(p.x+size/2,p.y+size/2);const float r=size/2-4;
    ImGui::InvisibleButton(id,ImVec2(size,size));bool changed=false;
    if(ImGui::IsItemActive()){const ImVec2 m=ImGui::GetIO().MousePos;float dx=(m.x-c.x)/r,dy=(c.y-m.y)/r;const float n=std::hypot(dx,dy);if(n>1){dx/=n;dy/=n;}
        const float e=dx*reach,no=dy*reach;changed=e!=east||no!=north;east=e;north=no;}
    d->AddCircleFilled(c,r,IM_COL32(20,24,30,200),48);d->AddCircle(c,r,IM_COL32(170,178,188,200),48,1.5f);
    d->AddLine(ImVec2(c.x-r,c.y),ImVec2(c.x+r,c.y),IM_COL32(90,96,104,160));d->AddLine(ImVec2(c.x,c.y-r),ImVec2(c.x,c.y+r),IM_COL32(90,96,104,160));
    d->AddText(ImVec2(c.x-4,p.y+2),IM_COL32(170,178,188,220),"N");
    const ImVec2 k(c.x+east/reach*r,c.y-north/reach*r);d->AddLine(c,k,IM_COL32(245,180,60,220),2);d->AddCircleFilled(k,6,IM_COL32(245,180,60,255),16);
    return changed;
}
api::FlightCommand vector(api::Vector3* (api::FlightCommand::*field)(),float n,float e,float down){
    api::FlightCommand c;auto* v=(c.*field)();v->set_x(n);v->set_y(e);v->set_z(down);return c;
}
struct SensorRow {const char* name;api::SensorFault::Sensor sensor;unsigned unit;const char* unit_text;bool vector;int mode=0;float value[3]{};};
struct State {
    int flight=0;float gust[3]{},push[3]{};Clock::time_point sent{};bool gustDirty=false,pushDirty=false;
    std::array<SensorRow,10> sensors{{{"IMU 1 acelerômetro",api::SensorFault::ACCELEROMETER,0,"m/s²",true},{"IMU 1 giroscópio",api::SensorFault::GYROSCOPE,0,"rad/s",true},
        {"IMU 2 acelerômetro",api::SensorFault::ACCELEROMETER,1,"m/s²",true},{"IMU 2 giroscópio",api::SensorFault::GYROSCOPE,1,"rad/s",true},
        {"IMU 3 acelerômetro",api::SensorFault::ACCELEROMETER,2,"m/s²",true},{"IMU 3 giroscópio",api::SensorFault::GYROSCOPE,2,"rad/s",true},
        {"GNSS",api::SensorFault::GNSS,0,"m (N, L, B)",true},{"Altímetro radar",api::SensorFault::RADAR_ALTIMETER,0,"m",false},
        {"Rastreador de estrelas",api::SensorFault::STAR_TRACKER,0,"rad (corpo)",true},{"Medidor de propelante",api::SensorFault::PROPELLANT_GAUGE,0,"kg",false}}};
    char message[65]{};Clock::time_point armed{};std::string notice;Clock::time_point noticeAt{};int click=0; // click: what a click on an engine does.
};
State& state(){static State s;return s;}
// The engine map from below, as the broadcast draws it: a click does what is chosen above it (fail, fail silently, jam its
// gimbal), a right click fails it silently.
void engineMap(rpc::Server& server,const std::string& id,const rpc::FlightServices::Gauge& g,float size,State& st){
    ImDrawList* d=ImGui::GetWindowDrawList();const ImVec2 p=ImGui::GetCursorScreenPos(),c(p.x+size/2,p.y+size/2);float reach=.1f;
    for(const auto& e:g.engines)reach=std::max(reach,std::hypot(e.y,e.z)+e.radius);
    const float k=(size/2-6)/reach;ImGui::InvisibleButton("##motores",ImVec2(size,size));const bool hover=ImGui::IsItemHovered();const ImVec2 m=ImGui::GetIO().MousePos;
    d->AddCircleFilled(c,size/2,IM_COL32(8,10,14,170),48);int over=-1;
    for(std::size_t i=0;i<g.engines.size();++i){const auto& e=g.engines[i];const ImVec2 q(c.x+e.y*k,c.y-e.z*k);const float r=std::max(e.radius*k,3.f);
        if(hover&&std::hypot(m.x-q.x,m.y-q.y)<=r+1)over=static_cast<int>(i);
        // Failed red, failed silently (claiming to be fine) violet, lit white by its throttle, else dark; its gimbal jammed: a thick
        // amber ring (it may still burn).
        const ImU32 fill=e.failed?(e.silent?IM_COL32(170,80,230,235):IM_COL32(220,50,50,235)):e.on?IM_COL32(255,255,255,static_cast<int>(150+105*std::clamp(e.throttle,0.f,1.f))):IM_COL32(28,30,36,230);
        d->AddCircleFilled(q,r,fill,20);
        if(e.stuck)d->AddCircle(q,r+1.5f,IM_COL32(255,190,40,255),20,2.5f);
        d->AddCircle(q,r,over==static_cast<int>(i)?IM_COL32(245,180,60,255):IM_COL32(120,126,136,200),20,over==static_cast<int>(i)?2.f:1.f);}
    { // The key to its colours.
        const ImVec2 k(p.x,p.y+size+4);const float h=ImGui::GetTextLineHeight(),rr=h*.3f;float x=k.x;
        auto key=[&](ImU32 c,const char* t,bool ring){const ImVec2 o(x+rr,k.y+h*.5f);if(ring)d->AddCircle(o,rr,c,12,2.f);else d->AddCircleFilled(o,rr,c,12);d->AddText(ImVec2(x+2*rr+3,k.y),IM_COL32(200,204,210,255),t);x+=2*rr+6+ImGui::CalcTextSize(t).x+10;};
        key(IM_COL32(220,50,50,235),"falha",false);key(IM_COL32(170,80,230,235),"calado",false);key(IM_COL32(255,190,40,255),"gimbal travado",true);
        ImGui::Dummy(ImVec2(size,h+6));}
    static const char* actions[]{"falha","falha calado (diz que está bem)","gimbal travado"};
    if(over>=0){ImGui::SetTooltip("Motor %d: clique: %s; botão direito: falha calado",over+1,actions[st.click]);
        const bool left=ImGui::IsMouseClicked(ImGuiMouseButton_Left),right=ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if(left||right){api::FlightCommand c;const int a=right?1:st.click;if(a==1)c.set_fail_silent(true);else if(a==2)c.set_stick_gimbal(true);else c.set_fail(true);c.set_index(static_cast<unsigned>(over+1));
            try{server.inject(id,c);st.notice="Motor "+std::to_string(over+1)+": "+actions[a];}catch(const std::exception& x){st.notice=x.what();}st.noticeAt=Clock::now();}}
}
}
void drawPlayground(rpc::Server& server,ImVec2 display,float margin){
    const auto ids=server.flights();if(ids.empty())return;
    State& st=state();st.flight=std::clamp(st.flight,0,static_cast<int>(ids.size())-1);const std::string id=ids[static_cast<std::size_t>(st.flight)];
    ImGui::SetNextWindowPos(ImVec2(display.x-margin,display.y*.18f),ImGuiCond_FirstUseEver,ImVec2(1,0));ImGui::SetNextWindowSize(ImVec2(360,0),ImGuiCond_FirstUseEver);ImGui::SetNextWindowBgAlpha(.8f);
    if(!ImGui::Begin("Interferência",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing)){ImGui::End();return;}
    auto send=[&](const api::FlightCommand& c,const std::string& what){try{server.inject(id,c);if(!what.empty()){st.notice=what;st.noticeAt=Clock::now();}}catch(const std::exception& x){st.notice=x.what();st.noticeAt=Clock::now();}};
    // The flight.
    {std::string items;for(const auto& f:ids){items+=f;items.push_back('\0');}ImGui::SetNextItemWidth(-1);ImGui::Combo("##voo",&st.flight,items.c_str());}
    if(ImGui::CollapsingHeader("Vento e forças",ImGuiTreeNodeFlags_DefaultOpen)){
        constexpr float size=120,gustReach=25,pushReach=600; // m/s; kN.
        ImGui::BeginGroup();ImGui::TextDisabled("Rajada (m/s)");if(joystick("##rajada",st.gust[0],st.gust[1],gustReach,size))st.gustDirty=true;
        ImGui::SetNextItemWidth(size);if(ImGui::SliderFloat("##rajada_v",&st.gust[2],-10,10,"vertical %.1f"))st.gustDirty=true; // NED down positive.
        if(ImGui::Button("Zerar##rajada")){st.gust[0]=st.gust[1]=st.gust[2]=0;st.gustDirty=true;}
        ImGui::Text("%.1f m/s de %.0f°",std::hypot(st.gust[0],st.gust[1]),std::fmod(std::atan2(-st.gust[1],-st.gust[0])*57.29578f+360,360.f));ImGui::EndGroup();ImGui::SameLine();
        ImGui::BeginGroup();ImGui::TextDisabled("Empurrão (kN)");if(joystick("##empurrao",st.push[0],st.push[1],pushReach,size))st.pushDirty=true;
        ImGui::SetNextItemWidth(size);if(ImGui::SliderFloat("##empurrao_v",&st.push[2],-pushReach,pushReach,"vertical %.0f"))st.pushDirty=true;
        if(ImGui::Button("Zerar##empurrao")){st.push[0]=st.push[1]=st.push[2]=0;st.pushDirty=true;}
        ImGui::Text("%.0f kN",std::hypot(st.push[0],st.push[1],st.push[2]));ImGui::EndGroup();
        if((st.gustDirty||st.pushDirty)&&Clock::now()-st.sent>std::chrono::milliseconds(150)){st.sent=Clock::now(); // At most ~7 a second while dragging.
            if(st.gustDirty)send(vector(&api::FlightCommand::mutable_gust_ned_meters_per_second,st.gust[0],st.gust[1],st.gust[2]),"");
            if(st.pushDirty)send(vector(&api::FlightCommand::mutable_force_ned_newtons,st.push[0]*1e3f,st.push[1]*1e3f,st.push[2]*1e3f),"");
            st.gustDirty=st.pushDirty=false;}
    }
    if(ImGui::CollapsingHeader("Motores",ImGuiTreeNodeFlags_DefaultOpen)){
        const auto gauges=server.flightGauges();const std::string scene=id=="flight"?id:"flight."+id;bool drawn=false;
        ImGui::RadioButton("Falha",&st.click,0);ImGui::SameLine();ImGui::RadioButton("Calada",&st.click,1);ImGui::SameLine();ImGui::RadioButton("Gimbal travado",&st.click,2);
        for(const auto& g:gauges)if(g.id==scene&&!g.engines.empty()){engineMap(server,id,g,170,st);drawn=true;break;}
        if(!drawn)ImGui::TextDisabled("Sem motores neste estágio.");
        for(const auto& g:gauges)if(g.id==scene&&!g.surfaces.empty()){ImGui::TextDisabled("Superfícies (travar onde estão):"); // Grid fins, flaps.
            for(std::size_t i=0;i<g.surfaces.size();++i){ImGui::PushID(static_cast<int>(i));if(i%2)ImGui::SameLine();
                if(ImGui::SmallButton((std::to_string(i+1)+" "+g.surfaces[i]).c_str())){api::FlightCommand c;c.set_stick_surface(true);c.set_index(static_cast<unsigned>(i+1));send(c,"Superfície "+std::to_string(i+1)+" travada");}
                ImGui::PopID();}break;}
    }
    if(ImGui::CollapsingHeader("Sensores")){
        static const char* modes="honesto\0morto\0travado\0viés\0deriva\0ruído\0";
        for(auto& r:st.sensors){ImGui::PushID(r.name);ImGui::TextUnformatted(r.name);ImGui::SameLine(170);ImGui::SetNextItemWidth(-1);ImGui::Combo("##modo",&r.mode,modes);
            if(r.mode>=3){ImGui::SetNextItemWidth(-60);if(r.vector)ImGui::InputFloat3("##valor",r.value,"%.4g");else ImGui::InputFloat("##valor",r.value,0,0,"%.4g");
                ImGui::SameLine();ImGui::TextDisabled("%s",r.unit_text);}
            if(ImGui::SmallButton("Aplicar")){api::FlightCommand c;auto* f=c.mutable_sensor_fault();f->set_sensor(r.sensor);f->set_unit(r.unit);f->set_mode(static_cast<api::SensorFault::Mode>(r.mode));
                f->mutable_value()->set_x(r.value[0]);f->mutable_value()->set_y(r.vector?r.value[1]:0);f->mutable_value()->set_z(r.vector?r.value[2]:0);
                const char* names[]{"honesto","morto","travado","com viés","derivando","com ruído"};send(c,std::string(r.name)+" "+names[r.mode]);}
            ImGui::Separator();ImGui::PopID();}
    }
    if(ImGui::CollapsingHeader("Operador",ImGuiTreeNodeFlags_DefaultOpen)){
        const float half=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)/2;
        if(ImGui::Button("SEGURAR (HOLD)",ImVec2(half,0))){api::FlightCommand c;c.set_uplink("HOLD");send(c,"Espera pedida");}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Segura a contagem regressiva (na ignição, aborta na plataforma).");ImGui::SameLine();
        if(ImGui::Button("RETOMAR (GO)",ImVec2(half,0))){api::FlightCommand c;c.set_uplink("GO");send(c,"Contagem retomada");}
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.75f,.45f,.08f,1));
        if(ImGui::Button("ABORTAR",ImVec2(-1,0))){api::FlightCommand c;c.set_uplink("ABORT");send(c,"Aborto enviado ao voo");}ImGui::PopStyleColor();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("O software de voo toma a saída mais segura para a sua fase (na contagem, cancela; na ignição, aborta na plataforma).");
        ImGui::SetNextItemWidth(-70);ImGui::InputText("##mensagem",st.message,sizeof st.message);ImGui::SameLine();
        if(ImGui::Button("Enviar")&&st.message[0]){api::FlightCommand c;c.set_uplink(st.message);send(c,"Mensagem enviada");st.message[0]=0;}
        const bool armed=Clock::now()-st.armed<std::chrono::seconds(3);
        ImGui::PushStyleColor(ImGuiCol_Button,armed?ImVec4(.85f,.12f,.12f,1):ImVec4(.35f,.08f,.08f,1));
        if(ImGui::Button(armed?"Confirmar: terminar o voo":"Terminação do voo (FTS)",ImVec2(-1,0))){
            if(armed){api::FlightCommand c;c.set_terminate(true);send(c,"Voo terminado");st.armed={};}else st.armed=Clock::now();}
        ImGui::PopStyleColor();
    }
    if(!st.notice.empty()&&Clock::now()-st.noticeAt<std::chrono::seconds(4))ImGui::TextDisabled("%s",st.notice.c_str());
    ImGui::End();
}
}
