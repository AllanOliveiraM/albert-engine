#include "missions.hpp"
#include "starship_flight.hpp"
#include <condition_variable>

namespace albert::missions {
namespace api=::albert::v1;
namespace ex=::albert::example;

Runner::~Runner(){stop();} // jthread joins before the status and mutex are destroyed.
Status Runner::status()const{std::lock_guard lock(mutex_);return status_;}
Playback Runner::playback()const{std::lock_guard lock(mutex_);return playback_;}
void Runner::speed(double multiplier){if(!std::isfinite(multiplier))return;std::lock_guard lock(mutex_);playback_.target=-1;
    const double selected=std::clamp(multiplier,0.,maximumSpeed);if(selected>0)playback_.resumeSpeed=selected;playback_.speed=selected;}
void Runner::resume(){std::lock_guard lock(mutex_);playback_.target=-1;playback_.speed=playback_.resumeSpeed;}
void Runner::advanceTo(Phase phase){std::lock_guard lock(mutex_);const int next=static_cast<int>(phase);
    if(next>static_cast<int>(playback_.phase)&&next<static_cast<int>(Phase::Finished)){playback_.resumeSpeed=1;playback_.target=next;playback_.speed=1;}}
void Runner::stop(){
    {std::lock_guard lock(mutex_);if(!status_.active)return;status_.state=State::Stopping;status_.message="Encerrando missão...";}
    worker_.request_stop();
}
bool Runner::startStarship(unsigned port,StarshipOptions options){
    {std::lock_guard lock(mutex_);if(status_.active)return false;}
    if(worker_.joinable())worker_.join();
    {std::lock_guard lock(mutex_);status_={State::Starting,"Preparando Starship...",true};playback_={};}
    try{worker_=std::jthread([this,port,options](std::stop_token stop){
        ex::flightStop=stop;
        ex::flightSpeed=[this]{const auto p=playback();return p.target>=0?maximumSpeed:p.speed;};
        ex::flightAdvance=[this]{return playback().target>=0;};
        ex::starship::missionProgress=[this](ex::starship::MissionPhase phase){
            std::lock_guard lock(mutex_);playback_.phase=std::max(playback_.phase,static_cast<Phase>(phase));
            if(playback_.target>=0&&static_cast<int>(playback_.phase)>=playback_.target){playback_.target=-1;playback_.speed=playback_.resumeSpeed=1;}
        };
        auto channel=grpc::CreateChannel("127.0.0.1:"+std::to_string(port),grpc::InsecureChannelCredentials());
        auto flight=api::FlightService::NewStub(channel);
        bool ownsFlights=false,failed=false;std::string message;
        try{
            api::FlightList list;
            ex::call([&](grpc::ClientContext& c){c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));return flight->ListFlights(&c,{},&list);});
            // The shared example uses these ids. Never replace a flight started by a client.
            for(const auto& id:list.flight_ids())if(id=="flight"||id=="booster")throw std::runtime_error("Encerre o voo externo antes de iniciar o Starship.");
            ex::checkFlightStop();ownsFlights=true;
            {std::lock_guard lock(mutex_);if(!stop.stop_requested())status_={State::Running,"Starship em execução",true};}
            auto camera=api::CameraService::NewStub(channel);
            std::function<void()> follow;
            if(options.followBooster)follow=[&]{api::FocusRequest q;q.set_id("flight.booster");api::Applied applied;
                ex::call([&](grpc::ClientContext& c){return camera->Focus(&c,q,&applied);});};
            const auto result=ex::starship::mission(*flight,false,options.accelerateOrbit?1000:1,false,follow);
            message=result.scrubbed.empty()?"Programa de voo concluído.":"Contagem cancelada: "+result.scrubbed;
            {std::lock_guard lock(mutex_);playback_.phase=Phase::Finished;playback_.target=-1;if(!stop.stop_requested())status_={State::Completed,message,true};}
            // Leave the final scene visible until the user ends the mission.
            std::mutex mutex;std::unique_lock lock(mutex);std::condition_variable_any wake;
            wake.wait(lock,stop,[]{return false;});
        }catch(const std::exception& e){failed=!stop.stop_requested();message=e.what();}
        ex::flightStop={};
        ex::flightSpeed={};ex::flightAdvance={};ex::starship::missionProgress={};
        if(ownsFlights){
            // Include a spawn whose response was cancelled after the server applied it.
            // Deadlines also bound teardown when the window has stopped draining its bridge.
            for(const char* id:{"booster","flight"}){api::FlightRequest q;q.set_flight_id(id);api::Applied applied;grpc::ClientContext c;
                c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(2));
                const auto result=flight->EndFlight(&c,q,&applied);
                if(!result.ok()&&result.error_code()!=grpc::StatusCode::FAILED_PRECONDITION){failed=true;message="Falha ao encerrar voo: "+result.error_message();}
            }
        }
        {std::lock_guard lock(mutex_);status_={failed?State::Failed:State::Idle,failed?message:"Missão encerrada.",false};}
    });}catch(const std::exception& e){std::lock_guard lock(mutex_);status_={State::Failed,e.what(),false};return false;}
    return true;
}
}
