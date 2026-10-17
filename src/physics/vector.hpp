#pragma once
#include <cmath>
namespace albert::physics {
struct V3 {double x=0,y=0,z=0;};
constexpr V3 operator+(V3 a,V3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
constexpr V3 operator-(V3 a,V3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
constexpr V3 operator-(V3 a){return{-a.x,-a.y,-a.z};}
constexpr V3 operator*(V3 a,double s){return{a.x*s,a.y*s,a.z*s};}
constexpr V3 operator*(double s,V3 a){return a*s;}
constexpr V3 operator/(V3 a,double s){return{a.x/s,a.y/s,a.z/s};}
constexpr V3& operator+=(V3& a,V3 b){return a=a+b;}
constexpr double dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
constexpr V3 cross(V3 a,V3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double norm(V3 a){return std::sqrt(dot(a,a));}
// Row-major 3x3; m*v maps column vectors.
struct M3 {double m[3][3]{{1,0,0},{0,1,0},{0,0,1}};};
constexpr V3 operator*(const M3& a,V3 v){return{a.m[0][0]*v.x+a.m[0][1]*v.y+a.m[0][2]*v.z,a.m[1][0]*v.x+a.m[1][1]*v.y+a.m[1][2]*v.z,a.m[2][0]*v.x+a.m[2][1]*v.y+a.m[2][2]*v.z};}
constexpr M3 operator*(const M3& a,const M3& b){M3 r;for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j];return r;}
constexpr M3 transpose(const M3& a){M3 r;for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i][j]=a.m[j][i];return r;}
inline M3 rotationZ(double angle){const double c=std::cos(angle),s=std::sin(angle);return{{{c,s,0},{-s,c,0},{0,0,1}}};} // Frame rotation (passive).
// Unit quaternion rotating body vectors into the reference frame.
struct Q {double w=1,x=0,y=0,z=0;};
constexpr Q operator*(Q a,Q b){return{a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};}
inline Q normalized(Q q){const double n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);return{q.w/n,q.x/n,q.y/n,q.z/n};}
constexpr M3 matrix(Q q){
    const double w=q.w,x=q.x,y=q.y,z=q.z;
    return{{{1-2*(y*y+z*z),2*(x*y-w*z),2*(x*z+w*y)},{2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)},{2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)}}};
}
inline Q quaternion(const M3& r){ // Shepperd: largest-diagonal branch.
    const auto& m=r.m;const double t=m[0][0]+m[1][1]+m[2][2];Q q;
    if(t>m[0][0]&&t>m[1][1]&&t>m[2][2]){const double s=2*std::sqrt(1+t);q={s/4,(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s};}
    else if(m[0][0]>m[1][1]&&m[0][0]>m[2][2]){const double s=2*std::sqrt(1+m[0][0]-m[1][1]-m[2][2]);q={(m[2][1]-m[1][2])/s,s/4,(m[0][1]+m[1][0])/s,(m[0][2]+m[2][0])/s};}
    else if(m[1][1]>m[2][2]){const double s=2*std::sqrt(1+m[1][1]-m[0][0]-m[2][2]);q={(m[0][2]-m[2][0])/s,(m[0][1]+m[1][0])/s,s/4,(m[1][2]+m[2][1])/s};}
    else{const double s=2*std::sqrt(1+m[2][2]-m[0][0]-m[1][1]);q={(m[1][0]-m[0][1])/s,(m[0][2]+m[2][0])/s,(m[1][2]+m[2][1])/s,s/4};}
    return normalized(q.w<0?Q{-q.w,-q.x,-q.y,-q.z}:q);
}
}
