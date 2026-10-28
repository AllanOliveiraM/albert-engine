#pragma once
#include "gpu/context.hpp"
#include <webgpu/wgpu.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
namespace albert::gpu {
// Nonblocking timestamp readback. Never reuse a query or mapped buffer while it is in flight.
class PassTiming {
    Context& gpu_;WGPUQuerySet queries_=nullptr;WGPUBuffer resolve_=nullptr,read_=nullptr;
    bool busy_=false,pending_=false,active_=false;std::vector<std::string> names_;
    std::map<std::string,std::pair<double,unsigned>> sums_;
    std::map<std::string,double> latest_;float period_=1;
    std::chrono::steady_clock::time_point printed_=std::chrono::steady_clock::now();
    static constexpr unsigned capacity=512;
public:
    explicit PassTiming(Context& gpu):gpu_(gpu){
        const auto* env=std::getenv("ALBERT_GPU_TIMING");if(!env||std::string(env)!="1")return;
        if(!wgpuDeviceHasFeature(gpu.device(),WGPUFeatureName_TimestampQuery)){std::fprintf(stderr,"GPU timing: timestamp-query unavailable (%s)\n",gpu.adapter().c_str());return;}
        WGPUQuerySetDescriptor q{};q.type=WGPUQueryType_Timestamp;q.count=capacity;queries_=wgpuDeviceCreateQuerySet(gpu.device(),&q);
        WGPUBufferDescriptor b{};b.size=capacity*8;b.usage=WGPUBufferUsage_QueryResolve|WGPUBufferUsage_CopySrc;resolve_=wgpuDeviceCreateBuffer(gpu.device(),&b);
        b.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;read_=wgpuDeviceCreateBuffer(gpu.device(),&b);period_=wgpuQueueGetTimestampPeriod(gpu.queue());
    }
    ~PassTiming(){if(busy_&&!pending_){wgpuDevicePoll(gpu_.device(),true,nullptr);wgpuInstanceProcessEvents(gpu_.instance());}if(read_)wgpuBufferRelease(read_);if(resolve_)wgpuBufferRelease(resolve_);if(queries_)wgpuQuerySetRelease(queries_);}
    bool enabled()const{return queries_!=nullptr;}
    void begin(bool measured){active_=queries_&&!busy_&&measured;if(active_)names_.clear();}
    const WGPUPassTimestampWrites* pass(const std::string& name,WGPUPassTimestampWrites& t){
        if(!active_||names_.size()*2>=capacity)return nullptr;
        t.querySet=queries_;t.beginningOfPassWriteIndex=unsigned(names_.size()*2);t.endOfPassWriteIndex=t.beginningOfPassWriteIndex+1;names_.push_back(name);return &t;
    }
    void finish(WGPUCommandEncoder encoder){
        if(!active_||names_.empty())return;const unsigned count=unsigned(names_.size()*2);
        wgpuCommandEncoderResolveQuerySet(encoder,queries_,0,count,resolve_,0);wgpuCommandEncoderCopyBufferToBuffer(encoder,resolve_,0,read_,0,count*8);busy_=pending_=true;active_=false;
    }
    void submitted(){
        if(!pending_)return;pending_=false;
        WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=this;
        cb.callback=[](WGPUMapAsyncStatus status,WGPUStringView,void* u,void*){auto& s=*static_cast<PassTiming*>(u);
            if(status==WGPUMapAsyncStatus_Success){const auto* ticks=static_cast<const uint64_t*>(wgpuBufferGetConstMappedRange(s.read_,0,s.names_.size()*16));s.latest_.clear();
                for(size_t i=0;i<s.names_.size();++i)if(ticks[i*2+1]>=ticks[i*2])s.latest_[s.names_[i]]+=double(ticks[i*2+1]-ticks[i*2])*s.period_*1e-6;
                wgpuBufferUnmap(s.read_);for(const auto&[name,ms]:s.latest_){auto& sum=s.sums_[name];sum.first+=ms;++sum.second;}
                const auto now=std::chrono::steady_clock::now();if(now-s.printed_>=std::chrono::seconds(2)){std::fprintf(stderr,"GPU passes (%s):",s.gpu_.adapter().c_str());for(const auto&[name,sum]:s.sums_)std::fprintf(stderr," %s=%.3fms",name.c_str(),sum.first/sum.second);std::fprintf(stderr,"\n");s.sums_.clear();s.printed_=now;}}
            s.busy_=false;};
        wgpuBufferMapAsync(read_,WGPUMapMode_Read,0,names_.size()*16,cb);
    }
    const std::map<std::string,double>& latest()const{return latest_;}
};
}
