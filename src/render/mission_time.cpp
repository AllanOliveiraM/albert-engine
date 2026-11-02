#include "mission_time.hpp"
#include "menu.hpp"
#include <array>
#include <cstdio>

void albert::missions::drawTimeControls(Runner& runner,float width){
    static constexpr const char* phases[]{"Contagem regressiva","Subida","Separação","Pouso do booster","Órbita e retorno","Reentrada","Pouso da nave","Concluída"};
    const auto playback=runner.playback();const auto state=runner.status();
    ImGui::TextUnformatted("MISSÃO STARSHIP");ImGui::TextWrapped("Fase: %s",phases[static_cast<int>(playback.phase)]);
    ImGui::BeginDisabled(state.state!=State::Running);
    if(ImGui::Button(playback.speed==0?"Retomar voo":"Pausar voo",ImVec2(width,0)))runner.speed(playback.speed==0?1:0);
    constexpr std::array<double,6> speeds{.5,1,2,5,10,30};
    const float third=(width-2*ImGui::GetStyle().ItemSpacing.x)/3;
    for(std::size_t i=0;i<speeds.size();++i){if(i%3)ImGui::SameLine();char label[24];std::snprintf(label,sizeof label,speeds[i]>=1?"%.0f×":"%.1f×",speeds[i]);
        const bool active=playback.target<0&&playback.speed==speeds[i];if(active)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.09f,.29f,.36f,1));
        if(ImGui::Button(label,ImVec2(third,0)))runner.speed(speeds[i]);if(active)ImGui::PopStyleColor();}
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();ImGui::TextUnformatted("Avançar até");
    static int destination=1;
    if(destination<=static_cast<int>(playback.phase))destination=std::min(6,static_cast<int>(playback.phase)+1);
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##Marco da missão",phases[destination])){
        for(int i=1;i<=6;++i){ImGui::BeginDisabled(i<=static_cast<int>(playback.phase));if(ImGui::Selectable(phases[i],i==destination))destination=i;ImGui::EndDisabled();}ImGui::EndCombo();
    }
    ImGui::BeginDisabled(destination<=static_cast<int>(playback.phase));
    if(ImGui::Button("Avançar missão",ImVec2(width,0)))runner.advanceTo(static_cast<Phase>(destination));ImGui::EndDisabled();
    if(playback.target>=0){ImGui::TextWrapped("Avançando até: %s",phases[playback.target]);if(ImGui::Button("Cancelar avanço",ImVec2(width,0)))runner.speed(1);}
    ImGui::EndDisabled();
    menu::hint("O avanço calcula o voo até o marco escolhido e retorna a 1×. A espera orbital mantém a aceleração configurada na missão.");
}
