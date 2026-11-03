#include "regional_refresh.hpp"
#include <cstdio>

namespace {
using Schedule=albert::RegionalRefreshSchedule;
using namespace std::chrono_literals;
int failures=0;
void check(bool ok,const char* name){std::printf("%s %s\n",ok?"ok  ":"FAIL",name);failures+=!ok;}
}

int main(){
    const Schedule::Time start{};
    {
        Schedule s;
        check(s.ready(start),"An initial view can load immediately");
        s.changed(start);
        check(!s.ready(start+119ms)&&s.ready(start+120ms),"A changed view waits for a short quiet period");
        s.changed(start+100ms);
        check(!s.ready(start+219ms)&&s.ready(start+220ms),"A second small movement groups with the first");
        s.clearChanges();
        check(s.ready(start+221ms),"Starting a load clears the pending movement burst");
    }
    {
        Schedule s;
        // No single view remains still for the quiet period, but imagery must
        // keep following the camera instead of starving indefinitely.
        bool grouped=true;
        for(int milliseconds=0;milliseconds<500;milliseconds+=20){
            const auto now=start+std::chrono::milliseconds(milliseconds);
            s.changed(now);
            grouped=grouped&&!s.ready(now);
        }
        check(grouped,"Continuous motion is grouped before the maximum delay");
        check(s.ready(start+500ms),"Continuous motion cannot postpone a refresh beyond half a second");
        s.clearChanges();s.changed(start+501ms);
        check(!s.ready(start+502ms),"A new load starts a separate movement burst");
    }
    {
        Schedule s;s.finished(start,true);
        // View changes, hiding the surface and returning must not evade the
        // source's backoff. No real time, files, network or GPU is needed.
        bool backedOff=true;
        for(int seconds=1;seconds<120;++seconds){
            const auto now=start+std::chrono::seconds(seconds);
            if(seconds%3==0)s.clearChanges();else s.changed(now);
            backedOff=backedOff&&!s.ready(now);
        }
        check(backedOff,"Movement and temporary loss of ground preserve the failure delay");
        check(s.ready(start+120s),"The current view becomes eligible when the failure delay ends");
        s.finished(start+120s,false);s.clearChanges();
        check(!s.ready(start+120s+79ms)&&s.ready(start+120s+80ms),"Success replaces the failure delay with normal upload pacing");
    }
    {
        Schedule s;s.finished(start,true);s.changed(start+1s);s.reset();
        check(s.ready(start+1s),"Explicit retry or a different source clears both waits");
        s.finished(start+1s,false);s.changed(start+1050ms);
        check(!s.ready(start+1080ms)&&s.ready(start+1170ms),"Changes during a load still get their quiet period after completion");
        s.clearChanges();s.finished(start+2s,false);s.changed(start+2010ms);s.changed(start+2020ms);
        check(!s.ready(start+2080ms)&&s.ready(start+2140ms),"Successful loading does not erase movement recorded during the load");
    }
    return failures?1:0;
}
