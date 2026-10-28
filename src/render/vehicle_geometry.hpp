#pragma once
// Indexed, rigid pieces joined in vehicle-local metres and posed on the GPU: each vertex carries its piece, and each frame only
// the pieces' poses and heat go up (six texels a piece), not the batch's positions, normals and glow (~12 MB a frame while
// the bells gimbal). The shadow pass sees the pieces at rest. No simulation shapes.
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/textures/DataTexture.hpp"
#include <algorithm>
#include <vector>
namespace albert {
class VehicleGeometry {
    struct Piece {threepp::Object3D* frame;std::shared_ptr<threepp::ShaderMaterial> bell;};
    std::vector<float> positions_,normals_,colours_,finish_,weights_,piece_;
    std::vector<unsigned> indices_;std::vector<Piece> pieces_;std::shared_ptr<threepp::DataTexture> poses_;
public:
    static constexpr unsigned texels=6; // A piece's row: its pose's three rows (vehicle-local), then its bell's burn, lip and ambient glow.
    std::shared_ptr<threepp::Mesh> mesh;
    void add(const std::shared_ptr<threepp::BufferGeometry>& g,threepp::Object3D* frame,
             threepp::Color colour,float rough=.45f,float metal=.8f,
             std::shared_ptr<threepp::ShaderMaterial> bell={}){
        const auto* p=g->getAttribute<float>("position");const auto* n=g->getAttribute<float>("normal");const auto* c=g->getAttribute<float>("color");
        const unsigned first=static_cast<unsigned>(positions_.size()/3);const float index=static_cast<float>(pieces_.size());pieces_.push_back({frame,bell});
        const float length=bell&&bell->uniforms.contains("bellLength")?bell->uniforms.at("bellLength").value<float>():1.f;
        for(int i=0;i<p->count();++i){positions_.insert(positions_.end(),{p->getX(i),p->getY(i),p->getZ(i)});
            normals_.insert(normals_.end(),{n->getX(i),n->getY(i),n->getZ(i)});colours_.insert(colours_.end(),{colour.r*(c?c->getX(i):1.f),colour.g*(c?c->getY(i):1.f),colour.b*(c?c->getZ(i):1.f)});finish_.insert(finish_.end(),{rough,metal});
            const float t=std::clamp(-p->getY(i)/length,0.f,1.f);auto smooth=[](float a,float b,float x){const float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);};
            weights_.insert(weights_.end(),{smooth(.35f,.78f,t),smooth(.87f,.99f,t)});piece_.push_back(index);}
        if(const auto* ix=g->getIndex())for(int i=0;i<ix->count();++i)indices_.push_back(first+static_cast<unsigned>(ix->getX(i)));
        else for(int i=0;i<p->count();++i)indices_.push_back(first+i);
    }
    void build(){
        using namespace threepp;auto g=BufferGeometry::create();g->setIndex(std::move(indices_));
        g->setAttribute("position",FloatBufferAttribute::create(std::move(positions_),3));g->setAttribute("normal",FloatBufferAttribute::create(std::move(normals_),3));
        g->setAttribute("color",FloatBufferAttribute::create(std::move(colours_),3));g->setAttribute("finish",FloatBufferAttribute::create(std::move(finish_),2));
        g->setAttribute("weights",FloatBufferAttribute::create(std::move(weights_),2));g->setAttribute("piece",FloatBufferAttribute::create(std::move(piece_),1));
        poses_=DataTexture::create(ImageData(std::vector<float>(4*texels*std::max<std::size_t>(pieces_.size(),1))),texels,static_cast<unsigned>(std::max<std::size_t>(pieces_.size(),1)));
        poses_->type=Type::Float;poses_->minFilter=poses_->magFilter=Filter::Nearest;poses_->generateMipmaps=false;
        auto m=ShaderMaterial::create();m->name="albert:vehicle-detail";m->side=Side::Double;m->uniforms={{"waterPlane",Uniform(Vector4())},{"poses",Uniform(static_cast<Texture*>(poses_.get()))}};
        mesh=Mesh::create(g,m);mesh->name="vehicle hardware";mesh->castShadow=mesh->receiveShadow=true;mesh->frustumCulled=false;update(true);
    }
    // The pieces' poses and heat into the texture (both LOD tiers: a few kilobytes), sent only when one changed.
    void update(bool force=false){
        using namespace threepp;if(!mesh)return;auto& d=poses_->image().data<float>();bool changed=false;
        for(std::size_t k=0;k<pieces_.size();++k){const auto& piece=pieces_[k];Matrix4 matrix;if(piece.frame){piece.frame->updateMatrix();matrix.copy(*piece.frame->matrix);}
            Vector3 burn,lip,ambient;if(piece.bell){auto& u=piece.bell->uniforms;if(u.contains("burn")){burn=u.at("burn").value<Vector3>();lip=u.at("lip").value<Vector3>();}ambient=u.at("emissive").value<Vector3>();}
            const auto& e=matrix.elements; // Column-major: row r is e[r], e[4+r], e[8+r], e[12+r].
            const float row[4*texels]{e[0],e[4],e[8],e[12],e[1],e[5],e[9],e[13],e[2],e[6],e[10],e[14],burn.x,burn.y,burn.z,0,lip.x,lip.y,lip.z,0,ambient.x,ambient.y,ambient.z,0};
            float* at=d.data()+4*texels*k;if(!std::equal(row,row+4*texels,at)){std::copy(row,row+4*texels,at);changed=true;}}
        if(changed||force)poses_->needsUpdate();
    }
};
}
