#pragma once
#include "gpu/programs.hpp"
namespace albert {
inline void defineLaunchCrowd(){
    using gpu::Uniform;
    for(bool people:{false,true}){gpu::Program p;p.points=true;p.instanceVertices=people?102:108;
        p.attributes={{"forward",3},{"up",3},{"color",3},{"kind",1}};p.uniforms={{"eye",Uniform::vec3}};
        p.wgsl=std::string("const people = ")+(people?"true;\n":"false;\n")+R"(
struct Material { eye: vec3f };
@group(2) @binding(0) var<uniform> material: Material;
struct Varyings { @builtin(position) clip: vec4f, @location(0) world: vec3f, @location(1) normal: vec3f, @location(2) colour: vec3f, @location(3) fade: f32 };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) forward: vec3f, @location(2) up: vec3f, @location(3) colour: vec3f, @location(4) kind: f32, @builtin(vertex_index) vertex: u32) -> Varyings {
    let solid = vertex < 72u;
    let part = select(2u+(vertex-72u)/6u,vertex/36u,solid); let face = (vertex % 36u) / 6u;
    let corners = array<vec2f,6>(vec2f(-1,-1),vec2f(1,-1),vec2f(1,1),vec2f(-1,-1),vec2f(1,1),vec2f(-1,1));
    let c = corners[vertex % 6u]; let axis = face / 2u; let sign = select(-1.0,1.0,face % 2u == 0u);
    var n = vec3f(0); n[axis] = sign; var corner = n;
    corner[(axis+1u)%3u] = c.x*sign; corner[(axis+2u)%3u] = c.y;
    if (!solid) {
        if (people || kind > 4.5) { n=vec3f(0,-1,0); corner=vec3f(c.x,0,c.y); }
        else { n=vec3f(select(-1.0,1.0,part%2u==0u),0,0); corner=vec3f(0,c.x*n.x,c.y); }
        if (part == 6u) { n=vec3f(0,0,1); corner=vec3f(c,0); }
        if (part == 7u) { n=vec3f(0,1,0); corner=vec3f(-c.x,0,c.y); }
    }
    var size = vec3f(0); var at = vec3f(0); var ink = colour;
    if (people) {
        let height = 0.88 + kind * 0.065;
        let skin = mix(vec3f(0.27,0.14,0.08),vec3f(0.73,0.48,0.30),kind/3.0);
        if (part == 0u) { size=vec3f(0.43,0.27,0.58); at.z=1.12; }
        if (part == 1u) { size=vec3f(0.25,0.25,0.29); at.z=1.57; ink=skin; }
        if (part == 2u || part == 3u) { size=vec3f(0.15,0.21,0.73); at=vec3f(select(-0.12,0.12,part==3u),0,0.4); ink=vec3f(0.07,0.09,0.12); }
        if (part == 4u || part == 5u) { size=vec3f(0.12,0.18,0.56); at=vec3f(select(-0.29,0.29,part==5u),0.03,1.05); ink=skin; }
        if (part == 6u) { size=vec3f(0.32,0.31,0.06); at=vec3f(0,0.035,1.74); ink=vec3f(0.58,0.51,0.37); }
        size *= height; at *= height;
    } else if (kind > 4.5) {
        // Camera body, lens and three splayed tripod legs, in the vehicle batch.
        if (part == 0u) { size=vec3f(0.28,0.5,0.24); at.z=1.62; }
        if (part == 1u) { size=vec3f(0.16,0.22,0.17); at=vec3f(0,0.34,1.62); ink=vec3f(0.04); }
        if (part >= 2u && part <= 4u) { size=vec3f(0.045,0.045,1.5); at=vec3f(0,0,0.76); }
    } else {
        let pickup = kind < 0.5; let suv = kind > 0.5 && kind < 1.5; let van = kind > 2.5;
        let rv = kind > 3.5; let length = select(select(4.65,5.5,pickup || suv),select(6.3,8.5,rv),van);
        let width = select(select(2.0,2.3,van),2.6,rv); let roof = select(select(1.48,1.92,pickup || suv),select(2.5,3.2,rv),van);
        if (part == 0u) { size=vec3f(width,length,0.66); at.z=0.71; }
        if (part == 1u) { size=vec3f(width*0.85,select(length*0.51,length*0.83,van || suv),roof-1.03); at=vec3f(0,select(0.0,0.55,pickup),1.03+(roof-1.03)*0.5); ink=vec3f(0.055,0.10,0.135); if (van && face!=2u) { ink=colour; } }
        if (part == 6u) { size=vec3f(width*0.89,select(length*0.52,length*0.85,van || suv),0.15); at=vec3f(0,select(0.0,0.55,pickup),roof); }
        if (part >= 2u && part <= 5u) { let wheel=part-2u; size=vec3f(0.25,0.72,0.72); at=vec3f(select(-1.0,1.0,wheel%2u==0u)*width*0.49,select(-1.0,1.0,wheel<2u)*length*0.32,0.38); ink=vec3f(0.025); }
        if (part == 7u) { size=vec3f(width*0.86,0.10,0.22); at=vec3f(0,length*0.5+0.02,0.74); ink=vec3f(0.55,0.57,0.54); }

    }
    var local = at + corner * size * 0.5; var normal=n;
    if (!people && kind < 4.5 && part==1u && corner.z>0.0) { local.x*=0.95; local.y=at.y+(local.y-at.y)*0.80; }
    if (!people && kind < 0.5 && part==0u && face==4u) { ink=mix(ink,vec3f(0.08),0.55); }
    if (!people && kind > 4.5 && part>=2u && part<=4u) {
        let angle=f32(part-2u)*2.0943951; local=vec3f(local.xy+vec2f(cos(angle),sin(angle))*(1.5-local.z)*0.36,local.z);
    }
    let right=normalize(cross(forward,up)); let basis=mat3x3f(right,forward,up);
    let world=object.model*vec4f(position+basis*local,1.0);
    var o: Varyings; o.world=world.xyz; o.clip=frame.projection*frame.view*world;
    o.normal=normalize(mat3x3f(object.model[0].xyz,object.model[1].xyz,object.model[2].xyz)*basis*normal);
    o.colour=ink; o.fade=1.0;
    if (people) { o.fade=1.0-smoothstep(550.0,800.0,distance(material.eye,position)); }
    return o;
}
@fragment fn fs_main(v: Varyings,@builtin(front_facing) front: bool) -> @location(0) vec4f {
    // Stable screen-door fade keeps depth writes and avoids sorting hundreds of tiny figures.
    let bayer=array<f32,16>(0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
    let cell=vec2u(v.clip.xy)%vec2u(4u); if (v.fade <= (bayer[cell.y*4u+cell.x]+0.5)/16.0) { discard; }
    let n=normalize(v.normal)*select(-1.0,1.0,front); let light=airLight(v.world,n);
    let viewNormal=normalize((frame.view*vec4f(n,0)).xyz);
    let lit=lambertLight(v.colour,viewNormal,light.sky*frame.sunColor.rgb,light.sun);
    return vec4f(finish(airApply(v.world,lit)),1.0);
}
)";
        gpu::define(people?"albert:crowd-people":"albert:crowd-cars",std::move(p));
    }
}
}
