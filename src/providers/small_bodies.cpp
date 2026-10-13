#include "small_bodies.hpp"
#include "core/time.hpp"
#include "store.hpp"
#include "platform.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
namespace albert::providers {
namespace {
std::string encode(const std::string& value){constexpr char hex[]="0123456789ABCDEF";std::string out;for(unsigned char c:value){if(std::isalnum(c)||c=='-'||c=='.'||c=='_')out+=c;else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
void valid(const std::string& value){if(value.empty()||value.size()>40||!std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c<127;}))throw std::invalid_argument("Use a name or designation of 1..40 ASCII characters");}
}
std::timed_mutex& jplRequestGate(){static std::timed_mutex mutex;return mutex;}
SmallBodies::SmallBodies(Transport transport,std::filesystem::path directory):transport_(std::move(transport)),directory_(std::move(directory)){
    if(!transport_&&directory_.empty())directory_=dataDirectory()/"catalog";
}
std::string smallBodyCatalogId(const std::string& designation){valid(designation);std::string id="sbdb.";constexpr char hex[]="0123456789abcdef";for(unsigned char c:designation){id+=hex[c>>4];id+=hex[c&15];}return id;}
std::string smallBodyDesignation(const std::string& id){if(!id.starts_with("sbdb.")||id.size()<7||id.size()>85||(id.size()-5)%2)throw std::invalid_argument("Invalid small-body catalog ID");std::string result;for(size_t i=5;i<id.size();i+=2){unsigned value=0;const auto [p,ec]=std::from_chars(id.data()+i,id.data()+i+2,value,16);if(ec!=std::errc{}||p!=id.data()+i+2)throw std::invalid_argument("Invalid small-body catalog ID");result+=static_cast<char>(value);}valid(result);return result;}
std::vector<SmallBody> parseSmallBodies(const std::string& response){
    if(response.size()>2*1024*1024)throw std::length_error("SBDB response too large");const auto doc=nlohmann::json::parse(response);
    if(doc.at("signature").at("version")!="1.3")throw std::runtime_error("Unsupported SBDB API version; provider needs review");
    std::vector<SmallBody> result;
    if(doc.contains("object")){
        const auto& object=doc.at("object");SmallBody value{object.at("des"),object.at("fullname")};valid(value.designation);if(value.name.empty()||value.name.size()>256)throw std::runtime_error("Invalid SBDB name");
        const std::string id=object.at("spkid");const auto [p,ec]=std::from_chars(id.data(),id.data()+id.size(),value.spkId);if(ec!=std::errc{}||p!=id.data()+id.size()||value.spkId<=0)throw std::runtime_error("Invalid SBDB SPK ID");
        for(const auto& property:doc.value("phys_par",nlohmann::json::array()))if(property.value("name","")=="diameter"&&!property.at("value").is_null()){
            if(property.value("units","")!="km")throw std::runtime_error("Unsupported SBDB diameter unit");const auto text=property.at("value").get<std::string>();size_t end=0;const double diameter=std::stod(text,&end);if(end!=text.size()||!std::isfinite(diameter)||diameter<=0||diameter>10000)throw std::runtime_error("Invalid SBDB diameter");value.radiusMeters=diameter*500.;
        }result.push_back(std::move(value));
    }else if(doc.contains("list")){
        for(const auto& row:doc.at("list")){if(result.size()==50)break;SmallBody value{row.at("pdes"),row.at("name")};valid(value.designation);if(value.name.empty()||value.name.size()>256)throw std::runtime_error("Invalid SBDB name");result.push_back(std::move(value));}
    }else if(doc.value("code",0)!=200)throw std::runtime_error(doc.value("message",std::string("SBDB search unavailable")));
    return result;
}
std::vector<SmallBody> SmallBodies::search(const std::string& query,DownloadProgress* progress){
    valid(query);std::unique_lock lock(jplRequestGate(),std::defer_lock);while(!lock.try_lock_for(std::chrono::milliseconds(100)))if(progress)progress->checkCancelled();if(progress)progress->checkCancelled();
    const double now=time::monotonicSeconds();const auto found=cache_.find(query);if(found!=cache_.end()&&now-found->second.fetched<300)return found->second.values;
    const std::string url="https://ssd-api.jpl.nasa.gov/sbdb.api?sstr="+encode(query)+"&phys-par=1&no-orbit=1";
    std::vector<SmallBody> result;bool reused=false;double fetched=now;
    if(auto saved=loadResponse(directory_,url,2*1024*1024))try{
        const double age=time::clock().utcSeconds()-saved->fetchedUtc;
        if(age>=-30&&age<=300){result=parseSmallBodies(saved->payload);reused=true;fetched=now-std::max(0.,age);}
    }catch(const std::exception&){}
    if(!reused){const auto response=transport_?transport_(url,progress):httpGet(url,progress,{2*1024*1024,20});result=parseSmallBodies(response);saveResponse(directory_,url,{response,time::clock().utcSeconds()},2*1024*1024);}
    if(cache_.size()>=32){const auto old=std::min_element(cache_.begin(),cache_.end(),[](const auto& a,const auto& b){return a.second.fetched<b.second.fetched;});cache_.erase(old);}
    cache_[query]={fetched,result};return result;
}
SmallBody SmallBodies::resolve(const std::string& designation,DownloadProgress* progress){auto result=search(designation,progress);if(result.size()!=1||result.front().spkId==0)throw std::runtime_error("Select a unique SBDB designation before importing");return result.front();}
}
