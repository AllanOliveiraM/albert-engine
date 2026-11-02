#include "flight_paths.hpp"
#include "cloud_weather.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    constexpr double r=6378137+200000,mu=3.986004418e14;
    const auto predicted=albert::predictTrajectory({0,{r,0,0},{0,std::sqrt(mu/r),0}},6000,10);
    require(predicted.size()==601,"Circular prediction ended early");
    for(const auto& p:predicted)require(std::abs(std::hypot(p.position.x,p.position.y,p.position.z)-r)<2,"Circular prediction lost radius");
    const auto falling=albert::predictTrajectory({0,{6378137+1000,0,0},{-200,0,0}},1000,1);
    require(falling.size()<10,"Prediction continued inside the Earth");
    const auto target=albert::starshipTargetOrbit({0,{6378137,0,0},{}});
    require(std::abs(std::hypot(target.front().position.x,target.front().position.y,target.front().position.z)-6378137-180000)<1,"Target perigee disagrees with guidance");
    require(std::abs(std::hypot(target[128].position.x,target[128].position.y,target[128].position.z)-6378137-200000)<1,"Target apogee disagrees with guidance");
    albert::FlightHistory history;albert::core::Object flight;flight.instance=1;flight.samples={{1,{r,0,0}},{2,{r,100,0}}};history.update(flight,2);
    flight.instance=2;flight.samples={{3,{r,200,0}}};history.update(flight,3);require(history.points().size()==3,"Stage replacement discarded the flown path");
    flight.samples={{0,{r,0,0}}};history.update(flight,0);require(history.points().size()==1,"Rewind retained the old path");
    const auto image=albert::simulatedCloudImage(512,256),same=albert::simulatedCloudImage(512,256);
    require(image.data()==same.data(),"Cloud weather is not deterministic");
    double sum=0,seam=0,interior=0;unsigned clear=0,dense=0;
    for(unsigned y=0;y<256;++y){const auto& data=image.data();seam+=std::abs(int(data[(y*512)*4])-int(data[(y*512+511)*4]));
        for(unsigned x=0;x<512;++x){const int d=data[(y*512+x)*4];sum+=d;clear+=d<32;dense+=d>180;if(x)interior+=std::abs(d-int(data[(y*512+x-1)*4]));}}
    require(sum/(512*256)>35&&sum/(512*256)<190&&clear>1000&&dense>1000,"Weather lost its clear gaps or dense decks");
    require(seam/256<interior/(511*256)*3+1,"Weather has a longitude seam");
    std::cout<<"Trajectory gravity, surface stop, target orbit, history and seamless cloud weather verified\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
