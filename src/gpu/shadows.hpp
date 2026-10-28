#pragma once
#include "threepp/math/Matrix4.hpp"
#include "threepp/math/Vector3.hpp"
#include <array>
#include <cmath>
namespace albert::gpu {
// Fixed extents preserve the texel scale during camera rotations. The anchor
// follows a physical site through floating-origin rebases, preserving grid phase.
inline std::array<threepp::Matrix4,4> sunShadowLevels(const threepp::Vector3& eye,const threepp::Vector3& sun,const threepp::Vector3& anchor,float metersPerUnit){
    using threepp::Vector3;Vector3 z=sun,x,up(0,1,0);if(std::abs(z.y)>.95f)up.set(0,0,1);x.crossVectors(up,z).normalize();Vector3 y;y.crossVectors(z,x);
    const Vector3 centre=Vector3(eye).sub(anchor);constexpr float extents[]{100,350,1100,3400};std::array<threepp::Matrix4,4> result;
    for(unsigned level=0;level<4;++level){const float extent=extents[level]/metersPerUnit,texel=2*extent/2048,depth=8000/metersPerUnit;
        const float cx=std::round(centre.dot(x)/texel)*texel,cy=std::round(centre.dot(y)/texel)*texel,cz=std::round(centre.dot(z)/texel)*texel;
        result[level].elements={x.x/extent,y.x/extent,-z.x/depth,0,x.y/extent,y.y/extent,-z.y/depth,0,x.z/extent,y.z/extent,-z.z/depth,0,
            -(cx+anchor.dot(x))/extent,-(cy+anchor.dot(y))/extent,.5f+(cz+anchor.dot(z))/depth,1};
    }
    return result;
}
}
