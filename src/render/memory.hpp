#pragma once
#include <cstdlib>
#ifdef __GLIBC__
#include <malloc.h>
#include <chrono>
#include <future>
#endif
namespace albert {
// Free terrain can remain in allocator arenas. Return unused pages on a
// worker, with one bounded task and a cooldown, never inside a draw call.
class MemoryReclaimer {
public:
    void update(bool released){
#ifdef __GLIBC__
        requested_|=released;
        if(pending_.valid()){
            if(pending_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            pending_.get();
        }
        const auto now=std::chrono::steady_clock::now();
        if(!requested_||now<next_)return;
        next_=now+std::chrono::seconds(5);
        try{pending_=std::async(std::launch::async,[]{malloc_trim(0);});requested_=false;}catch(const std::system_error&){}
#else
        (void)released; // Other allocators retain/release their own free lists.
#endif
    }
private:
#ifdef __GLIBC__
    bool requested_=false;
    std::future<void> pending_;
    std::chrono::steady_clock::time_point next_{};
#endif
};
}
