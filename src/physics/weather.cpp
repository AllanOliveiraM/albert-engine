#include "physics/weather.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace albert::physics {
namespace {
constexpr double deg=3.14159265358979323846/180,dryGas=287.05,g0=9.80665,meanRadius=6371008.8;
struct Column {double temperature,logPressure,east,north,humidity,top;};
}
Weather::Weather(WeatherGrid source,std::shared_ptr<const AirModel> upper):upper_(std::move(upper)){
    const auto& g=source;const std::size_t nodes=static_cast<std::size_t>(g.rows)*static_cast<std::size_t>(g.columns),field=nodes*g.pressure.size();
    if(g.rows<2||g.columns<2||g.pressure.size()<2||g.times.empty()||!(g.step>0))throw std::invalid_argument("Incomplete weather grid");
    for(std::size_t k=1;k<g.pressure.size();++k)if(!(g.pressure[k]<g.pressure[k-1]))throw std::invalid_argument("Weather levels must decrease in pressure");
    for(std::size_t i=0;i<g.times.size();++i){const auto& t=g.times[i];
        if(t.height.size()!=field||t.temperature.size()!=field||t.east.size()!=field||t.north.size()!=field||t.humidity.size()!=field)throw std::invalid_argument("Weather field size mismatch");
        if(i&&!(t.valid>g.times[i-1].valid))throw std::invalid_argument("Weather times must increase");}
    std::vector<double> geoid(nodes);
    for(int r=0;r<g.rows;++r)for(int c=0;c<g.columns;++c)geoid[static_cast<std::size_t>(r*g.columns+c)]=geoidHeight((g.south+r*g.step)*deg,(g.west+c*g.step)*deg);
    grid_=std::make_shared<const WeatherGrid>(std::move(source));geoid_=std::make_shared<const std::vector<double>>(std::move(geoid));
}
Air Weather::at(std::int64_t tt,double latitude,double longitude,double height)const{
    auto above=[&]{return upper_?upper_->at(tt,latitude,longitude,height):standardAtmosphere(height);};const WeatherGrid& grid_=*this->grid_; // Upper air only where it weighs.
    const double x=std::remainder(longitude/deg-grid_.west,360.)/grid_.step,y=(latitude/deg-grid_.south)/grid_.step;
    const double edge=std::min({x,grid_.columns-1-x,y,grid_.rows-1-y})*grid_.step/.5;
    if(!(edge>0))return above();
    const int c0=std::min(static_cast<int>(x),grid_.columns-2),r0=std::min(static_cast<int>(y),grid_.rows-2);const double fx=x-c0,fy=y-r0;
    const std::size_t nodes=static_cast<std::size_t>(grid_.rows*grid_.columns),levels=grid_.pressure.size();
    auto node=[&](int r,int c){return static_cast<std::size_t>(r*grid_.columns+c);};
    const double weights[4]{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};const std::size_t corners[4]{node(r0,c0),node(r0,c0+1),node(r0+1,c0),node(r0+1,c0+1)};
    double n=0;for(int i=0;i<4;++i)n+=weights[i]*(*geoid_)[corners[i]];
    // Geometric height above the geoid to geopotential height (normal gravity, Somigliana).
    const double s2=std::sin(latitude)*std::sin(latitude),gamma=9.7803253359*(1+.00193185265241*s2)/std::sqrt(1-earth.e2()*s2);
    const double z=height-n,h=gamma*meanRadius*z/((meanRadius+z)*g0);
    // Time bracket (held at the ends).
    const auto& ts=grid_.times;std::size_t t1=static_cast<std::size_t>(std::upper_bound(ts.begin(),ts.end(),tt,[](std::int64_t v,const WeatherGrid::Time& t){return v<t.valid;})-ts.begin());
    const std::size_t t0=t1?t1-1:0;t1=std::min(t1,ts.size()-1);
    const double ft=t1==t0?0.:static_cast<double>(tt-ts[t0].valid)/static_cast<double>(ts[t1].valid-ts[t0].valid);
    auto column=[&](const WeatherGrid::Time& t,std::size_t at){
        auto value=[&](const std::vector<float>& f,std::size_t k){return static_cast<double>(f[k*nodes+at]);};
        const double top=value(t.height,levels-1),q=std::min(h,top);
        std::size_t k=0;while(k+2<levels&&value(t.height,k+1)<=q)++k; // Lowest pair extrapolates below the first level.
        const double h0=value(t.height,k),h1=value(t.height,k+1),f=(q-h0)/(h1-h0);
        auto lerp=[&](const std::vector<float>& v){return value(v,k)+(value(v,k+1)-value(v,k))*f;};
        return Column{lerp(t.temperature),std::log(grid_.pressure[k])+(std::log(grid_.pressure[k+1])-std::log(grid_.pressure[k]))*f,lerp(t.east),lerp(t.north),std::max(0.,lerp(t.humidity)),top};
    };
    Column m{};
    for(int tIndex=0;tIndex<2;++tIndex){const double wt=tIndex?ft:1-ft;if(wt==0)continue;
        for(int i=0;i<4;++i){const Column v=column(ts[tIndex?t1:t0],corners[i]);const double w=wt*weights[i];
            m.temperature+=w*v.temperature;m.logPressure+=w*v.logPressure;m.east+=w*v.east;m.north+=w*v.north;m.humidity+=w*v.humidity;m.top+=w*v.top;}}
    const double w=std::min(edge,1.)*std::clamp((m.top-h)/8000.,0.,1.);
    if(w<=0)return above();
    const double p=std::exp(m.logPressure),tv=m.temperature*(1+.6078*m.humidity);
    const Air g{m.temperature,p,p/(dryGas*tv),std::sqrt(1.4*dryGas*tv),{m.north,m.east,0},m.humidity};
    if(w>=1)return g;
    const Air upper=above();auto mix=[&](double a,double b){return w*a+(1-w)*b;};
    return{mix(g.temperature,upper.temperature),mix(g.pressure,upper.pressure),mix(g.density,upper.density),mix(g.sound,upper.sound),g.wind*w+upper.wind*(1-w),g.humidity*w}; // Dry above.
}
}
