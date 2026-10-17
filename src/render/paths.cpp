#include "paths.hpp"
#include <cmath>
#include <numbers>

using namespace threepp;

std::shared_ptr<ShaderMaterial> albert::orbitMaterial(float horizon) {
    auto material=ShaderMaterial::create();
    material->vertexShader = R"(
        attribute float orbitTime, nextTime, lineSide;
        attribute vec3 nextPosition;
        uniform vec2 viewport;
        uniform float nearClip, strokeWidth;
        varying float vTime;
        noperspective varying float vSide;
        void main() {
            vec4 p=vec4(mat3(viewMatrix)*position,1.0);
            vec4 q=vec4(mat3(viewMatrix)*nextPosition,1.0);
            vTime=orbitTime; vSide=lineSide*(nextTime < orbitTime ? -1.0 : 1.0);
            if(p.z > -nearClip && q.z > -nearClip) {
                gl_Position=vec4(2.0,2.0,2.0,1.0);return;
            }
            if(p.z > -nearClip) {
                float fraction=(-nearClip-p.z)/(q.z-p.z);
                p=mix(p,q,fraction);vTime=mix(orbitTime,nextTime,fraction);
            }
            if(q.z > -nearClip) q=mix(q,p,(-nearClip-q.z)/(p.z-q.z));
            vec4 clip=projectionMatrix*p, other=projectionMatrix*q;
            vec2 direction=(other.xy/other.w-clip.xy/clip.w)*viewport;
            vec2 perpendicular=vec2(-direction.y,direction.x)/max(length(direction),0.0001);
            clip.xy += perpendicular*lineSide*strokeWidth/viewport*clip.w;
            gl_Position=clip;
        }
    )";
    material->fragmentShader = R"(
        uniform float present, horizon, dashDays, visibility, referenceVisibility, strokeWidth;
        varying float vTime;
        noperspective varying float vSide;
        // Integral of a periodic pulse. Averaging its footprint preserves
        // coverage even when several dashes fall inside one framebuffer pixel.
        float pulseIntegral(float value) {
            return floor(value)*0.63+min(fract(value),0.63);
        }
        void main() {
            float age = vTime - present;
            float fade = 1.0 - smoothstep(0.02, 1.0, abs(age) / horizon);
            float future = step(0.0, age);
            float phase = age / dashDays;
            float footprint = max(fwidth(phase),0.0001);
            float dash = (pulseIntegral(phase+footprint*0.5)-pulseIntegral(phase-footprint*0.5))/footprint;
            dash = mix(clamp(dash,0.0,1.0),0.63,smoothstep(0.12,0.5,footprint));
            vec3 color = mix(vec3(0.29, 0.67, 0.79), vec3(0.94, 0.70, 0.40), future);
            float edge=abs(vSide);
            float coverage=1.0-smoothstep(0.35,0.75,edge);
            coverage=max(coverage,0.12*(1.0-smoothstep(0.6,1.0,edge)));
            // A complete, readable ~1 px guide, independent of highlight width.
            float guide=referenceVisibility*.42*(1.0-smoothstep(.25,.95,edge*strokeWidth*.5));
            float highlight=coverage*fade*mix(.78,.92*dash,future);
            float alpha=guide+highlight*(1.0-guide);
            vec3 ink=mix(vec3(.32,.48,.55),color,highlight/max(alpha,.00001));
            gl_FragColor=vec4(ink,visibility*alpha);
            #include <encodings_fragment>
        }
    )";
    material->uniforms.emplace("referenceVisibility",Uniform(0.f));
    material->uniforms.emplace("strokeWidth",Uniform(4.f));
    material->uniforms.emplace("present", Uniform(0.f));
    material->uniforms.emplace("viewport",Uniform(Vector2(1280,720)));
    material->uniforms.emplace("nearClip",Uniform(.01f));
    material->uniforms.emplace("dashDays",Uniform(.25f));
    material->uniforms.emplace("horizon",Uniform(horizon));
    material->uniforms.emplace("visibility",Uniform(1.f));
    material->transparent=true;
    material->depthTest=true;
    material->depthWrite=false;
    material->toneMapped=false;
    material->side=Side::Double;
    material->forceSinglePass=true;
    return material;
}

