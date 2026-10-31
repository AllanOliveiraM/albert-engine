#pragma once
#include <imgui.h>
#include <algorithm>
#include <span>

namespace albert::menu {
// Preserve readable labels when a narrow window or a large font needs another row.
inline void pages(std::span<const char* const> labels,int& selected) {
    const float available=ImGui::GetContentRegionAvail().x;
    const auto& style=ImGui::GetStyle();
    float used=0;
    for(std::size_t i=0;i<labels.size();++i){
        const float width=std::min(available,ImGui::CalcTextSize(labels[i]).x+style.FramePadding.x*2);
        if(used>0&&used+style.ItemSpacing.x+width<=available){ImGui::SameLine();used+=style.ItemSpacing.x;}
        else used=0;
        const bool active=selected==static_cast<int>(i);
        if(active)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.09f,.29f,.36f,1));
        if(ImGui::Button(labels[i],ImVec2(width,ImGui::GetFrameHeight())))selected=static_cast<int>(i);
        if(active)ImGui::PopStyleColor();
        used+=width;
    }
}
inline void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s",text);
    ImGui::PopStyleColor();
}
inline bool checkbox(const char* label,bool& value) {
    ImGui::PushID(label);
    ImGui::BeginGroup();
    bool changed=ImGui::Checkbox("##value",&value);
    ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s",label);
    if(ImGui::IsItemClicked()){value=!value;changed=true;}
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}
inline bool section(const char* label,bool open=false) {
    ImGui::Spacing();
    return ImGui::CollapsingHeader(label,open?ImGuiTreeNodeFlags_DefaultOpen:0);
}
inline void repositoryLink(){
    const float size=ImGui::GetFrameHeight();const auto at=ImGui::GetCursorScreenPos();
    auto* draw=ImGui::GetWindowDrawList();const auto colour=ImGui::GetColorU32(ImGuiCol_CheckMark);
    const float unit=size/24.f;
    auto point=[&](float x,float y){return ImVec2(at.x+x*unit,at.y+y*unit);};
    // A repository book, drawn as vectors so it stays sharp at every text scale.
    draw->AddRect(point(4,3),point(20,21),colour,2*unit,0,1.5f*unit);
    draw->AddLine(point(8,3),point(8,17),colour,1.5f*unit);
    draw->AddLine(point(4,17),point(20,17),colour,1.5f*unit);
    draw->AddLine(point(11,8),point(17,8),colour,1.5f*unit);
    draw->AddLine(point(11,11),point(15,11),colour,1.5f*unit);
    ImGui::Dummy(ImVec2(size,size));ImGui::SameLine();ImGui::AlignTextToFramePadding();
    ImGui::TextLinkOpenURL("GitHub","https://github.com/AllanOliveiraM/albert-engine");
    hint("AllanOliveiraM/albert-engine");
}
}
