#include "gfs.hpp"
#include "platform.hpp"
#include "core/simulation.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
namespace albert::providers {
namespace {
std::uint64_t be(std::string_view b,std::size_t at,int n){std::uint64_t v=0;for(int i=0;i<n;++i)v=v<<8|static_cast<unsigned char>(b.at(at+static_cast<std::size_t>(i)));return v;}
std::int64_t signedValue(std::string_view b,std::size_t at,int n){const std::uint64_t v=be(b,at,n),sign=std::uint64_t{1}<<(8*n-1);return v&sign?-static_cast<std::int64_t>(v&~sign):static_cast<std::int64_t>(v);} // Sign and magnitude.
constexpr const char* levels[]{"0.01","0.02","0.04","0.07","0.1","0.2","0.4","0.7","1","2","3","5","7","10","15","20","30","40","50","70","100","150","200","250","300","350","400","450","500","550","600","650","700","750","800","850","900","925","950","975","1000"};
}
std::vector<GribField> decodeGrib2(std::string_view b){
    std::vector<GribField> out;std::size_t at=0;
    while(at+16<=b.size()){
        if(b.substr(at,4)!="GRIB"||b[at+7]!=2)throw std::runtime_error("Not a GRIB2 message");
        const std::size_t total=be(b,at+8,8),end=at+total;if(total<16||end>b.size())throw std::runtime_error("Truncated GRIB2 message");
        GribField f;f.discipline=static_cast<unsigned char>(b[at+6]);int bits=0;double r=0,e=0,d=0;bool grid=false,product=false,packing=false;
        for(std::size_t s=at+16;s+5<=end-4;){
            const std::size_t length=be(b,s,4);const int number=static_cast<unsigned char>(b[s+4]);
            if(length<5||s+length>end)throw std::runtime_error("Malformed GRIB2 section");
            if(number==1){time::Civil c{static_cast<int>(be(b,s+12,2)),static_cast<int>(be(b,s+14,1)),static_cast<int>(be(b,s+15,1)),static_cast<int>(be(b,s+16,1)),static_cast<int>(be(b,s+17,1)),static_cast<int>(be(b,s+18,1))};f.reference=time::unixSeconds(c);}
            else if(number==3){
                if(be(b,s+12,2)!=0||be(b,s+6,4)==0)throw std::runtime_error("Unsupported GRIB2 grid (template 3.0 only)");
                f.ni=static_cast<int>(be(b,s+30,4));f.nj=static_cast<int>(be(b,s+34,4));f.la1=signedValue(b,s+46,4)*1e-6;f.lo1=signedValue(b,s+50,4)*1e-6;
                f.di=static_cast<double>(be(b,s+63,4))*1e-6;f.dj=static_cast<double>(be(b,s+67,4))*1e-6;f.scan=static_cast<unsigned char>(b[s+71]);grid=true;
            }else if(number==4){
                const auto productTemplate=be(b,s+7,2);
                if(productTemplate!=0&&productTemplate!=8)throw std::runtime_error("Unsupported GRIB2 product (template 4.0 or cloud 4.8 only)");
                f.category=static_cast<unsigned char>(b[s+9]);f.number=static_cast<unsigned char>(b[s+10]);
                const int unit=static_cast<unsigned char>(b[s+17]);const std::int64_t value=signedValue(b,s+18,4);
                if(unit!=0&&unit!=1)throw std::runtime_error("Unsupported GRIB2 forecast time unit");
                f.forecastMinutes=static_cast<int>(unit==1?value*60:value);f.surface=static_cast<unsigned char>(b[s+22]);
                f.level=static_cast<double>(be(b,s+24,4))*std::pow(10.,-static_cast<double>(signedValue(b,s+23,1)));
                if(productTemplate==8){
                    if(length<58||f.category!=6||f.number!=1||f.surface!=214||be(b,s+41,1)!=1||be(b,s+46,1)!=0)throw std::runtime_error("Unsupported GRIB2 statistical field");
                    const time::Civil endTime{static_cast<int>(be(b,s+34,2)),static_cast<int>(be(b,s+36,1)),static_cast<int>(be(b,s+37,1)),static_cast<int>(be(b,s+38,1)),static_cast<int>(be(b,s+39,1)),static_cast<int>(be(b,s+40,1))};
                    f.forecastMinutes=static_cast<int>((time::unixSeconds(endTime)-f.reference)/60); // Average attributed to its interval end, for drawing only.
                }
                product=true;
            }else if(number==5){
                if(be(b,s+9,2)!=0)throw std::runtime_error("Unsupported GRIB2 packing (template 5.0 only)");
                if(be(b,s+5,4)!=static_cast<std::uint64_t>(f.ni)*static_cast<std::uint64_t>(f.nj))throw std::runtime_error("GRIB2 point count mismatch");
                if(static_cast<std::uint64_t>(f.ni)*static_cast<std::uint64_t>(f.nj)>(1u<<22))throw std::runtime_error("GRIB2 grid too large"); // A global 0.25° grid has 1.04 M points.
                r=std::bit_cast<float>(static_cast<std::uint32_t>(be(b,s+11,4)));e=std::ldexp(1.,static_cast<int>(signedValue(b,s+15,2)));d=std::pow(10.,-static_cast<double>(signedValue(b,s+17,2)));bits=static_cast<unsigned char>(b[s+19]);packing=true;
                if(bits>32)throw std::runtime_error("Unsupported GRIB2 bit width");
            }else if(number==6){if(static_cast<unsigned char>(b[s+5])!=255)throw std::runtime_error("GRIB2 bitmaps are not supported");}
            else if(number==7){
                if(!grid||!product||!packing)throw std::runtime_error("GRIB2 data before its definition");
                const std::size_t n=static_cast<std::size_t>(f.ni)*static_cast<std::size_t>(f.nj);
                if(bits>0&&(length-5)*8<n*static_cast<std::size_t>(bits))throw std::runtime_error("Truncated GRIB2 data");
                f.values.resize(n);std::size_t bit=(s+5)*8;
                for(std::size_t i=0;i<n;++i){std::uint64_t x=0;for(int k=0;k<bits;++k,++bit)x=x<<1|((static_cast<unsigned char>(b[bit/8])>>(7-bit%8))&1u);
                    f.values[i]=static_cast<float>((r+static_cast<double>(x)*e)*d);}
                out.push_back(f);
            }
            s+=length;
        }
        at=end;
    }
    return out;
}
physics::WeatherGrid gfsGrid(const std::vector<std::vector<GribField>>& hours,std::string source){
    physics::WeatherGrid g;g.source=std::move(source);
    const GribField* first=nullptr;std::vector<double> pressure;
    for(const auto& f:hours.at(0))if(f.discipline==0&&f.surface==100&&f.category==0&&f.number==0){pressure.push_back(f.level);first=&f;}
    if(!first||pressure.size()<2)throw std::runtime_error("GFS response without isobaric temperatures");
    std::sort(pressure.begin(),pressure.end(),std::greater<>());g.pressure=pressure;
    if(first->scan!=64&&first->scan!=0)throw std::runtime_error("Unsupported GRIB2 scanning mode");
    g.rows=first->nj;g.columns=first->ni;g.step=first->di;
    g.south=first->scan==64?first->la1:first->la1-(first->nj-1)*first->dj;g.west=std::remainder(first->lo1,360.);
    const std::size_t nodes=static_cast<std::size_t>(g.rows*g.columns),levels=pressure.size();
    for(const auto& hour:hours){
        physics::WeatherGrid::Time t;double valid=-1;
        std::vector<float>* fields[]{&t.height,&t.temperature,&t.east,&t.north,&t.humidity};for(auto* v:fields)v->assign(nodes*levels,std::nanf(""));
        for(const auto& f:hour){
            if(f.discipline!=0||f.surface!=100)continue;
            std::vector<float>* target=f.category==3&&f.number==5?&t.height:f.category==0&&f.number==0?&t.temperature:f.category==2&&f.number==2?&t.east:f.category==2&&f.number==3?&t.north:f.category==1&&f.number==0?&t.humidity:nullptr;
            if(!target)continue;
            const auto k=std::find(pressure.begin(),pressure.end(),f.level);if(k==pressure.end())continue;
            if(f.ni!=g.columns||f.nj!=g.rows||f.scan!=first->scan||std::abs(f.la1-first->la1)>1e-9||std::abs(f.lo1-first->lo1)>1e-9)throw std::runtime_error("GFS fields on different grids");
            const double v=f.reference+f.forecastMinutes*60.;if(valid>=0&&v!=valid)throw std::runtime_error("GFS fields of different instants in one hour");valid=v;
            const std::size_t base=static_cast<std::size_t>(k-pressure.begin())*nodes;
            for(int r=0;r<g.rows;++r){const int source=f.scan==64?r:g.rows-1-r;
                std::copy_n(f.values.begin()+static_cast<std::ptrdiff_t>(source*g.columns),g.columns,target->begin()+static_cast<std::ptrdiff_t>(base+static_cast<std::size_t>(r*g.columns)));}
        }
        for(auto* v:fields)if(std::any_of(v->begin(),v->end(),[](float x){return std::isnan(x);}))throw std::runtime_error("GFS response is missing levels or variables");
        t.valid=physics::ttFromUtc(valid);g.times.push_back(std::move(t));
    }
    std::sort(g.times.begin(),g.times.end(),[](const auto& a,const auto& b){return a.valid<b.valid;});
    return g; // Isobaric levels are encoded in Pa.
}
double gfsLowCloudCover(const std::vector<GribField>& fields,double lat,double lon,double utc){
    double before=-1,after=-1,t0=-1e30,t1=1e30;
    for(const auto& f:fields){
        if(f.discipline!=0||f.category!=6||f.number!=1||f.surface!=214||f.ni<2||f.nj<2||f.di<=0||f.dj<=0||(f.scan!=0&&f.scan!=64))continue;
        const double x=std::remainder(lon-f.lo1,360.)/f.di,y=(lat-f.la1)/f.dj*(f.scan==64?1:-1);
        if(!(x>=0&&y>=0&&x<=f.ni-1&&y<=f.nj-1)||f.values.size()!=static_cast<std::size_t>(f.ni*f.nj))continue;
        const int ix=std::min(static_cast<int>(x),f.ni-2),iy=std::min(static_cast<int>(y),f.nj-2);
        const auto value=[&](int dx,int dy){return static_cast<double>(f.values[static_cast<std::size_t>((iy+dy)*f.ni+ix+dx)]);};
        const double v=std::lerp(std::lerp(value(0,0),value(1,0),x-ix),std::lerp(value(0,1),value(1,1),x-ix),y-iy)/100.;
        if(!std::isfinite(v))continue;
        const double t=f.reference+f.forecastMinutes*60.;
        if(t<=utc&&t>t0){t0=t;before=v;}if(t>=utc&&t<t1){t1=t;after=v;}
    }
    if(before<0||after<0)return -1; // Outside the downloaded hours: never use stale cloud cover.
    return std::clamp(t1>t0?std::lerp(before,after,(utc-t0)/(t1-t0)):before,0.,1.);
}
Gfs::Gfs(std::filesystem::path cache,Transport transport):cache_(std::move(cache)),transport_(std::move(transport)){
    if(!transport_)transport_=[](const std::string& url,DownloadProgress* p){return httpGet(url,p,{32*1024*1024,90});};
}
Gfs::Download Gfs::fetch(double lat,double lon,double utc,DownloadProgress* progress){
    if(!(std::abs(lat)<=90)||!std::isfinite(lon)||!std::isfinite(utc))throw std::invalid_argument("Invalid weather request");
    std::lock_guard lock(mutex_);
    const double clat=std::round(lat*4)/4,clon=std::round(std::remainder(lon,360.)*4)/4;
    const double south=std::max(-90.,clat-6),north=std::min(90.,clat+6),west=clon-6,east=clon+6;
    auto stamp=[](double t,bool hour){const auto c=time::civil(t);char s[16];std::snprintf(s,sizeof s,hour?"%04d%02d%02d%02d":"%04d%02d%02d",c.year,c.month,c.day,c.hour);return std::string(s);};
    const double cycle0=std::floor(utc/21600.)*21600.;std::string last;
    auto complete=[](const std::vector<GribField>& f){try{return !f.empty()&&!gfsGrid({f},{}).times.empty();}catch(const std::exception&){return false;}};
    for(int back=0;back<5;++back){
        const double cycle=cycle0-back*21600.;const int k=static_cast<int>(std::floor((utc-cycle)/3600.));
        if(k<0||k+1>120)continue;
        const std::string day=stamp(cycle,false),hh=stamp(cycle,true).substr(8,2);
        auto path=[&](int f){std::ostringstream key;key<<"gfs-"<<day<<hh<<"-f"<<f<<"-"<<south<<"_"<<west<<".grb2";return cache_/key.str();};
        std::vector<std::vector<GribField>> hours(2);std::vector<std::string> raw(2);
        for(int i=0;i<2;++i){std::string bytes;if(std::ifstream in{path(k+i),std::ios::binary})bytes.assign(std::istreambuf_iterator<char>(in),{});
            try{hours[static_cast<std::size_t>(i)]=decodeGrib2(bytes);}catch(const std::exception&){}
            if(!complete(hours[static_cast<std::size_t>(i)]))hours[static_cast<std::size_t>(i)].clear();else raw[static_cast<std::size_t>(i)]=std::move(bytes);}
        if(hours[0].empty()||hours[1].empty()){ // Not cached completely: needs the cycle published.
            char name[64];std::snprintf(name,sizeof name,"gfs.t%sz.pgrb2.0p25.f%03d",hh.c_str(),k+1);
            try{transport_("https://noaa-gfs-bdp-pds.s3.amazonaws.com/gfs."+day+"/"+hh+"/atmos/"+name+".idx",progress);}catch(const std::exception& e){last=e.what();continue;}
            for(int i=0;i<2;++i){if(!hours[static_cast<std::size_t>(i)].empty())continue;
                char file[64];std::snprintf(file,sizeof file,"gfs.t%sz.pgrb2.0p25.f%03d",hh.c_str(),k+i);
                std::ostringstream url;url<<"https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_0p25.pl?dir=%2Fgfs."<<day<<"%2F"<<hh<<"%2Fatmos&file="<<file<<"&var_HGT=on&var_TMP=on&var_UGRD=on&var_VGRD=on&var_SPFH=on";
                for(const char* l:levels)url<<"&lev_"<<l<<"_mb=on";
                url<<"&subregion=&toplat="<<north<<"&leftlon="<<west<<"&rightlon="<<east<<"&bottomlat="<<south;
                std::string bytes=transport_(url.str(),progress);auto fields=decodeGrib2(bytes); // Throws on an error page.
                if(!complete(fields))throw std::runtime_error("Incomplete GFS response");
                // Optional drawing metadata. Its availability/encoding must never prevent a flight using its weather.
                try{
                    std::ostringstream cloudUrl;cloudUrl<<"https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_0p25.pl?dir=%2Fgfs."<<day<<"%2F"<<hh<<"%2Fatmos&file="<<file
                        <<"&var_TCDC=on&lev_low_cloud_layer=on&subregion=&toplat="<<north<<"&leftlon="<<west<<"&rightlon="<<east<<"&bottomlat="<<south;
                    const auto cloudBytes=transport_(cloudUrl.str(),progress);auto clouds=decodeGrib2(cloudBytes);
                    if(!clouds.empty()&&std::all_of(clouds.begin(),clouds.end(),[](const auto& f){return f.discipline==0&&f.category==6&&f.number==1&&f.surface==214;})){
                        bytes+=cloudBytes;for(auto& f:clouds)fields.push_back(std::move(f));
                    }
                }catch(const std::exception&){if(progress)progress->checkCancelled();}
                std::error_code ec;std::filesystem::create_directories(cache_,ec);
                if(!ec){const auto partial=path(k+i).string()+".tmp";{std::ofstream o{partial,std::ios::binary};o.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}std::filesystem::rename(partial,path(k+i),ec);}
                hours[static_cast<std::size_t>(i)]=std::move(fields);raw[static_cast<std::size_t>(i)]=bytes;
            }
        }
        std::error_code ec; // Drop downloads older than two days.
        for(const auto& entry:std::filesystem::directory_iterator(cache_,ec))
            if(entry.path().extension()==".grb2"&&std::filesystem::last_write_time(entry,ec)<std::filesystem::file_time_type::clock::now()-std::chrono::hours(48))std::filesystem::remove(entry,ec);
        char source[160];std::snprintf(source,sizeof source,"NOAA GFS 0.25° %.4s-%.2s-%.2s %sZ (+%d/+%d h), %.2f..%.2f N, %.2f..%.2f E",day.c_str(),day.c_str()+4,day.c_str()+6,hh.c_str(),k,k+1,south,north,west,east);
        return{std::make_shared<const physics::Weather>(gfsGrid(hours,source)),std::move(raw)};
    }
    throw std::runtime_error("No published GFS cycle covers this instant ("+last+")");
}
Gfs& gfs(){static Gfs value(dataDirectory()/"weather");return value;}
}
