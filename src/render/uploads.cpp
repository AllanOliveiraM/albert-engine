#include "uploads.hpp"
#include "threepp/threepp.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include <glad/glad.h>
#include <chrono>
#include <deque>
#include <stdexcept>
using namespace threepp;
struct albert::TextureUploads::Impl {
    struct Job {
        std::shared_ptr<Texture> texture;
        Image image;
        unsigned row=0;
        bool initialized=false;
        bool mipmaps=false;
    };
    std::deque<Job> jobs;
    std::shared_ptr<Scene> scene;
    std::shared_ptr<OrthographicCamera> camera;
    std::shared_ptr<MeshBasicMaterial> material;
    std::unique_ptr<RenderTarget> target;
};
albert::TextureUploads::TextureUploads():impl_(std::make_unique<Impl>()) {}
albert::TextureUploads::~TextureUploads()=default;
bool albert::TextureUploads::active() const {return !impl_->jobs.empty();}
void albert::TextureUploads::add(std::shared_ptr<Texture> texture) {
    if(!texture || texture->image().channels()!=4) throw std::invalid_argument("Upload requires RGBA pixels");
    auto image=std::move(texture->image());
    const bool mips=texture->generateMipmaps;
    texture->generateMipmaps=false;texture->minFilter=Filter::Linear;
    texture->image()=image.isFloat() ? Image(std::vector<float>(4,0.f),1,1) : Image(std::vector<unsigned char>(4,0),1,1);
    impl_->jobs.push_back({std::move(texture),std::move(image),0,false,mips});
}
void albert::TextureUploads::tick(GLRenderer* gl) {
    if(!active()) return;
    auto& state=*impl_;auto& job=state.jobs.front();
    if(!gl){
        if(job.mipmaps){job.texture->generateMipmaps=true;job.texture->minFilter=Filter::LinearMipmapLinear;}
        job.texture->image()=std::move(job.image);job.texture->needsUpdate();state.jobs.pop_front();return;
    }
    auto& renderer=*gl;
    RenderTarget* previous=renderer.getRenderTarget();
    Vector4 viewport;renderer.getViewport(viewport);
    if(!state.scene) {
        state.scene=Scene::create();state.camera=OrthographicCamera::create(-1,1,1,-1,.1f,10.f);
        state.camera->position.z=1;
        state.material=MeshBasicMaterial::create();state.material->toneMapped=false;
        state.scene->add(Mesh::create(PlaneGeometry::create(1,1),state.material));
        state.target=RenderTarget::create(1,1,{});
    }
    if(!job.initialized) {
        state.material->map=job.texture;state.material->needsUpdate();
        renderer.setRenderTarget(state.target.get());renderer.render(*state.scene,*state.camera);
        renderer.setRenderTarget(previous);renderer.setViewport(viewport);
        const auto id=renderer.getGlTextureId(*job.texture);
        if(!id) throw std::runtime_error("GPU texture initialization failed");
        glBindTexture(GL_TEXTURE_2D,*id);
        glTexImage2D(GL_TEXTURE_2D,0,job.image.isFloat() ? GL_RGBA32F : GL_RGBA8,
            job.image.width(),job.image.height(),0,GL_RGBA,job.image.isFloat() ? GL_FLOAT : GL_UNSIGNED_BYTE,nullptr);
        job.initialized=true;
        renderer.resetState();renderer.setRenderTarget(previous);renderer.setViewport(viewport);
        return; // Never combine allocation with a large upload in one frame.
    }
    const auto id=renderer.getGlTextureId(*job.texture);
    if(!id) throw std::runtime_error("Pending GPU texture was disposed");
    glBindTexture(GL_TEXTURE_2D,*id);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    const auto start=std::chrono::steady_clock::now();
    const size_t rowBytes=job.image.width()*4*(job.image.isFloat() ? sizeof(float) : 1);
    size_t bytes=0;
    while(job.row<job.image.height() && bytes<8*1024*1024) {
        const unsigned rows=std::min<unsigned>(job.image.height()-job.row,std::max<size_t>(1,1024*1024/rowBytes));
        const void* data=job.image.isFloat() ? static_cast<const void*>(job.image.data<float>().data()+job.row*job.image.width()*4) :
            static_cast<const void*>(job.image.data().data()+job.row*job.image.width()*4);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,job.row,job.image.width(),rows,GL_RGBA,job.image.isFloat() ? GL_FLOAT : GL_UNSIGNED_BYTE,data);
        job.row+=rows;bytes+=rows*rowBytes;
        if(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(2)) break;
    }
    // Mipmap generation gets its own frame too. The image is unpublished until
    // every level is ready, and the last valid surface keeps drawing meanwhile.
    if(bytes==0 && job.row==job.image.height()) {
        if(job.mipmaps) {
            glGenerateMipmap(GL_TEXTURE_2D);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
            job.texture->minFilter=Filter::LinearMipmapLinear;
        }
        job.texture->image()=std::move(job.image);
        state.material->map.reset();state.jobs.pop_front();
    }
    renderer.resetState();renderer.setRenderTarget(previous);renderer.setViewport(viewport);
}
