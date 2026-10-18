#pragma once
#include "physics/vector.hpp"
#include <memory>
#include <string>
#include <vector>
namespace albert::physics {
// Fully normalised spherical-harmonic field, C̄nm and S̄nm packed at n(n+1)/2+m.
struct GravityField {std::string name;double gm=0,radius=0;int degree=0;std::vector<double> c,s;};
// Body-fixed attraction by the normalised Cunningham recursion: V̄nm = N̄nm Vnm stays bounded,
// so any degree works without factorial overflow, and the Cartesian form has no polar singularity.
class Gravity {
public:
    explicit Gravity(std::shared_ptr<const GravityField> field);
    V3 acceleration(int degree,V3 fixed)const;
    const GravityField& field()const{return *field_;}
private:
    std::shared_ptr<const GravityField> field_;
    std::vector<double> alpha_,beta_,plus_,minus_,zero_; // Recursion and output factors per (n,m).
};
const Gravity& egm2008Gravity(); // EGM2008 tide-free to degree 70.
}
