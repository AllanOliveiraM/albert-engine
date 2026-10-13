#include "model.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
namespace albert::core {
namespace {
using J=nlohmann::json;using Matrix=std::array<double,16>;
void need(bool condition,const char* message){if(!condition)throw std::invalid_argument(message);}
std::uint32_t u32(std::span<const unsigned char> b,std::size_t i){need(i<=b.size()&&b.size()-i>=4,"Truncated GLB");return std::uint32_t(b[i])|(std::uint32_t(b[i+1])<<8)|(std::uint32_t(b[i+2])<<16)|(std::uint32_t(b[i+3])<<24);}
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix multiply(const Matrix& a,const Matrix& b){Matrix r{};for(int c=0;c<4;++c)for(int row=0;row<4;++row)for(int k=0;k<4;++k)r[c*4+row]+=a[k*4+row]*b[c*4+k];return r;}
double finite(const J& v){need(v.is_number(),"Expected numeric model value");double n=v.get<double>();need(std::isfinite(n)&&std::abs(n)<=1e8,"Invalid model value");return n;}
Matrix transform(const J& node){
    if(node.contains("matrix")){need(!node.contains("translation")&&!node.contains("rotation")&&!node.contains("scale"),"GLB matrix and TRS cannot coexist");const auto& v=node.at("matrix");need(v.is_array()&&v.size()==16,"Invalid model matrix");Matrix r;for(int i=0;i<16;++i)r[i]=finite(v[i]);need(r[3]==0&&r[7]==0&&r[11]==0&&r[15]==1,"Only affine model matrices are supported");return r;}
    Matrix r=identity();if(node.contains("rotation")){const auto& v=node.at("rotation");need(v.is_array()&&v.size()==4,"Invalid model rotation");double x=finite(v[0]),y=finite(v[1]),z=finite(v[2]),w=finite(v[3]),n=std::hypot(std::hypot(x,y),std::hypot(z,w));need(n>1e-12,"Null model quaternion");x/=n;y/=n;z/=n;w/=n;
        r={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,0,0,0,1};}
    if(node.contains("scale")){const auto& v=node.at("scale");need(v.is_array()&&v.size()==3,"Invalid model scale");for(int c=0;c<3;++c){const double s=finite(v[c]);need(std::abs(s)>1e-12,"Degenerate model scale");for(int row=0;row<3;++row)r[c*4+row]*=s;}}
    if(node.contains("translation")){const auto& v=node.at("translation");need(v.is_array()&&v.size()==3,"Invalid model translation");for(int i=0;i<3;++i)r[12+i]=finite(v[i]);}return r;
}
Vector cross(Vector a,Vector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double dot(Vector a,Vector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct Accessor {std::span<const unsigned char> bytes;std::size_t count,stride,offset;unsigned type;};
Accessor accessor(const J& root,std::span<const unsigned char> data,int index,bool vector){
    const auto& a=root.at("accessors").at(index);need(!a.contains("sparse")&&!a.value("normalized",false),"Sparse/normalized model accessors are not supported");
    const unsigned type=a.at("componentType").get<unsigned>();need(vector?(type==5126&&a.at("type")=="VEC3"):(a.at("type")=="SCALAR"&&(type==5121||type==5123||type==5125)),"Unsupported model accessor format");
    const std::size_t component=type==5121?1:type==5123?2:4,width=component*(vector?3:1),count=a.at("count").get<std::size_t>();need(count>0&&count<=(vector?maxModelVertices:maxModelTriangles*3),"Model accessor count exceeds limit");
    const auto& view=root.at("bufferViews").at(a.at("bufferView").get<int>());need(view.at("buffer")==0&&!view.contains("extensions"),"Unsupported model buffer view");
    const std::size_t begin=view.value("byteOffset",std::size_t(0)),length=view.at("byteLength").get<std::size_t>(),offset=a.value("byteOffset",std::size_t(0)),stride=view.value("byteStride",width);
    need(begin<=data.size()&&length<=data.size()-begin&&offset<=length&&stride>=width&&stride<=252&&stride%component==0&&offset%component==0&&begin%component==0,"Invalid model accessor range/stride");
    need(width<=length-offset&&(count-1)<=(length-offset-width)/stride,"Model accessor exceeds its buffer view");return {data.subspan(begin,length),count,stride,offset,type};
}
Vector vertex(const Accessor& a,std::size_t i){Vector v;double* fields[]{&v.x,&v.y,&v.z};for(int c=0;c<3;++c){*fields[c]=std::bit_cast<float>(u32(a.bytes,a.offset+i*a.stride+c*4));need(std::isfinite(*fields[c])&&std::abs(*fields[c])<=1e8,"Non-finite or excessive model vertex");}return v;}
unsigned element(const Accessor& a,std::size_t i){const auto at=a.offset+i*a.stride;if(a.type==5121)return a.bytes[at];if(a.type==5123)return a.bytes[at]|(unsigned(a.bytes[at+1])<<8);return u32(a.bytes,at);}
void append(std::vector<float>& output,Vector v){output.insert(output.end(),{static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z)});}
}
std::shared_ptr<const Model> decodeModel(std::string id,std::string source,std::span<const unsigned char> bytes){
    need(validObjectId(id)&&source.size()<=512,"Invalid model ID/source");need(bytes.size()>=28&&bytes.size()<=maxModelBytes,"GLB size exceeds limit");
    need(u32(bytes,0)==0x46546c67&&u32(bytes,4)==2&&u32(bytes,8)==bytes.size(),"Invalid GLB 2 header");
    const auto jsonSize=u32(bytes,12);need(jsonSize<=128*1024&&jsonSize%4==0&&jsonSize<=bytes.size()-28&&u32(bytes,16)==0x4e4f534a,"Invalid GLB JSON chunk");
    const std::size_t chunk=20+jsonSize;const auto binarySize=u32(bytes,chunk);need(u32(bytes,chunk+4)==0x004e4942&&binarySize%4==0&&binarySize==bytes.size()-chunk-8,"Expected a single embedded GLB BIN chunk");
    const auto root=J::parse(bytes.begin()+20,bytes.begin()+chunk,[](int depth,J::parse_event_t event,J& value){need(depth<=64,"Model JSON nesting exceeds limit");need(event!=J::parse_event_t::key||value!="extensions","Model extensions are not supported");return true;});
    need(root.at("asset").at("version")=="2.0"&&root.at("asset").value("minVersion",std::string("2.0"))=="2.0","Unsupported glTF version");
    for(const char* field:{"extensionsUsed","extensionsRequired","extensions","images","textures","samplers","skins","animations","cameras"})need(!root.contains(field)||root.at(field).empty(),"Unsupported model feature: static opaque untextured GLB required");
    const auto& buffers=root.at("buffers");need(buffers.is_array()&&buffers.size()==1&&!buffers[0].contains("uri"),"GLB must contain one embedded buffer without URI");const auto used=buffers[0].at("byteLength").get<std::size_t>();need(used<=binarySize&&binarySize-used<=3,"GLB buffer length mismatch");const auto data=bytes.subspan(chunk+8,used);
    const auto& nodes=root.at("nodes");need(nodes.is_array()&&nodes.size()<=128,"Model node limit exceeded");
    for(const char* field:{"meshes","accessors","bufferViews","materials","scenes"})if(root.contains(field))need(root.at(field).is_array()&&root.at(field).size()<=512,"Model table limit exceeded");
    auto model=std::make_shared<Model>();model->id=std::move(id);model->source=std::move(source);model->sourceBytes=bytes.size();
    Vector lower{1e300,1e300,1e300},upper{-1e300,-1e300,-1e300};std::vector<bool> visited(nodes.size());
    std::function<void(int,const Matrix&,int)> visit=[&](int index,const Matrix& parent,int depth){
        need(index>=0&&static_cast<std::size_t>(index)<nodes.size()&&depth<=32&&!visited[index],"Invalid, cyclic or multiply parented model hierarchy");visited[index]=true;const auto& node=nodes[index];need(!node.contains("camera")&&!node.contains("skin")&&!node.contains("weights")&&!node.contains("extensions"),"Unsupported model node");const auto matrix=multiply(parent,transform(node));
        const Vector ax{matrix[0],matrix[1],matrix[2]},ay{matrix[4],matrix[5],matrix[6]},az{matrix[8],matrix[9],matrix[10]};const Vector nx=cross(ay,az),ny=cross(az,ax),nz=cross(ax,ay);const double det=dot(ax,nx);need(std::isfinite(det)&&std::abs(det)>1e-20,"Degenerate model transform");
        if(node.contains("mesh")){
            const auto& mesh=root.at("meshes").at(node.at("mesh").get<int>());need(!mesh.contains("weights"),"Morph targets are not supported");const auto& primitives=mesh.at("primitives");need(primitives.is_array()&&primitives.size()<=64,"Model primitive limit exceeded");
            for(const auto& primitive:primitives){need(model->parts.size()<64&&primitive.value("mode",4)==4&&!primitive.contains("targets")&&!primitive.contains("extensions"),"Only static triangle primitives are supported");const auto& attributes=primitive.at("attributes");need(attributes.is_object()&&attributes.contains("POSITION"),"Model primitive has no positions");for(auto i=attributes.begin();i!=attributes.end();++i)need(i.key()=="POSITION"||i.key()=="NORMAL","Unsupported model vertex attribute");
                const auto positions=accessor(root,data,attributes.at("POSITION").get<int>(),true);model->vertices+=positions.count;need(model->vertices<=maxModelVertices,"Model vertex limit exceeded");ModelPart part;
                std::optional<Accessor> normals;if(attributes.contains("NORMAL")){normals=accessor(root,data,attributes.at("NORMAL").get<int>(),true);need(normals->count==positions.count,"Model normal count mismatch");}
                for(std::size_t i=0;i<positions.count;++i){const auto p=vertex(positions,i);const Vector v=ax*p.x+ay*p.y+az*p.z+Vector{matrix[12],matrix[13],matrix[14]};const double radius=std::hypot(v.x,v.y,v.z);need(std::isfinite(radius)&&radius<=1e8,"Model transformed extent exceeds limit");append(part.positions,v);model->radiusUnits=std::max(model->radiusUnits,radius);lower={std::min(lower.x,v.x),std::min(lower.y,v.y),std::min(lower.z,v.z)};upper={std::max(upper.x,v.x),std::max(upper.y,v.y),std::max(upper.z,v.z)};
                    if(normals){const auto n=vertex(*normals,i);const auto v=(nx*n.x+ny*n.y+nz*n.z)*(1./det);const double length=std::hypot(v.x,v.y,v.z);need(std::isfinite(length)&&length>1e-12,"Invalid model normal");append(part.normals,v*(1./length));}}
                if(primitive.contains("indices")){const auto indices=accessor(root,data,primitive.at("indices").get<int>(),false);for(std::size_t i=0;i<indices.count;++i){const auto value=element(indices,i);need(value<positions.count,"Model triangle references missing vertex");part.indices.push_back(value);}}
                else for(unsigned i=0;i<positions.count;++i)part.indices.push_back(i);
                need(part.indices.size()%3==0,"Incomplete model triangle");model->triangles+=part.indices.size()/3;need(model->triangles<=maxModelTriangles,"Model triangle limit exceeded");if(det<0)for(std::size_t i=0;i<part.indices.size();i+=3)std::swap(part.indices[i+1],part.indices[i+2]);
                if(primitive.contains("material")){const auto& material=root.at("materials").at(primitive.at("material").get<int>());need(material.value("alphaMode",std::string("OPAQUE"))=="OPAQUE"&&!material.contains("extensions")&&!material.contains("normalTexture")&&!material.contains("occlusionTexture")&&!material.contains("emissiveTexture"),"Only opaque untextured PBR materials are supported");part.doubleSided=material.value("doubleSided",false);
                    if(material.contains("emissiveFactor"))for(const auto& e:material.at("emissiveFactor"))need(finite(e)==0,"Emissive materials are not supported");
                    const auto pbr=material.value("pbrMetallicRoughness",J::object());need(!pbr.contains("baseColorTexture")&&!pbr.contains("metallicRoughnessTexture")&&!pbr.contains("extensions"),"Model textures are not supported");
                    const auto color=pbr.value("baseColorFactor",J::array({1,1,1,1}));need(color.is_array()&&color.size()==4&&finite(color[3])==1,"Only opaque base colors are supported");for(int i=0;i<3;++i){double v=finite(color[i]);need(v>=0&&v<=1,"Invalid PBR color");part.color[i]=static_cast<float>(v);}
                    part.metalness=static_cast<float>(finite(pbr.value("metallicFactor",J(1))));part.roughness=static_cast<float>(finite(pbr.value("roughnessFactor",J(1))));need(part.metalness>=0&&part.metalness<=1&&part.roughness>=0&&part.roughness<=1,"Invalid PBR parameters");}
                model->parts.push_back(std::move(part));
            }
        }
        if(node.contains("children")){need(node.at("children").is_array()&&node.at("children").size()<=128,"Invalid model children");for(const auto& child:node.at("children"))visit(child.get<int>(),matrix,depth+1);}
    };
    const auto& scene=root.at("scenes").at(root.value("scene",0));need(scene.at("nodes").is_array()&&scene.at("nodes").size()<=128,"Invalid model roots");for(const auto& node:scene.at("nodes"))visit(node.get<int>(),identity(),0);
    need(!model->parts.empty()&&model->radiusUnits>0,"Model has no visible triangle geometry");model->sizeUnits=upper-lower;return model;
}
}
