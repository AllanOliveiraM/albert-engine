#include "bridge.hpp"
#include <chrono>
#include <stdexcept>
namespace albert::core {
Ticket Bridge::submit(Command command){
    Pending p{std::move(command),{},std::make_shared<std::atomic_bool>(false)};Ticket ticket{p.promise.get_future(),p.cancelled};
    std::lock_guard lock(mutex_);
    if(closed_)p.promise.set_value({Code::Unavailable,"Application is shutting down",0});
    else if(queue_.size()>=capacity)p.promise.set_value({Code::Exhausted,"Command queue is full",0});
    else queue_.push_back(std::move(p));
    return ticket;
}
size_t Bridge::drain(const std::function<Reply(Command&)>& apply){
    const auto start=std::chrono::steady_clock::now();size_t count=0;
    while(count<capacity){ // Within 2 ms: a flight's samples, its arms and the clock come many a frame.
        std::optional<Pending> p;
        {std::lock_guard lock(mutex_);if(queue_.empty())break;p.emplace(std::move(queue_.front()));queue_.pop_front();}
        Reply result;
        if(*p->cancelled)result={Code::Cancelled,"Command cancelled before application",0};
        else try{result=apply(p->command);}
        catch(const std::out_of_range& e){result={Code::NotFound,e.what(),0};}
        catch(const std::length_error& e){result={Code::Exhausted,e.what(),0};}
        catch(const std::exception& e){result={Code::Invalid,e.what(),0};}
        p->promise.set_value(std::move(result));++count;
        if(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(2))break;
    }
    return count;
}
size_t Bridge::waiting() const{std::lock_guard lock(mutex_);return queue_.size();}
void Bridge::publish(Snapshot s){snapshot_.store(std::make_shared<const Snapshot>(std::move(s)));}
std::shared_ptr<const Snapshot> Bridge::snapshot() const{return snapshot_.load();}
void Bridge::close(){std::lock_guard lock(mutex_);closed_=true;for(auto& p:queue_)p.promise.set_value({Code::Unavailable,"Application is shutting down",0});queue_.clear();}
}
