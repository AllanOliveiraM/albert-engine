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
    std::size_t rows=0;std::vector<char> seen(n,0); // Every (n, m) from n = 1 exactly once.
    while(std::getline(in,line)){
        std::istringstream r(line);int a,b;char comma;double c,s;if(!(r>>a>>comma>>b>>comma>>c>>comma>>s))continue;
        if(a>degree)break;if(a<1||b<0||b>a||!std::isfinite(c)||!std::isfinite(s))throw std::runtime_error("Malformed gravity row");
        const std::size_t i=static_cast<std::size_t>(a*(a+1)/2+b);if(seen[i])throw std::runtime_error("Duplicate gravity row");seen[i]=1;f.c[i]=c;f.s[i]=s;++rows;
    }
    if(rows!=n-1)throw std::runtime_error("Truncated gravity table");
    f.c[0]=1;return f;
}
namespace {
struct Source {const char* name;const char* url;};
Source source(physics::Center body){
    if(body==physics::Center::moon)return{"GRGM1200A","https://pds-geosciences.wustl.edu/grail/grail-l-lgrs-5-rdr-v1/grail_1001/shadr/gggrx_1200a_sha.tab"};
    if(body==physics::Center::mars)return{"GMM-3","https://pds-geosciences.wustl.edu/mro/mro-m-rss-5-sdp-v1/mrors_1xxx/data/shadr/gmm3_120_sha.tab"};
    throw std::invalid_argument("Earth uses the embedded EGM2008");
}
}
int gravityMaximum(physics::Center body){return body==physics::Center::moon?600:body==physics::Center::mars?120:0;}
std::size_t gravityTableBytes(int degree){return 245+123*(static_cast<std::size_t>((degree+1)*(degree+2)/2)-1);}
GravityModel gravityModel(physics::Center body,std::string table,int degree){
    const Source s=source(body);
    if(degree<2||degree>gravityMaximum(body))throw std::invalid_argument(std::string(s.name)+" degree must be 2-"+std::to_string(gravityMaximum(body)));
    if(table.size()>gravityTableBytes(degree))throw std::runtime_error("Gravity table longer than its degree allows");
    auto field=std::make_shared<const physics::GravityField>(parseShadr(table,degree,s.name));
    return{std::make_shared<const physics::Gravity>(std::move(field)),std::move(table)};
}
GravityModels::GravityModels(std::filesystem::path cache,Transport transport):cache_(std::move(cache)),transport_(std::move(transport)){
    if(!transport_)transport_=[](const std::string& url,const std::string& range,DownloadProgress* p){return httpGet(url,p,{std::stoull(range.substr(range.find('-')+1))+1,90,range});};
}
GravityModel GravityModels::load(physics::Center body,int degree,DownloadProgress* progress){
    const Source s=source(body);
    if(degree<2||degree>gravityMaximum(body))throw std::invalid_argument(std::string(s.name)+" degree must be 2-"+std::to_string(gravityMaximum(body)));
    const auto path=cache_/(std::string(s.name)+"-"+std::to_string(degree)+".tab");std::string text;
    std::error_code size;if(const auto bytes=std::filesystem::file_size(path,size);!size&&bytes<=gravityTableBytes(degree))
        if(std::ifstream in{path,std::ios::binary})text.assign(std::istreambuf_iterator<char>(in),{});
    try{return gravityModel(body,std::move(text),degree);}catch(const std::exception&){}
    // Fixed rows: a 243-character header, then 121 characters per (n, m) from n = 1.
    const std::size_t rows=static_cast<std::size_t>((degree+1)*(degree+2)/2)-1,bytes=244+122*rows;
    GravityModel model=gravityModel(body,transport_(s.url,"0-"+std::to_string(bytes-1),progress),degree);
    std::error_code ec;std::filesystem::create_directories(cache_,ec);std::ofstream(path,std::ios::binary)<<model.table;
    return model;
}
GravityModels& gravityModels(){static GravityModels value(dataDirectory()/"gravity");return value;}
}
