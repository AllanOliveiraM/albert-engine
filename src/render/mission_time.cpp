#include "mission_time.hpp"
#include "menu.hpp"

void albert::missions::drawTimeControls(Runner& runner,float width){
    static constexpr const char* phases[]{"Contagem regressiva","Subida","Separação","Pouso do booster","Órbita e retorno","Reentrada","Pouso da nave","Concluída"};
    const auto playback=runner.playback();const auto state=runner.status();
    ImGui::TextUnformatted("MISSÃO STARSHIP");ImGui::TextWrapped("Fase: %s",phases[static_cast<int>(playback.phase)]);
    ImGui::BeginDisabled(state.state!=State::Running);
    const auto& style=ImGui::GetStyle();
    const float needed=2*(std::max(ImGui::CalcTextSize("Retomar voo").x,ImGui::CalcTextSize("Tempo normal").x)+2*style.FramePadding.x)+style.ItemSpacing.x;
    const bool beside=width>=needed;
    const float buttonWidth=beside?(width-style.ItemSpacing.x)/2:width;
    if(ImGui::Button(playback.speed==0?"Retomar voo":"Pausar voo",ImVec2(buttonWidth,0))){if(playback.speed==0)runner.resume();else runner.speed(0);}
    if(beside)ImGui::SameLine();
    if(ImGui::Button("Tempo normal",ImVec2(buttonWidth,0)))runner.speed(1);
    ImGui::Spacing();ImGui::TextDisabled("VELOCIDADE DO VOO");
    double speed=playback.speed>0?playback.speed:playback.resumeSpeed,minimum=.5,maximum=Runner::maximumSpeed;
    ImGui::SetNextItemWidth(-1);
    if(menu::rateSlider("##VelocidadeDoVoo",speed,minimum,maximum,"%.2g×"))runner.speed(speed);
    if(playback.speed==0)menu::hint("Pausado. Retomar conserva a velocidade escolhida.");
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();ImGui::TextUnformatted("Avançar até");
    static int destination=1;
    if(destination<=static_cast<int>(playback.phase))destination=std::min(6,static_cast<int>(playback.phase)+1);
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##Marco da missão",phases[destination])){
        for(int i=1;i<=6;++i){ImGui::BeginDisabled(i<=static_cast<int>(playback.phase));if(ImGui::Selectable(phases[i],i==destination))destination=i;ImGui::EndDisabled();}ImGui::EndCombo();
    }
    if(destination==3)menu::hint("Até o início da queima de pouso do booster.");
    if(destination==6)menu::hint("Até a manobra de giro para o pouso da nave.");
    ImGui::BeginDisabled(destination<=static_cast<int>(playback.phase));
    if(ImGui::Button("Avançar missão",ImVec2(width,0)))runner.advanceTo(static_cast<Phase>(destination));
    ImGui::EndDisabled();
    if(playback.target>=0){ImGui::TextWrapped("Avançando até: %s",phases[playback.target]);menu::hint("Pausar ou escolher uma velocidade cancela o avanço automático.");if(ImGui::Button("Cancelar avanço",ImVec2(width,0)))runner.speed(1);}
    ImGui::EndDisabled();
    menu::hint("O avanço calcula o voo até o marco escolhido e retorna a 1×. Na costa da nave, avança tão rápido quanto o cálculo permitir; fora do avanço, vale a aceleração orbital configurada em Missões.");
}
