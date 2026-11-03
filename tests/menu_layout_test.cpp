#include "menu.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void rateEdit(double minimum,double maximum,const char* typed,double expected){
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={420,180};io.DeltaTime=1.f/60.f;
    io.ConfigInputTrickleEventQueue=false;
    io.Fonts->AddFontDefault();unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    double value=1.;ImVec2 center;
    for(int frame=0;frame<9;++frame){
        if(frame==1)io.AddMousePosEvent(center.x,center.y);
        if(frame==2){io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddMouseButtonEvent(0,true);}
        if(frame==3){io.AddMouseButtonEvent(0,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);}
        if(frame==4)io.AddInputCharactersUTF8(typed);
        if(frame==5)io.AddKeyEvent(ImGuiKey_Enter,true);
        if(frame==6)io.AddKeyEvent(ImGuiKey_Enter,false);
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({420,180});
        ImGui::Begin("Time",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetNextItemWidth(-1);
        albert::menu::rateSlider("##rate",value,minimum,maximum);
        center={(ImGui::GetItemRectMin().x+ImGui::GetItemRectMax().x)/2,(ImGui::GetItemRectMin().y+ImGui::GetItemRectMax().y)/2};
        require(std::isfinite(value)&&value>=minimum&&value<=maximum,"Typed slider value escaped the simulation's valid range");
        ImGui::End();ImGui::Render();
    }
    require(value==expected,"Ctrl+click did not apply a bounded time rate");
    ImGui::DestroyContext();
}
void layout(float width,float height,float scale){
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={width,height};io.DeltaTime=1.f/60.f;
    ImFontConfig config;config.SizePixels=16;io.Fonts->AddFontDefault(&config);
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    auto& style=ImGui::GetStyle();style.FramePadding={10,8};style.WindowPadding={20,20};style.ItemSpacing={8,10};
    io.FontGlobalScale=scale;
    bool value=false;ImVec2 label;
    for(int frame=0;frame<4;++frame){
        if(frame==2){io.AddMousePosEvent(label.x,label.y);io.AddMouseButtonEvent(0,true);}
        if(frame==3)io.AddMouseButtonEvent(0,false);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,height});
        ImGui::Begin("Menu",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar);
        const char* pages[]{"Cena","Dados","Fisica","Ajustes","Missões","Sobre"};int selected=3;
        albert::menu::pages(pages,selected);
        const auto* window=ImGui::GetCurrentWindow();
        require(window->DC.CursorMaxPos.x<=window->WorkRect.Max.x+1,"Page navigation exceeds the panel width");
        ImGui::Separator();
        ImGui::BeginChild("Conteudo",{0,0},0,ImGuiWindowFlags_AlwaysVerticalScrollbar);
        albert::menu::checkbox("Texto longo de uma configuracao que precisa continuar legivel em uma janela estreita",value);
        label={ImGui::GetItemRectMin().x+ImGui::GetFrameHeight()+style.ItemInnerSpacing.x+8,
               ImGui::GetItemRectMin().y+ImGui::GetFrameHeight()*.5f};
        require(ImGui::GetItemRectMax().x<=ImGui::GetCurrentWindow()->WorkRect.Max.x+1,"Checkbox label exceeds the scrollable area");
        if(width<=420.f)require(ImGui::GetItemRectSize().y>ImGui::GetFrameHeight(),"Long checkbox label did not wrap");
        int year=2026,month=11,day=3,hour=22,minute=59;
        albert::menu::utcFields(year,month,day,hour,minute);
        require(ImGui::GetCurrentWindow()->DC.CursorMaxPos.x<=ImGui::GetCurrentWindow()->WorkRect.Max.x+1,"UTC fields exceed the scrollable area");
        require(year==2026&&month==11&&day==3&&hour==22&&minute==59,"Layout changed the selected UTC date");
        for(int i=0;i<40;++i)albert::menu::hint("Outra opcao do menu.");
        if(frame>0)require(ImGui::GetScrollMaxY()>0,"Long content has no vertical scroll");
        ImGui::EndChild();ImGui::End();ImGui::Render();
    }
    require(value,"Clicking the wrapped label did not toggle its checkbox");
    ImGui::DestroyContext();
}
}
int main(){
    try{
        for(float width:{288.f,420.f,560.f})for(float scale:{.8f,1.f,1.6f})layout(width,600,scale);
        rateEdit(1.,31557600.,"999999999",31557600.);
        rateEdit(1.,31557600.,"-100",1.);
        rateEdit(.5,60.,"1000",60.);
        rateEdit(.5,60.,"0",.5);
        std::puts("Menu: wrapped navigation and labels, label clicks and scrolling verified at 9 sizes/scales");
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
