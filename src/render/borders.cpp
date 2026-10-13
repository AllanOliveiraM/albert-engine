#include "borders.hpp"
#include "paths.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

using namespace threepp;
namespace {
// Preserve disconnected country rings and simplify each contiguous chain.
// The error is in scene units. Preserve closed islands even below that error.
std::vector<float> simplify(const std::vector<float>& input) {
    constexpr float toleranceSquared=.0005f*.0005f;
    std::vector<float> output;
    size_t start=0;
    while(start+5<input.size()) {
        std::vector<Vector3> chain{{input[start],input[start+1],input[start+2]}};
        do {
            chain.emplace_back(input[start+3],input[start+4],input[start+5]);start+=6;
        } while(start+5<input.size() && chain.back().equals(Vector3(input[start],input[start+1],input[start+2])));
        std::vector<bool> keep(chain.size(),false);keep.front()=keep.back()=true;
        std::vector<std::pair<size_t,size_t>> pending{{0,chain.size()-1}};
        while(!pending.empty()) {
            const auto [a,b]=pending.back();pending.pop_back();
            Vector3 delta=chain[b];delta.sub(chain[a]);const float squared=delta.lengthSq();
            float error=toleranceSquared;size_t split=0;
            for(size_t i=a+1;i<b;++i) {
                Vector3 relative=chain[i];relative.sub(chain[a]);
                const float t=squared>0 ? std::clamp(relative.dot(delta)/squared,0.f,1.f) : 0.f;
                Vector3 nearest=delta;nearest.multiplyScalar(t).add(chain[a]);
                const float distance=nearest.distanceToSquared(chain[i]);
                if(distance>error) {error=distance;split=i;}
            }
            if(split) {keep[split]=true;pending.emplace_back(a,split);pending.emplace_back(split,b);}
        }
        if(chain.front().equals(chain.back()) && std::count(keep.begin(),keep.end(),true)<4) {
            std::fill(keep.begin(),keep.end(),true);
        }
        Vector3 previous=chain.front();
        for(size_t i=1;i<chain.size();++i) if(keep[i]) {
            const auto& next=chain[i];
            output.insert(output.end(),{previous.x,previous.y,previous.z,next.x,next.y,next.z});previous=next;
        }
    }
    return output;
}
std::shared_ptr<Group> level(const std::vector<float>& endpoints,const std::shared_ptr<ShaderMaterial>& material) {
    std::map<int,std::vector<size_t>> tiles;
    for(size_t start=0;start+5<endpoints.size();start+=6) {
        Vector3 middle(endpoints[start]+endpoints[start+3],endpoints[start+1]+endpoints[start+4],endpoints[start+2]+endpoints[start+5]);
        middle.normalize();
        const double latitude=std::asin(std::clamp(middle.y,-1.f,1.f))*180./std::numbers::pi;
        const double longitude=std::atan2(-middle.z,middle.x)*180./std::numbers::pi;
        const int row=std::clamp(static_cast<int>((latitude+90)/5),0,35);
        const int column=std::clamp(static_cast<int>((longitude+180)/5),0,71);
        tiles[row*72+column].push_back(start);
    }
    std::vector<float> positions,nextPositions,sides,times,nextTimes;
    positions.reserve(endpoints.size()*3);nextPositions.reserve(endpoints.size()*3);
    sides.reserve(endpoints.size());times.reserve(endpoints.size());nextTimes.reserve(endpoints.size());
    struct Chunk {int start,count;Sphere bounds;};std::vector<Chunk> chunks;
    constexpr int ends[]={0,1,0,0,1,1};constexpr float signs[]={-1,-1,1,1,-1,1};
    for(const auto& [key,segments]:tiles) {
        (void)key;
        const int first=static_cast<int>(positions.size()/3);
        Vector3 low(2,2,2),high(-2,-2,-2);
        for(const size_t start:segments) {
            for(int end=0;end<2;++end) {
                const Vector3 p(endpoints[start+3*end],endpoints[start+3*end+1],endpoints[start+3*end+2]);
                low.min(p);high.max(p);
            }
            for(int vertex=0;vertex<6;++vertex) {
                const int end=ends[vertex];
                for(int axis=0;axis<3;++axis) {
                    positions.push_back(endpoints[start+3*end+axis]);
                    nextPositions.push_back(endpoints[start+3*(1-end)+axis]);
                }
                sides.push_back(signs[vertex]*(end ? -1.f : 1.f));
                times.push_back(static_cast<float>(end));nextTimes.push_back(static_cast<float>(1-end));
            }
        }
        Vector3 center=low;center.add(high).multiplyScalar(.5f);
        // Include displacement, the cartographic clearance and chord error.
        const float radius=low.distanceTo(high)*.5f+.005f;
        chunks.push_back({first,static_cast<int>(positions.size()/3)-first,Sphere(center,radius)});
    }
    const std::shared_ptr<BufferAttribute> p=FloatBufferAttribute::create(std::move(positions),3),q=FloatBufferAttribute::create(std::move(nextPositions),3);
    const std::shared_ptr<BufferAttribute> side=FloatBufferAttribute::create(std::move(sides),1),time=FloatBufferAttribute::create(std::move(times),1),nextTime=FloatBufferAttribute::create(std::move(nextTimes),1);
    auto group=Group::create();
    for(const auto& chunk:chunks) {
        auto geometry=BufferGeometry::create();
        geometry->setAttribute("position",p);geometry->setAttribute("nextPosition",q);
        geometry->setAttribute("lineSide",side);geometry->setAttribute("orbitTime",time);geometry->setAttribute("nextTime",nextTime);
        geometry->setDrawRange(chunk.start,chunk.count);geometry->boundingSphere=chunk.bounds;
        auto mesh=Mesh::create(geometry,material);mesh->renderOrder=2;group->add(mesh);
    }
    return group;
}
}
albert::BorderLines::BorderLines(const std::vector<float>& endpoints):style(borderMaterial()) {
    coarse_=level(simplify(endpoints),style);fine_=level(endpoints,style);
    add(coarse_);add(fine_);fine_->visible=false;
}
void albert::BorderLines::updateDetail(float distance) {
    if(distance<2.1f) detailed_=true;
    else if(distance>2.3f) detailed_=false;
    fine_->visible=detailed_;coarse_->visible=!detailed_;
}
