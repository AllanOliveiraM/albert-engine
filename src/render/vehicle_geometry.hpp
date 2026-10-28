#pragma once
// Indexed, rigid pieces joined in vehicle-local metres. Only changed joint ranges are
// transformed; their positions also serve the ordinary shadow pass. No simulation shapes.
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <algorithm>
#include <vector>
namespace albert {
class VehicleGeometry {
    struct Piece {
        threepp::Object3D* frame;std::size_t first,count;threepp::Matrix4 previous;
        std::shared_ptr<threepp::ShaderMaterial> bell;threepp::Vector3 burn,lip,ambient;bool ready=false;
    };
    std::vector<float> positions_,normals_,colours_,finish_,weights_;
    std::vector<unsigned> indices_;std::vector<Piece> pieces_;
public:
    std::shared_ptr<threepp::Mesh> mesh;
    void add(const std::shared_ptr<threepp::BufferGeometry>& g,threepp::Object3D* frame,
             threepp::Color colour,float rough=.45f,float metal=.8f,
             std::shared_ptr<threepp::ShaderMaterial> bell={}){
        const auto* p=g->getAttribute<float>("position");const auto* n=g->getAttribute<float>("normal");
        const unsigned first=static_cast<unsigned>(positions_.size()/3);pieces_.push_back({frame,first,static_cast<std::size_t>(p->count()),{},bell,{},{},{},false});
        const float length=bell&&bell->uniforms.contains("bellLength")?bell->uniforms.at("bellLength").value<float>():1.f;
        for(int i=0;i<p->count();++i){positions_.insert(positions_.end(),{p->getX(i),p->getY(i),p->getZ(i)});
            normals_.insert(normals_.end(),{n->getX(i),n->getY(i),n->getZ(i)});colours_.insert(colours_.end(),{colour.r,colour.g,colour.b});finish_.insert(finish_.end(),{rough,metal});
            const float t=std::clamp(-p->getY(i)/length,0.f,1.f);auto smooth=[](float a,float b,float x){const float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);};
            weights_.insert(weights_.end(),{smooth(.35f,.78f,t),smooth(.87f,.99f,t)});}
        if(const auto* ix=g->getIndex())for(int i=0;i<ix->count();++i)indices_.push_back(first+static_cast<unsigned>(ix->getX(i)));
        else for(int i=0;i<p->count();++i)indices_.push_back(first+i);
    }
    void build(){
        using namespace threepp;auto g=BufferGeometry::create();g->setIndex(std::move(indices_));
        g->setAttribute("position",FloatBufferAttribute::create(positions_,3));g->setAttribute("normal",FloatBufferAttribute::create(normals_,3));
        g->setAttribute("color",FloatBufferAttribute::create(std::move(colours_),3));g->setAttribute("finish",FloatBufferAttribute::create(std::move(finish_),2));
        g->setAttribute("emission",FloatBufferAttribute::create(std::vector<float>(positions_.size()),3));
        for(const auto* name:{"position","normal","emission"})g->getAttribute<float>(name)->setUsage(DrawUsage::Dynamic);
        auto m=ShaderMaterial::create();m->name="albert:vehicle-detail";m->side=Side::Double;m->uniforms={{"waterPlane",Uniform(Vector4())}};
        mesh=Mesh::create(g,m);mesh->name="vehicle hardware";mesh->castShadow=mesh->receiveShadow=true;mesh->frustumCulled=false;update();
    }
    void update(){
        using namespace threepp;if(!mesh)return;auto* p=mesh->geometry()->getAttribute<float>("position");auto* n=mesh->geometry()->getAttribute<float>("normal");auto* e=mesh->geometry()->getAttribute<float>("emission");bool moved=false,heated=false;
        for(auto& piece:pieces_){Matrix4 matrix;if(piece.frame){piece.frame->updateMatrix();matrix.copy(*piece.frame->matrix);}
            Vector3 burn,lip,ambient;if(piece.bell){auto& u=piece.bell->uniforms;if(u.contains("burn")){burn=u.at("burn").value<Vector3>();lip=u.at("lip").value<Vector3>();}ambient=u.at("emissive").value<Vector3>();}
            const bool transform=!piece.ready||matrix.elements!=piece.previous.elements;
            const bool heat=!piece.ready||burn!=piece.burn||lip!=piece.lip||ambient!=piece.ambient;
            if(!transform&&!heat)continue;
            for(std::size_t i=piece.first;i<piece.first+piece.count;++i){if(transform){Vector3 v(positions_[3*i],positions_[3*i+1],positions_[3*i+2]);v.applyMatrix4(matrix);p->setXYZ(i,v.x,v.y,v.z);
                    v.set(normals_[3*i],normals_[3*i+1],normals_[3*i+2]);v.transformDirection(matrix);n->setXYZ(i,v.x,v.y,v.z);}
                if(heat){Vector3 v=Vector3(burn).lerp(lip,weights_[2*i+1]).multiplyScalar(weights_[2*i]).add(ambient);e->setXYZ(i,v.x,v.y,v.z);}}
            moved|=transform;heated|=heat;piece.previous=matrix;piece.burn=burn;piece.lip=lip;piece.ambient=ambient;piece.ready=true;
        }
        if(moved){p->needsUpdate();n->needsUpdate();}if(heated)e->needsUpdate();
    }
};
}
