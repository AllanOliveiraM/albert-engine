#include "core/scene.hpp"
#include "core/bridge.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace albert::core;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid command was accepted");}
Object object(){Object o;o.id="test";o.name="Test";o.samples={{100,{1e11,0,0},{10,0,0},Quaternion{0,0,0,1},true},{102,{1e11+20,0,0},{10,0,0},Quaternion{0,0,0,-1},true}};return o;}
int main(){try{
    Scene scene;require(scene.objects().size()==3,"Built-ins missing");scene.create(object());
    scene.builtinState("moon",{100,{1,2,3}});require(scene.evaluate("moon",104).stale,"Expired built-in state was reported fresh");
    auto s=scene.evaluate("test",101);require(std::abs(s.state.positionMeters.x-(1e11+10))<1e-5,"Interpolation lost metre precision");
    require(s.state.attitude&&std::abs(s.state.attitude->w-1)<1e-9,"Antipodal quaternion interpolation failed");
    // Past the last sample: on along its velocity for at most a second, then stale.
    require(std::abs(scene.evaluate("test",102.5).state.positionMeters.x-(1e11+25))<1e-5&&!scene.evaluate("test",102.5).stale,"Not carried on past the last sample");
    require(std::abs(scene.evaluate("test",104).state.positionMeters.x-(1e11+30))<1e-5&&scene.evaluate("test",104).stale,"Disconnected stream extrapolated forever");
    const auto revision=scene.revision();rejects([&]{scene.samples("test",{{103,{1,0,0}},{102,{2,0,0}}});});
    require(scene.revision()==revision&&scene.get("test").samples.size()==2,"Rejected batch partially changed state");
    rejects([&]{scene.create(object());});rejects([&]{scene.remove("earth");});rejects([&]{scene.samples("moon",{{100,{}}});});
    auto bad=object();bad.id="nan";bad.samples[0].positionMeters.x=std::numeric_limits<double>::quiet_NaN();rejects([&]{scene.create(bad);});
    bad=object();bad.id="bad-quaternion";bad.samples[0].attitude=Quaternion{0,0,0,0};rejects([&]{scene.create(bad);});
    // Simulated dates reach 1900: instants before 1970 are valid, far earlier ones are not.
    scene.builtinState("moon",{-14182940.,{1,2,3}});require(scene.evaluate("moon",-14182940.).state.utcSeconds==-14182940.,"Pre-1970 instant rejected");
    rejects([&]{scene.builtinState("moon",{-2e10,{1,2,3}});});
    std::vector<Sample> values;for(int i=0;i<256;++i)values.push_back({103.+i,{double(i),0,0}});scene.samples("test",std::move(values));
    require(scene.get("test").samples.size()==256,"History is unbounded");scene.remove("test");require(scene.objects().size()==3,"Removal failed");
    Bridge bridge;auto cancelled=bridge.submit(Focus{"earth"});*cancelled.cancelled=true;bool applied=false;
    bridge.drain([&](Command&){applied=true;return Reply{};});require(!applied&&cancelled.result.get().code==Code::Cancelled,"Cancelled command changed scene");
    std::vector<Ticket> tickets;for(size_t i=0;i<Bridge::capacity;++i)tickets.push_back(bridge.submit(Focus{"earth"}));
    require(bridge.submit(Focus{"earth"}).result.get().code==Code::Exhausted,"Queue has no backpressure");bridge.close();
    for(auto& t:tickets)require(t.result.get().code==Code::Unavailable,"Shutdown left a waiting client");
    require(bridge.submit(Focus{"earth"}).result.get().code==Code::Unavailable,"Closed bridge accepted a command");
    Bridge concurrent;std::vector<Ticket> a,b;
    std::thread one([&]{for(int i=0;i<64;++i)a.push_back(concurrent.submit(Focus{"earth"}));});
    std::thread two([&]{for(int i=0;i<64;++i)b.push_back(concurrent.submit(Focus{"moon"}));});one.join();two.join();
    size_t count=0;while(concurrent.drain([&](Command&){++count;return Reply{};})){}
    require(count==128,"Concurrent producers lost commands");for(auto& t:a)require(t.result.get().code==Code::Ok,"Concurrent command failed");for(auto& t:b)require(t.result.get().code==Code::Ok,"Concurrent command failed");
    { // A vehicle's moving parts interpolate with the rest; non-finite ones are refused.
        Scene parts;Object o=object();o.id="vehicle";o.samples[0].parts=std::make_shared<std::vector<float>>(std::vector<float>{0,1});
        o.samples[1].parts=std::make_shared<std::vector<float>>(std::vector<float>{1,3});parts.create(o);
        const auto e=parts.evaluate("vehicle",101);require(e.state.parts&&std::abs((*e.state.parts)[0]-.5f)<1e-6f&&std::abs((*e.state.parts)[1]-2)<1e-6f,"Vehicle parts not interpolated");
        Object bad=object();bad.id="bad";bad.samples[0].parts=std::make_shared<std::vector<float>>(std::vector<float>{std::numeric_limits<float>::quiet_NaN()});rejects([&]{parts.create(bad);});
    }
    { // Fixed on the Earth: where its frame, turning, puts it; an anchor on no built-in body is refused.
        Scene fixed;Object o=object();o.id="site";o.anchor=Anchor{"earth",{6378137,0,0},Quaternion{0,0,0,1}};fixed.create(o);fixed.builtinState("earth",{100,{0,0,0}});
        require(std::abs(fixed.evaluate("site",101).state.positionMeters.x-1e11-10)<1e-6,"Anchored object without a frame not on its samples");
        const double spin=7.2921158553e-5,quarter=3.14159265358979323846/2/spin;fixed.builtinFrame("earth",100,{1,0,0,0,1,0,0,0,1},spin);
        const auto e=fixed.evaluate("site",100+quarter).state;const auto q=*e.attitude;
        require(std::abs(e.positionMeters.x)<1e-6&&std::abs(e.positionMeters.y-6378137)<1e-6&&std::abs(e.velocityMetersPerSecond.x+spin*6378137)<1e-9&&
            std::abs(q.z-std::sqrt(.5))<1e-12&&std::abs(q.w-std::sqrt(.5))<1e-12,"Anchored object not turned with its body");
        Object bad=object();bad.id="loose";bad.anchor=Anchor{"test",{1,0,0},Quaternion{0,0,0,1}};rejects([&]{fixed.create(bad);});
    }
    std::cout<<"Scene OK: precision, validation, interpolation, vehicle parts, anchors, bounded history, cancellation and concurrent queue\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
