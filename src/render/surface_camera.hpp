#pragma once
#include "terrain.hpp"
#include <string>
#include <cmath>

namespace albert {
// A free camera near a body keeps its pose in that body's rotating frame.
// Retain the double local position between frames: re-encoding float world
// coordinates every frame accumulates drift at planetary distances.
class SurfaceCamera {
    std::string body_;
    std::array<double,3> position_{},forward_{},up_{};
    static std::array<double,3> local(const TerrainFrame& f,const threepp::Vector3& v,bool point){
        const std::array<double,3> p{v.x-(point?f.at[0]:0),v.y-(point?f.at[1]:0),v.z-(point?f.at[2]:0)};
        std::array<double,3> out{};
        for(int c=0;c<3;++c){double norm=0;for(int r=0;r<3;++r){out[c]+=f.linear[3*c+r]*p[r];norm+=std::pow(f.linear[3*c+r],2);}
            out[c]/=point?norm:std::sqrt(norm);}
        return out;
    }
    static threepp::Vector3 world(const TerrainFrame& f,const std::array<double,3>& p,bool point){
        auto out=point?f.at:std::array<double,3>{};
        for(int c=0;c<3;++c){double norm=0;for(int r=0;r<3;++r)norm+=std::pow(f.linear[3*c+r],2);
            for(int r=0;r<3;++r)out[r]+=p[c]*f.linear[3*c+r]/(point?1:std::sqrt(norm));}
        return {float(out[0]),float(out[1]),float(out[2])};
    }
public:
    void reset(){body_.clear();}
    bool follows(const std::string& id)const{return body_==id;}
    void capture(const std::string& id,const TerrainFrame& frame,const threepp::Vector3& eye,const threepp::Vector3& forward,const threepp::Vector3& up){
        body_=id;position_=local(frame,eye,true);forward_=local(frame,forward,false);up_=local(frame,up,false);
    }
    void restore(const TerrainFrame& frame,threepp::Vector3& eye,threepp::Vector3& forward,threepp::Vector3& up)const{
        eye=world(frame,position_,true);forward=world(frame,forward_,false).normalize();up=world(frame,up_,false).normalize();
    }
};
}
