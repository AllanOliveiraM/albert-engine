#include "gravity_models.hpp"
#include "platform.hpp"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace albert::providers {
physics::GravityField parseShadr(const std::string& text,int degree,std::string name){
    if(degree<2||degree>2000)throw std::invalid_argument("Invalid gravity degree");
    std::istringstream in(text);std::string line;physics::GravityField f;f.name=std::move(name);f.degree=degree;
    const std::size_t n=static_cast<std::size_t>((degree+1)*(degree+2)/2);f.c.assign(n,0);f.s.assign(n,0);
    if(!std::getline(in,line))throw std::runtime_error("Empty gravity table");
    {std::istringstream h(line);char comma;double radius,gm;int max=0;h>>radius>>comma>>gm;double sigma;h>>comma>>sigma>>comma>>max;
        if(!h||!(radius>0&&radius<1e5)||!(gm>0&&gm<1e9))throw std::runtime_error("Malformed gravity header");if(max<degree)throw std::runtime_error("Gravity model below the requested degree");
        f.radius=radius*1e3;f.gm=gm*1e9;}
    std::size_t rows=0;
    while(std::getline(in,line)){
        std::istringstream r(line);int a,b;char comma;double c,s;if(!(r>>a>>comma>>b>>comma>>c>>comma>>s))continue;
        if(a>degree)break;if(a<1||b<0||b>a||!std::isfinite(c)||!std::isfinite(s))throw std::runtime_error("Malformed gravity row");
        const std::size_t i=static_cast<std::size_t>(a*(a+1)/2+b);f.c[i]=c;f.s[i]=s;++rows;
    }
    if(rows<n-1)throw std::runtime_error("Truncated gravity table");
    f.c[0]=1;return f;
}
GravityModels::GravityModels(std::filesystem::path cache,Transport transport):cache_(std::move(cache)),transport_(std::move(transport)){
    if(!transport_)transport_=[](const std::string& url,const std::string& range,DownloadProgress* p){return httpGet(url,p,{64*1024*1024,90,range});};
}
std::shared_ptr<const physics::Gravity> GravityModels::load(const std::string& url,const std::string& name,int degree,int maximum,DownloadProgress* progress){
    if(degree<2||degree>maximum)throw std::invalid_argument(name+" degree must be 2-"+std::to_string(maximum));
    const auto path=cache_/(name+"-"+std::to_string(degree)+".tab");std::string text;
    if(std::ifstream in{path,std::ios::binary})text.assign(std::istreambuf_iterator<char>(in),{});
    try{return std::make_shared<const physics::Gravity>(std::make_shared<const physics::GravityField>(parseShadr(text,degree,name)));}catch(const std::exception&){}
    // Fixed rows: a 243-character header, then 121 characters per (n, m) from n = 1.
    const std::size_t rows=static_cast<std::size_t>((degree+1)*(degree+2)/2)-1,bytes=244+122*rows;
    text=transport_(url,"0-"+std::to_string(bytes-1),progress);
    auto field=std::make_shared<const physics::GravityField>(parseShadr(text,degree,name));
    std::error_code ec;std::filesystem::create_directories(cache_,ec);std::ofstream(path,std::ios::binary)<<text;
    return std::make_shared<const physics::Gravity>(std::move(field));
}
std::shared_ptr<const physics::Gravity> GravityModels::moon(int degree,DownloadProgress* p){
    return load("https://pds-geosciences.wustl.edu/grail/grail-l-lgrs-5-rdr-v1/grail_1001/shadr/gggrx_1200a_sha.tab","GRGM1200A",degree,1200,p);
}
std::shared_ptr<const physics::Gravity> GravityModels::mars(int degree,DownloadProgress* p){
    return load("https://pds-geosciences.wustl.edu/mro/mro-m-rss-5-sdp-v1/mrors_1xxx/data/shadr/gmm3_120_sha.tab","GMM-3",degree,120,p);
}
GravityModels& gravityModels(){static GravityModels value(dataDirectory()/"gravity");return value;}
}
