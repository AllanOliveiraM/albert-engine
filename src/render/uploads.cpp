#include "uploads.hpp"
#include "threepp/textures/Texture.hpp"
#include <deque>
#include <stdexcept>
using namespace threepp;
struct albert::TextureUploads::Impl {
    struct Job {std::shared_ptr<Texture> texture;Image image;bool mipmaps=false;};
    std::deque<Job> jobs;
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
    impl_->jobs.push_back({std::move(texture),std::move(image),mips});
}
void albert::TextureUploads::tick() {
    if(!active()) return;
    auto& job=impl_->jobs.front();
    if(job.mipmaps){job.texture->generateMipmaps=true;job.texture->minFilter=Filter::LinearMipmapLinear;}
    job.texture->image()=std::move(job.image);job.texture->needsUpdate();impl_->jobs.pop_front();
}