std::shared_ptr<ShaderMaterial> albert::borderMaterial() {
    auto material=ShaderMaterial::create();
    material->vertexShader=R"(
        attribute vec3 nextPosition;
        attribute float lineSide, orbitTime, nextTime;
        uniform vec2 viewport;
        uniform float nearClip, surfaceOffset, detailEnabled, clampSurface, widthPixels;
        uniform sampler2D heightMap, detailHeightMap;
        uniform vec4 detailBounds;
        noperspective varying float vSide;
        vec3 surface(vec3 point) {
            if(clampSurface<0.5)return point;
            const vec3 axes=vec3(1.0,6356.752314245/6378.137,1.0);
            vec3 radial=normalize(point/axes);
            float latitude=atan(radial.y,axes.y*length(radial.xz));
            vec2 uv=vec2(atan(-radial.z,radial.x)/6.28318530718+0.5,latitude/3.14159265359+0.5);
            float height=texture2D(heightMap,uv).r;
            vec2 local=vec2(fract(uv.x-detailBounds.x),uv.y-detailBounds.y)/detailBounds.zw;
            if(detailEnabled>0.5 && all(greaterThanEqual(local,vec2(0.0))) && all(lessThanEqual(local,vec2(1.0)))) {
                float detailed=texture2D(detailHeightMap,local).r;
                vec2 edge=min(local,1.0-local);
                // The coarse globe is masked only inside the patch core.
                height=min(edge.x,edge.y)>0.025 ? detailed : max(height,detailed);
            }
            return radial*axes*(1.0+height/6378137.0+surfaceOffset);
        }
        void main() {
            vec4 p=modelViewMatrix*vec4(surface(position),1.0);
            vec4 q=modelViewMatrix*vec4(surface(nextPosition),1.0);
            vSide=lineSide*(nextTime<orbitTime ? -1.0 : 1.0);
            if(p.z>-nearClip && q.z>-nearClip) {gl_Position=vec4(2.0,2.0,2.0,1.0);return;}
            if(p.z>-nearClip) p=mix(p,q,(-nearClip-p.z)/(q.z-p.z));
            if(q.z>-nearClip) q=mix(q,p,(-nearClip-q.z)/(p.z-q.z));
            vec4 clip=projectionMatrix*p,other=projectionMatrix*q;
            vec2 direction=(other.xy/other.w-clip.xy/clip.w)*viewport;
            vec2 normal=vec2(-direction.y,direction.x)/max(length(direction),0.0001);
            clip.xy+=normal*lineSide*widthPixels/viewport*clip.w;
            gl_Position=clip;
        }
    )";
    material->fragmentShader=R"(
        uniform float visibility;
        uniform vec3 lineColor;
        noperspective varying float vSide;
        void main() {
            float coverage=1.0-smoothstep(0.3,0.95,abs(vSide));
            gl_FragColor=vec4(lineColor,coverage*0.75*visibility);
            #include <encodings_fragment>
        }
    )";
    material->uniforms.emplace("viewport",Uniform(Vector2(1280,720)));
    material->uniforms.emplace("nearClip",Uniform(.01f));
    material->uniforms.emplace("visibility",Uniform(0.f));
    material->uniforms.emplace("clampSurface",Uniform(1.f));
    material->uniforms.emplace("widthPixels",Uniform(3.f));
    material->uniforms.emplace("lineColor",Uniform(Vector3(.60f,.73f,.82f)));
    material->uniforms.emplace("surfaceOffset",Uniform(.00005f));
    material->uniforms.emplace("heightMap",Uniform(static_cast<Texture*>(nullptr)));
    material->uniforms.emplace("detailHeightMap",Uniform(static_cast<Texture*>(nullptr)));
    material->uniforms.emplace("detailBounds",Uniform(Vector4(0,0,1,1)));
    material->uniforms.emplace("detailEnabled",Uniform(0.f));
    material->transparent=true;
    material->depthTest=true;
    material->depthWrite=false;
    material->toneMapped=false;
    material->side=Side::Double;
    material->forceSinglePass=true;
    return material;
}

double albert::revolutionDays(std::span<const PathPoint> points,const std::array<double,3>& normal){
    const double length=std::hypot(normal[0],normal[1],normal[2]);
    if(points.size()<3||!(length>0))return 0;
    const double n[]={normal[0]/length,normal[1]/length,normal[2]/length};
    // In-plane basis from the first point.
    const auto& first=points.front();const double along=first.x*n[0]+first.y*n[1]+first.z*n[2];
    double u[]={first.x-along*n[0],first.y-along*n[1],first.z-along*n[2]};const double size=std::hypot(u[0],u[1],u[2]);
    if(!(size>0))return 0;
    for(auto& value:u)value/=size;
    const double v[]={n[1]*u[2]-n[2]*u[1],n[2]*u[0]-n[0]*u[2],n[0]*u[1]-n[1]*u[0]};
    auto angle=[&](const PathPoint& p){return std::atan2(p.x*v[0]+p.y*v[1]+p.z*v[2],p.x*u[0]+p.y*u[1]+p.z*u[2]);};
    constexpr double turn=2*std::numbers::pi;double swept=0,previous=angle(first);
    for(size_t i=1;i<points.size();++i){
        const double current=angle(points[i]),step=std::remainder(current-previous,turn);previous=current;
        if(swept+step>=turn&&step>0)return std::lerp(points[i-1].day,points[i].day,(turn-swept)/step)-first.day;
        swept+=step;
    }
    return 0;
}

double albert::pathPresent(double days,double halfWidth,double period){
    if(std::abs(days)<=halfWidth||!(period>0))return days;
    return std::abs(days)<=halfWidth+3*period?std::remainder(days,period):1e7;
}
