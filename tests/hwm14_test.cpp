// HWM14 against the check output distributed by NRL (Check/gfortran.txt of HWM14.123114, produced by
// checkhwm14.f90): the quiet, disturbance and total winds of six profiles and three DWM07 profiles.
#include "physics/hwm14.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using albert::physics::HorizontalWind;
namespace {
int failures=0,checked=0;double worst=0;
void expect(double got,double want,const std::string& what){
    ++checked;worst=std::max(worst,std::abs(got-want));if(std::abs(got-want)<=2e-3)return; // Printed to 3 decimals; NRL's own compilers differ by 1e-3.
    if(++failures<=20)std::fprintf(stderr,"%s: %.4f vs %.3f\n",what.c_str(),got,want);
}
}
int main(int argc,char** argv){try{
    if(argc<2)throw std::runtime_error("usage: hwm14_test hwm14_check.txt");
    std::ifstream in(argv[1]);if(!in)throw std::runtime_error("cannot open the check output");
    std::string line,section;int rows=0;
    while(std::getline(in,line)){
        if(line.find("profile")!=std::string::npos){section=line.substr(line.find_first_not_of(' '));continue;}
        std::istringstream s(line);double x;if(!(s>>x))continue;std::vector<double> v;for(double y;s>>y;)v.push_back(y);
        if(section.rfind("dwm:",0)==0){
            if(v.size()!=2)continue;HorizontalWind w;
            if(section=="dwm: magnetic latitude profile")w=albert::physics::dwm07b(3,x,6);
            else if(section=="dwm: magnetic local time profile")w=albert::physics::dwm07b(x,45,6);
            else if(section=="dwm: kp profile")w=albert::physics::dwm07b(3,-50,x);
            else throw std::runtime_error("unknown section "+section);
            expect(w.north,v[0],section+" "+std::to_string(x)+" mer");expect(w.east,v[1],section+" "+std::to_string(x)+" zon");++rows;continue;
        }
        if(v.size()!=6)continue;
        int day;float sec,alt,lat,lon,ap;
        if(section=="height profile"){day=150;sec=43200;alt=float(x);lat=-45;lon=-85;ap=80;}
        else if(section=="latitude profile"){day=305;sec=64800;alt=250;lat=float(x);lon=30;ap=48;}
        else if(section=="local time profile"){day=75;alt=125;lat=45;lon=-70;ap=30;sec=(float(x)-lon/15.0f)*3600.0f;}
        else if(section=="longitude profile"){day=330;sec=21600;alt=40;lat=-5;lon=float(x);ap=4;}
        else if(section=="day of year profile"){day=int(x);sec=75600;alt=200;lat=-65;lon=-135;ap=15;}
        else if(section=="magnetic activity profile"){day=280;sec=75600;alt=350;lat=38;lon=125;ap=float(x);}
        else throw std::runtime_error("unknown section "+section);
        const auto quiet=albert::physics::hwm14(day,sec,alt,lat,lon,-1),dist=albert::physics::dwm07(day,sec,alt,lat,lon,ap),total=albert::physics::hwm14(day,sec,alt,lat,lon,ap);
        const std::string at=section+" "+std::to_string(x);
        expect(quiet.north,v[0],at+" quiet mer");expect(quiet.east,v[1],at+" quiet zon");
        expect(dist.north,v[2],at+" disturbed mer");expect(dist.east,v[3],at+" disturbed zon");
        expect(total.north,v[4],at+" total mer");expect(total.east,v[5],at+" total zon");++rows;
    }
    if(rows!=17+19+17+19+19+14+19+17+19)throw std::runtime_error("check output rows: "+std::to_string(rows));
    // Below the ground and at non-finite inputs: the ground's wind and calm, where the Fortran hangs.
    const auto ground=albert::physics::hwm14(150,43200,0,-45,-85,-1),below=albert::physics::hwm14(150,43200,-3,-45,-85,-1);
    if(below.north!=ground.north||below.east!=ground.east)throw std::runtime_error("Negative height is not the ground's");
    const auto nan=albert::physics::hwm14(150,43200,std::nan(""),-45,-85,4);if(nan.north!=0||nan.east!=0)throw std::runtime_error("NaN height");
    if(failures)throw std::runtime_error(std::to_string(failures)+" of "+std::to_string(checked)+" values differ");
    std::printf("hwm14_test: %d values in %d rows match the NRL check output (largest difference %.4f m/s)\n",checked,rows,worst);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
