#pragma once
// The terminal count of a launch as the vehicle's own computer runs it (its autosequence, from T-60 s), on what its sensors
// and its actuators' feedback report, never the flight's truth:
// - Self-tests from T-60 s until ignition, each second on the last 10 s: each IMU unit reading and alive (an accelerometer
//   or gyroscope repeating its output exactly 5 times is frozen), its specific force the pad's gravity and its rate the
//   Earth's turning within its errors, the units within theirs of each other; the alignment checked against them (a
//   gyrocompass: level within 0.03°, heading within 0.5°); the GNSS on an RTK fix where the navigation stands (0.3 m); the
//   tanks' gauge at the load the ground measured (1% and its noise); every engine healthy and off. T-40 s: go for launch.
// - T-30 to T-24 s: each gimballed engine swept 5° each way in pitch and in yaw and back, its actuator's feedback within
//   0.2° of the command at the end of each second.
// - T-10 s: the flame deflector's water (the ground's).
// - From T-3 s the engines lit ring by ring from the axis out, 0.4 s apart, each held to its expected thrust as it comes up
//   (failed, or short of 90% 1.2 s after its start: abort); at T-0, every one at thrust, the clamps let go.
// Anything out of line before ignition scrubs the count; after it, aborts on the pad (every engine cut, the clamps holding).
// So do the FDIR's findings while counting. The operator's uplinks: HOLD holds the count before ignition (up to the window;
// during it, aborts), GO resumes it, ABORT scrubs it.
#include "navigation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>
namespace albert::example::starship {
class Countdown {
public:
    enum class Phase {counting,holding,igniting,launched,scrubbed};
    struct Event {std::string name;double at;}; // An event of the flight to come (mission time, s): replanned with the count.
    // design: the vehicle (its first stage's engines lit and swept); s: the flight on the pad as the count starts; flight: the
    // events after liftoff for the broadcast; from: the count's start (s before T-0); window: the longest the count may stand
    // in a hold (s).
    Countdown(const api::VehicleDefinition& design,const api::FlightSample& s,std::vector<Event> flight={},double from=60,double window=1800)
        :from_(from),window_(window),zero_(s.elapsed_seconds()+from),last_(s.elapsed_seconds()),units_(static_cast<std::size_t>(s.imus_size())){
        events_={{"GO PARA LANÇAMENTO",-40},{"TESTE DOS GIMBALS",-30},{"DEFLETOR DE CHAMAS",-10},{"IGNIÇÃO",-3},{"DECOLAGEM",0}};
        for(auto& e:flight)events_.push_back(std::move(e));
        const auto& es=design.stages(0).engines();std::vector<double> radii;
        for(int i=0;i<es.size();++i){const auto& e=es[i];const double r=std::hypot(e.position_meters().y(),e.position_meters().z());
            vac_.push_back(e.vacuum_thrust_newtons());area_.push_back(e.exit_area_meters2());if(e.gimbal_radians()>0)gimballed_.push_back(i);
            std::size_t k=0;while(k<radii.size()&&std::abs(radii[k]-r)>.1)++k;if(k==radii.size()){radii.push_back(r);rings_.emplace_back();}rings_[k].push_back(i);}
        for(std::size_t a=0;a<radii.size();++a)for(std::size_t b=a+1;b<radii.size();++b)if(radii[b]<radii[a]){std::swap(radii[a],radii[b]);std::swap(rings_[a],rings_[b]);} // From the axis out.
        lit_.assign(vac_.size(),-1e9);second_=std::floor(s.elapsed_seconds());
    }
    // A step of the count: its commands, the broadcast's events and clock into q; fdir: what the FDIR found since the last one.
    void step(const api::FlightSample& s,const Navigator& nav,const std::vector<std::string>& fdir,api::StepRequest& q){
        const double now=s.elapsed_seconds(),utc=s.utc_unix_seconds();
        if(!started_){started_=true;refused_=nav.refused();mark(q,"AUTOSSEQUÊNCIA");plan(q,utc,now-zero_);say(now-zero_,"o computador de voo assume a contagem (autossequência)");}
        const double dt=std::max(now-last_,0.);last_=now;
        if(phase_==Phase::launched||phase_==Phase::scrubbed)return;
        if(phase_==Phase::holding)zero_+=dt; // The clock stands.
        const double T=now-zero_;
        for(const auto& u:s.uplinks()){
            if(u=="ABORT"){stop(q,"aborto pedido pelo operador",T);return;}
            if(u=="HOLD"&&phase_==Phase::igniting){stop(q,"espera pedida na ignição",T);return;}
            if(u=="HOLD"&&phase_==Phase::counting){phase_=Phase::holding;held_=now;mark(q,"CONTAGEM EM ESPERA");say(T,"em espera pelo operador");}
            else if(u=="GO"&&phase_==Phase::holding){phase_=Phase::counting;plan(q,utc,T);say(T,"retomada");}}
        if(!fdir.empty()){stop(q,"FDIR: "+fdir.front(),T);return;}
        if(phase_!=Phase::igniting&&dt>0){collect(s,dt); // Until ignition (its shaking past what the tests allow), held or counting.
            if(std::floor(now)!=second_){second_=std::floor(now);roll();
                if(full()){if(const std::string bad=selfTest(s,nav);!bad.empty()){stop(q,bad,T);return;}checked_=true;}}}
        if(phase_==Phase::holding){if(now-held_>window_)stop(q,"janela de lançamento encerrada em espera",T);else q.set_clock_zero_utc_unix_seconds(utc-T);return;}
        if(T>=-40&&!go_){if(!checked_){zero_+=dt;q.set_clock_zero_utc_unix_seconds(utc-T);return;} // Not yet a window of data: the clock waits for it.
            go_=true;mark(q,"GO PARA LANÇAMENTO");say(T,"autotestes em ordem (IMUs, alinhamento, GNSS/RTK, medidor, motores): GO para lançamento");}
        if(T>=-30&&!swept_){const int k=std::min(static_cast<int>(std::floor(T+30)),6); // Six one-second segments, then done.
            if(k!=segment_){if(segment_>=0)if(const std::string bad=followed(s);!bad.empty()){stop(q,bad,T);return;}
                segment_=k;if(k==0){mark(q,"TESTE DOS GIMBALS");say(T,"varredura dos gimbals de "+std::to_string(gimballed_.size())+" motores");}
                if(k>=6){swept_=true;say(T,"gimbals seguiram os comandos");}
                else{const double a=5*deg,p[]{a,-a,0,0,0,0},y[]{0,0,0,a,-a,0};pitch_=p[k];yaw_=y[k];
                    for(int i:gimballed_){auto* c=q.add_commands();c->set_index(static_cast<unsigned>(i+1));c->mutable_gimbal()->set_pitch_radians(pitch_);c->mutable_gimbal()->set_yaw_radians(yaw_);}}}}
        if(T>=-10&&!deluge_){deluge_=true;mark(q,"DEFLETOR DE CHAMAS");say(T,"água do defletor de chamas");}
        if(T>=-3&&phase_==Phase::counting){phase_=Phase::igniting;mark(q,"IGNIÇÃO");say(T,"sequência de ignição");}
        if(phase_==Phase::igniting){
            for(std::size_t r=0;r<rings_.size();++r)if(T>=-3+.4*static_cast<double>(r)&&!(lit_[static_cast<std::size_t>(rings_[r].front())]>-1e9))
                for(int e:rings_[r]){auto* c=q.add_commands();c->set_ignite(true);c->set_index(static_cast<unsigned>(e+1));lit_[static_cast<std::size_t>(e)]=T;}
            const double p=s.static_pressure_pascals();bool ready=true;
            for(std::size_t e=0;e<lit_.size()&&static_cast<int>(e)<s.engines_size();++e){
                if(!(lit_[e]>-1e9)){ready=false;continue;}
                const auto& x=s.engines(static_cast<int>(e));const double want=vac_[e]-p*area_[e],age=T-lit_[e],share=x.thrust_newtons()/want;
                if(x.failed()){stop(q,"motor "+std::to_string(e+1)+" falhou na ignição",T);return;}
                if(age>1.2&&share<.9){stop(q,"motor "+std::to_string(e+1)+" sem empuxo ("+std::to_string(static_cast<int>(100*share))+"% a "+number(age)+" s da partida)",T);return;}
                ready=ready&&share>=.9;}
            if(T>=0){if(!ready){stop(q,"motores fora do empuxo no T-0",T);return;}
                q.add_commands()->set_release(true);phase_=Phase::launched;zero_=now;mark(q,"DECOLAGEM");q.set_clock_zero_utc_unix_seconds(utc);
                say(0,std::to_string(lit_.size())+" motores no empuxo: garras soltas, decolagem");return;}}
        q.set_clock_zero_utc_unix_seconds(utc-T);
    }
    double clock(double elapsed)const{return elapsed-zero_;} // The mission's time (s; negative before T-0).
    double zero()const{return zero_;}                        // T-0 on the flight's clock (its elapsed seconds).
    Phase phase()const{return phase_;}
    const std::string& why()const{return why_;} // Why it scrubbed.
private:
    struct Sum {Vec dv,da;double seconds=0;};
    struct Unit {std::deque<Sum> past;Sum now;Vec lastDv,lastDa;int sameV=0,sameA=0;bool dead=false,frozenV=false,frozenA=false;};
    static constexpr std::size_t span=10; // The tests' window (s).
    static std::string number(double x){char b[32];std::snprintf(b,sizeof b,std::abs(x)>=100?"%.0f":"%.2g",x);return b;}
    static void say(double T,const std::string& what){std::printf("%7.2f s  contagem: %s\n",T,what.c_str());}
    static void mark(api::StepRequest& q,const char* name,double plannedUtc=-1){auto* m=q.add_milestones();m->set_name(name);if(plannedUtc>=0){m->set_utc_unix_seconds(plannedUtc);m->set_planned(true);}}
    void plan(api::StepRequest& q,double utc,double T)const{for(const auto& e:events_)if(e.at>T)mark(q,e.name.c_str(),utc+e.at-T);} // The events to come at their UTC.
    void stop(api::StepRequest& q,const std::string& why,double T){
        const bool lit=phase_==Phase::igniting;phase_=Phase::scrubbed;why_=why;q.set_stop_clock(true); // The broadcast's clock stops there.
        if(lit)q.add_commands()->set_cutoff(true);q.add_commands()->mutable_gimbal(); // Every engine out, every gimbal centred.
        mark(q,lit?"ABORTO NA PLATAFORMA":"CONTAGEM CANCELADA");say(T,std::string(lit?"ABORTO NA PLATAFORMA (motores cortados, garras fechadas): ":"CANCELADA: ")+why);
    }
    void collect(const api::FlightSample& s,double dt){ // This second's increments of each unit and the gauge's readings.
        for(int u=0;u<s.imus_size()&&static_cast<std::size_t>(u)<units_.size();++u){Unit& x=units_[static_cast<std::size_t>(u)];const auto& m=s.imus(u);
            if(!m.valid()){x.dead=true;continue;}const Vec dv=vec(m.delta_velocity_meters_per_second()),da=vec(m.delta_angle_radians());
            x.sameV=dv.x==x.lastDv.x&&dv.y==x.lastDv.y&&dv.z==x.lastDv.z?x.sameV+1:0;x.sameA=da.x==x.lastDa.x&&da.y==x.lastDa.y&&da.z==x.lastDa.z?x.sameA+1:0;
            x.frozenV=x.frozenV||x.sameV>=5;x.frozenA=x.frozenA||x.sameA>=5;x.lastDv=dv;x.lastDa=da;x.now.dv=x.now.dv+dv;x.now.da=x.now.da+da;x.now.seconds+=dt;}
        if(const auto& g=s.propellant_gauge();g.valid()&&g.elapsed_seconds()>gaugeAt_){gaugeAt_=g.elapsed_seconds();gaugeNow_.first+=g.propellant_kilograms();++gaugeNow_.second;}
        else if(s.has_propellant_gauge()&&!g.valid())gaugeDead_=true;
    }
    void roll(){ // A second done: into the window, the oldest out.
        for(Unit& x:units_){x.past.push_back(x.now);x.now={};if(x.past.size()>span)x.past.pop_front();}
        gauges_.push_back(gaugeNow_);gaugeNow_={};if(gauges_.size()>span)gauges_.pop_front();
    }
    bool full()const{return gauges_.size()>=span&&std::all_of(units_.begin(),units_.end(),[](const Unit& x){return x.past.size()>=span;});}
    // The self-tests on the window: what is out of line, or nothing.
    std::string selfTest(const api::FlightSample& s,const Navigator& nav)const{
        const Mat c=matrix(nav.attitude),b=transposed(c);const Vec up=gravityFixed(nav.position)*-1.,w{0,0,earthRate}; // At rest on the pad: the ground's push, the Earth's turning.
        const Vec fb=mul(b,up),wb=mul(b,w);std::vector<Vec> f,r;
        for(std::size_t u=0;u<units_.size();++u){const Unit& x=units_[u];const std::string n="IMU "+std::to_string(u+1);
            if(x.dead)return n+" sem leitura";if(x.frozenV)return n+": acelerômetro com a saída congelada";if(x.frozenA)return n+": giroscópio com a saída congelada";
            Sum t;for(const Sum& k:x.past){t.dv=t.dv+k.dv;t.da=t.da+k.da;t.seconds+=k.seconds;}if(!(t.seconds>0))return n+" sem leitura";
            f.push_back(t.dv*(1/t.seconds));r.push_back(t.da*(1/t.seconds));
            // Its errors (navigation grade): bias 25 µg and scale 50 ppm, 0.01°/h; and the alignment's 0.005° of level.
            if(const double e=norm(f.back()+fb*-1.);e>4e-3)return n+": acelerômetro fora da gravidade do pad ("+number(e/g0*1e6)+" µg)";
            if(const double e=norm(r.back()+wb*-1.);e>1.5e-6)return n+": giroscópio fora da rotação da Terra ("+number(e/deg*3600)+"°/h)";}
        if(!f.empty()){auto med=[](std::vector<double> v){std::sort(v.begin(),v.end());return v.size()%2?v[v.size()/2]:(v[v.size()/2-1]+v[v.size()/2])/2;};
            auto median=[&](const std::vector<Vec>& v){std::vector<double> x,y,z;for(const Vec& a:v){x.push_back(a.x);y.push_back(a.y);z.push_back(a.z);}return Vec{med(x),med(y),med(z)};};
            const Vec fv=median(f),rv=median(r);Vec fm,rm;
            for(std::size_t u=0;u<f.size();++u){const std::string n="IMU "+std::to_string(u+1);
                if(const double e=norm(f[u]+fv*-1.);e>3e-3)return n+": acelerômetro fora das outras unidades ("+number(e/g0*1e6)+" µg)";
                if(const double e=norm(r[u]+rv*-1.);e>1e-6)return n+": giroscópio fora das outras unidades ("+number(e/deg*3600)+"°/h)";
                fm=fm+f[u]*(1./static_cast<double>(f.size()));rm=rm+r[u]*(1./static_cast<double>(f.size()));}
            // The alignment against them: the measured gravity's direction (level), the Earth's rate across it (heading).
            const Vec fe=mul(c,fm),re=mul(c,rm),u=up*(1/norm(up));
            if(const double tilt=std::acos(std::clamp(dot(fe,u)/norm(fe),-1.,1.));tilt>.03*deg)return "alinhamento: nível "+number(tilt/deg)+"° fora da gravidade medida";
            const Vec h1=re+u*-dot(re,u),h2=w+u*-dot(w,u);
            if(const double head=std::acos(std::clamp(dot(h1,h2)/(norm(h1)*norm(h2)),-1.,1.));head>.5*deg)return "alinhamento: rumo "+number(head/deg)+"° fora da rotação da Terra";}
        const auto& g=s.gnss();
        if(!g.valid())return "GNSS sem posição";if(!g.rtk())return "GNSS sem RTK no pad";
        if(const double e=norm(vec(g.position_ecef_meters())+nav.position*-1.);e>.3||nav.refused()>refused_)return "GNSS fora da posição levantada do pad ("+number(e)+" m)";
        const std::size_t st=static_cast<std::size_t>(s.stage());
        if(s.has_propellant_gauge()&&st<nav.load.size()){const double load=nav.load[st];double sum=0;int n=0;for(const auto& [k,m]:gauges_){sum+=k;n+=m;}
            if(gaugeDead_||n==0)return "medidor de propelante sem leitura";
            const double mean=sum/n,sigma=.005*load+300;
            if(std::abs(mean-load)>.01*load+4*sigma/std::sqrt(static_cast<double>(n)))
                return "medidor de propelante fora da carga ("+std::to_string(static_cast<int>(mean/1e3))+" t lidas, "+std::to_string(static_cast<int>(load/1e3))+" t carregadas)";}
        for(int e=0;e<s.engines_size();++e){if(s.engines(e).failed())return "motor "+std::to_string(e+1)+" em falha";if(s.engines(e).on())return "motor "+std::to_string(e+1)+" aceso antes da ignição";}
        return {};
    }
    std::string followed(const api::FlightSample& s)const{ // The sweep's segment just ended: each actuator where it was told.
        for(int i:gimballed_){if(i>=s.engines_size())continue;const auto& g=s.engines(i).gimbal();
            if(std::abs(g.pitch_radians()-pitch_)>.2*deg||std::abs(g.yaw_radians()-yaw_)>.2*deg)
                return "gimbal do motor "+std::to_string(i+1)+" não seguiu o comando ("+number(g.pitch_radians()/deg)+"°, "+number(g.yaw_radians()/deg)+"° para "+number(pitch_/deg)+"°, "+number(yaw_/deg)+"°)";}
        return {};
    }
    double from_,window_,zero_,last_,held_=0,pitch_=0,yaw_=0,gaugeAt_=-1,second_=0;int segment_=-1,refused_=0;
    bool started_=false,checked_=false,go_=false,swept_=false,deluge_=false,gaugeDead_=false;Phase phase_=Phase::counting;std::string why_;
    std::vector<Event> events_;std::vector<Unit> units_;std::pair<double,int> gaugeNow_{};std::deque<std::pair<double,int>> gauges_;
    std::vector<double> vac_,area_,lit_;std::vector<int> gimballed_;std::vector<std::vector<int>> rings_;
};
}
