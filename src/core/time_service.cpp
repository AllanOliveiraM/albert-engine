#include "time.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>

namespace albert::time {
namespace {
#ifdef _WIN32
using Socket=SOCKET;constexpr Socket invalidSocket=INVALID_SOCKET;
void closeSocket(Socket s){closesocket(s);}
using AddressLength=int;
#else
using Socket=int;constexpr Socket invalidSocket=-1;
void closeSocket(Socket s){close(s);}
using AddressLength=socklen_t;
#endif
struct SocketGuard {Socket value=invalidSocket;~SocketGuard(){if(value!=invalidSocket)closeSocket(value);}};
struct Address {sockaddr_storage value{};AddressLength size=0;};
struct Resolve {std::vector<Address> addresses;std::stop_token stop;};
curl_socket_t resolved(void* user,curlsocktype,curl_sockaddr* info) noexcept{
    // libcurl owns its cancellable, timed DNS resolver. Capture addresses before
    // opening any TCP socket; only our UDP query below reaches the time server.
    auto& result=*static_cast<Resolve*>(user);
    if(info->addrlen<=sizeof(sockaddr_storage)&&result.addresses.size()<8)try{
        Address address;address.size=static_cast<AddressLength>(info->addrlen);std::memcpy(&address.value,&info->addr,info->addrlen);result.addresses.push_back(address);
    }catch(...){}
    return CURL_SOCKET_BAD;
}
int cancelled(void* user,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<Resolve*>(user)->stop.stop_requested()?1:0;}
std::vector<Address> resolve(const char* host,std::stop_token stop){
    Resolve state{{},stop};auto* handle=curl_easy_init();if(!handle)throw std::runtime_error("NTP: DNS resolver unavailable");
    struct Guard {CURL* value;~Guard(){curl_easy_cleanup(value);}} guard{handle};
    const auto url=std::string("http://")+host+":123/";
    curl_easy_setopt(handle,CURLOPT_URL,url.c_str());curl_easy_setopt(handle,CURLOPT_PROXY,"");curl_easy_setopt(handle,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(handle,CURLOPT_TIMEOUT_MS,2000L);curl_easy_setopt(handle,CURLOPT_CONNECTTIMEOUT_MS,2000L);
    curl_easy_setopt(handle,CURLOPT_OPENSOCKETFUNCTION,resolved);curl_easy_setopt(handle,CURLOPT_OPENSOCKETDATA,&state);
    curl_easy_setopt(handle,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(handle,CURLOPT_XFERINFOFUNCTION,cancelled);curl_easy_setopt(handle,CURLOPT_XFERINFODATA,&state);
    curl_easy_perform(handle); // Deliberate connection failure after resolving.
    if(state.addresses.empty())throw std::runtime_error("NTP: DNS failed/timed out");
    std::stable_sort(state.addresses.begin(),state.addresses.end(),[](const Address& a,const Address& b){return a.value.ss_family==AF_INET&&b.value.ss_family!=AF_INET;});
    return state.addresses;
}
Observation query(const char* host,std::stop_token stop,std::set<std::string>& endpoints,std::mt19937_64& random){
    auto addresses=resolve(host,stop);std::string error="NTP: UDP timed out/blocked";
    for(const auto& address:addresses){
        if(stop.stop_requested())throw std::runtime_error("NTP: cancelled");
        const void* ip=address.value.ss_family==AF_INET ? static_cast<const void*>(&reinterpret_cast<const sockaddr_in*>(&address.value)->sin_addr):static_cast<const void*>(&reinterpret_cast<const sockaddr_in6*>(&address.value)->sin6_addr);
        char name[INET6_ADDRSTRLEN]{};if(!inet_ntop(address.value.ss_family,ip,name,sizeof(name)))continue;
        if(endpoints.contains(name))continue;
        SocketGuard socket{::socket(address.value.ss_family,SOCK_DGRAM,IPPROTO_UDP)};if(socket.value==invalidSocket)continue;
#ifdef _WIN32
        u_long nonblocking=1;if(ioctlsocket(socket.value,FIONBIO,&nonblocking)!=0)continue;
#else
        if(fcntl(socket.value,F_SETFL,O_NONBLOCK)!=0)continue;
#endif
        if(::connect(socket.value,reinterpret_cast<const sockaddr*>(&address.value),address.size)!=0)continue;
        const auto packet=request(random());const double start=monotonicSeconds();
        if(::send(socket.value,reinterpret_cast<const char*>(packet.data()),static_cast<int>(packet.size()),0)!=static_cast<int>(packet.size()))continue;
        while(!stop.stop_requested()&&monotonicSeconds()-start<1.){
            fd_set ready;FD_ZERO(&ready);FD_SET(socket.value,&ready);timeval wait{0,100000};
            const int selected=::select(static_cast<int>(socket.value)+1,&ready,nullptr,nullptr,&wait);
            if(selected<0)break;if(!selected)continue;
            std::array<unsigned char,512> data{};const auto length=::recv(socket.value,reinterpret_cast<char*>(data.data()),static_cast<int>(data.size()),0);const double end=monotonicSeconds();
            if(length<=0)break;
            try{auto result=decode(std::span(data.data(),static_cast<size_t>(length)),packet,start,end,host);endpoints.insert(name);return result;}
            catch(const std::exception& e){throw std::runtime_error(e.what());}
        }
        // At most one timeout per provider per round; no address fan-out bursts.
        error="NTP: UDP timed out/blocked";break;
    }
    throw std::runtime_error(error);
}
double localUtc(){return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();}
}
struct Service::Impl {
    mutable std::mutex mutex;std::condition_variable changed;Discipline discipline{localUtc(),monotonicSeconds()};
    bool syncing=true,requested=false;std::jthread worker;
    Impl(){
        if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("NTP: curl initialization failed");
        try{worker=std::jthread([this](std::stop_token stop){run(stop);});}catch(...){curl_global_cleanup();throw;}
    }
    ~Impl(){worker.request_stop();changed.notify_all();if(worker.joinable())worker.join();curl_global_cleanup();}
    void run(std::stop_token stop) noexcept{
        constexpr std::array<const char*,3> hosts{"time.cloudflare.com","time.nist.gov","ptbtime1.ptb.de"};
        std::array<double,3> allowed{};unsigned failures=0;double next=0,lastTick=monotonicSeconds();
        try{
            std::random_device entropy;std::mt19937_64 random((std::uint64_t(entropy())<<32)^entropy());
            while(!stop.stop_requested()){
                const double now=monotonicSeconds();
                std::unique_lock lock(mutex);
                if(now-lastTick>30.){discipline.failed("NTP: resume detected; checking external time");next=std::min(next,now);}
                lastTick=now;
                if(requested){next=std::min(next,now);requested=false;}
                // Manual requests and resume never bypass each provider's cooldown.
                bool available=false;for(double deadline:allowed)available|=now>=deadline;
                if(now<next||!available){changed.wait_for(lock,std::chrono::seconds(1));continue;}
                syncing=true;lock.unlock();
                std::vector<Observation> observations;std::set<std::string> endpoints;std::string errors;
                for(size_t i=0;i<hosts.size()&&!stop.stop_requested();++i){
                    if(monotonicSeconds()<allowed[i])continue;
                    allowed[i]=monotonicSeconds()+60.;
                    try{observations.push_back(query(hosts[i],stop,endpoints,random));}
                    catch(const std::exception& e){
                        const std::string message=e.what();if(!errors.empty())errors+="; ";errors+=std::string(hosts[i])+": "+message;
                        if(message.starts_with("NTP KoD:"))allowed[i]=monotonicSeconds()+3600.;
                        if(message=="NTP KoD: DENY"||message=="NTP KoD: RSTR")allowed[i]=std::numeric_limits<double>::infinity();
                    }
                }
                const double finished=monotonicSeconds();lock.lock();
                if(discipline.synchronize(observations,finished))failures=0;
                else {discipline.failed(errors.empty()?"NTP: sources disagree or are rate limited":errors);++failures;}
                syncing=false;
                const double delay=failures ? std::min(1800.,60.*(1u<<std::min(failures-1,5u))):300.;
                next=finished+delay+static_cast<double>(random()%1000)*.01;lastTick=finished;
            }
        }catch(const std::exception& e){std::lock_guard lock(mutex);discipline.failed(e.what());syncing=false;}
    }
};
Service::Service():impl_(std::make_unique<Impl>()){}
Service::~Service()=default;
double Service::utcSeconds()const{std::lock_guard lock(impl_->mutex);return impl_->discipline.utcSeconds(monotonicSeconds());}
Status Service::status()const{std::lock_guard lock(impl_->mutex);auto s=impl_->discipline.read(monotonicSeconds());s.synchronizing=impl_->syncing;return s;}
void Service::resynchronize(){std::lock_guard lock(impl_->mutex);impl_->requested=true;impl_->changed.notify_all();}
Service& clock(){static Service value;return value;}
}
