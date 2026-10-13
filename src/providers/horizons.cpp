#include "horizons.hpp"
#include "core/astro_time.hpp"
#include "core/time.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
namespace albert::providers {
namespace {
std::string encode(const std::string& input){constexpr char hex[]="0123456789ABCDEF";std::string out;for(unsigned char c:input){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.'||c=='_'||c=='~')out+=static_cast<char>(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
double stepSeconds(const std::string& step){
    std::istringstream stream(step);unsigned n=0;std::string unit,extra;if(!(stream>>n>>unit)||n==0||n>100000||stream>>extra)throw std::invalid_argument("Invalid Horizons step");
    const double factor=unit=="min"?60.:unit=="h"?3600.:unit=="d"?86400.:0.;if(!factor)throw std::invalid_argument("Unsupported Horizons step unit");return n*factor;
}
Query normalize(Query q){
    const double step=stepSeconds(q.step);
    if(q.target==0||std::abs(static_cast<long long>(q.target))>2000000000LL||(q.center!="500@399"&&q.center!="500@10")||!std::isfinite(q.startUtc)||!std::isfinite(q.endUtc)||q.startUtc<0||q.endUtc<=q.startUtc||q.endUtc-q.startUtc>400.*86400.||q.endUtc>1e11)throw std::invalid_argument("Invalid Horizons query");
    q.startUtc=std::floor(q.startUtc/step)*step;q.endUtc=std::ceil(q.endUtc/step)*step;
    if((q.endUtc-q.startUtc)/step>8190)throw std::length_error("Horizons query exceeds sample limit");return q;
}
double number(const std::string& text){size_t end=0;const double v=std::stod(text,&end);if(!std::isfinite(v)||text.find_first_not_of(" \t\r",end)!=std::string::npos)throw std::runtime_error("Invalid Horizons numeric field");return v;}
}
std::vector<Point> parseVectors(const std::string& text){
    const auto begin=text.find("$$SOE"),end=text.find("$$EOE");if(begin==std::string::npos||end==std::string::npos||end<=begin)throw std::runtime_error("Horizons response has no vector table");
    std::istringstream input(text.substr(begin+5,end-begin-5));std::vector<Point> values;std::string line;
    while(std::getline(input,line)){
        if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
        std::istringstream stream(line);std::vector<std::string> fields;std::string field;while(std::getline(stream,field,','))fields.push_back(field);
        if(fields.size()<8)throw std::runtime_error("Incomplete Horizons vector row");
        Point p{number(fields[0]),{number(fields[2])*1000.,number(fields[3])*1000.,number(fields[4])*1000.},{number(fields[5])*1000.,number(fields[6])*1000.,number(fields[7])*1000.}};
        if(p.jdTDB<0||p.jdTDB>1e8||(!values.empty()&&p.jdTDB<=values.back().jdTDB)||std::hypot(p.positionMeters.x,p.positionMeters.y,p.positionMeters.z)>1e15||std::hypot(p.velocityMetersPerSecond.x,p.velocityMetersPerSecond.y,p.velocityMetersPerSecond.z)>3e8)throw std::runtime_error("Invalid Horizons vector state");
        values.push_back(p);if(values.size()>8192)throw std::length_error("Horizons response exceeds sample limit");
    }
    if(values.size()<2)throw std::runtime_error("Insufficient Horizons samples");return values;
}
std::string horizonsURL(const Query& request){
    const auto q=normalize(request);
    auto epoch=[](double utc){std::ostringstream out;out<<"'JD"<<std::fixed<<std::setprecision(9)<<astro::utcToTdb(utc)<<"'";return out.str();};
    const std::pair<std::string,std::string> args[]={{"format","text"},{"COMMAND","'"+std::to_string(q.target)+"'"},{"OBJ_DATA","NO"},{"MAKE_EPHEM","YES"},{"EPHEM_TYPE","VECTORS"},{"CENTER","'"+q.center+"'"},{"REF_PLANE","FRAME"},{"REF_SYSTEM","ICRF"},{"OUT_UNITS","KM-S"},{"VEC_TABLE","2"},{"VEC_LABELS","NO"},{"VEC_CORR","NONE"},{"CSV_FORMAT","YES"},{"TIME_TYPE","TDB"},{"START_TIME",epoch(q.startUtc)},{"STOP_TIME",epoch(q.endUtc+stepSeconds(q.step))},{"STEP_SIZE","'"+q.step+"'"}};
    std::string url="https://ssd.jpl.nasa.gov/api/horizons.api?";for(const auto& [key,value]:args){if(url.back()!='?')url+='&';url+=key+'='+encode(value);}return url;
}
struct Horizons::Impl {
    struct Entry {Table table;double fetchedMono,lastUse,ttl;};
    Transport transport;Now utcNow,monotonicNow;std::mutex mutex;std::timed_mutex gate;std::map<std::string,Entry> cache;
    explicit Impl(Transport value,Now utc,Now mono):transport(std::move(value)),utcNow(std::move(utc)),monotonicNow(std::move(mono)){
        if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("Horizons HTTP initialization failed");
        if(!utcNow)utcNow=[](){return time::clock().utcSeconds();};
        if(!monotonicNow)monotonicNow=time::monotonicSeconds;
        if(!transport)transport=[](const std::string& url,DownloadProgress* p){return httpGet(url,p,{4*1024*1024,30});};
    }
    ~Impl(){curl_global_cleanup();}
};
Horizons::Horizons(Transport value,Now utc,Now mono):impl_(std::make_unique<Impl>(std::move(value),std::move(utc),std::move(mono))){}
Horizons::~Horizons()=default;
Table Horizons::query(Query request,DownloadProgress* progress){
    request=normalize(std::move(request));const auto key=horizonsURL(request);const double ttl=request.endUtc-request.startUtc>86400.?86400.:300.;
    auto cached=[&]()->std::optional<Table>{std::lock_guard lock(impl_->mutex);const auto i=impl_->cache.find(key);const double now=impl_->monotonicNow();if(i==impl_->cache.end()||now-i->second.fetchedMono>i->second.ttl)return {};i->second.lastUse=now;auto value=i->second.table;value.fromCache=true;return value;};
    if(progress)progress->checkCancelled();if(auto value=cached())return *value;
    std::unique_lock lock(impl_->gate,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(100)))if(progress)progress->checkCancelled();
    if(progress)progress->checkCancelled();if(auto value=cached())return *value;
    auto points=parseVectors(impl_->transport(key,progress));
    const double start=astro::utcToTdb(request.startUtc),end=astro::utcToTdb(request.endUtc);
    if(points.front().jdTDB>start+1e-7||points.back().jdTDB<end-1e-7)throw std::runtime_error("Horizons table does not cover requested interval");
    Table result{std::move(points),impl_->utcNow(),false,"NASA/JPL Horizons"};
    {std::lock_guard cacheLock(impl_->mutex);if(impl_->cache.size()>=32){auto oldest=std::min_element(impl_->cache.begin(),impl_->cache.end(),[](const auto& a,const auto& b){return a.second.lastUse<b.second.lastUse;});impl_->cache.erase(oldest);}const double now=impl_->monotonicNow();impl_->cache[key]={result,now,now,ttl};}
    return result;
}
Horizons& horizons(){static Horizons value;return value;}
}
