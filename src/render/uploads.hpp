#pragma once
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/textures/Texture.hpp"
#include <memory>
namespace albert {
// Owns unpublished textures; upload only from the graphics thread.
class TextureUploads {
public:
    TextureUploads();
    ~TextureUploads();
    void add(std::shared_ptr<threepp::Texture> texture);
    void tick(threepp::GLRenderer& renderer);
    bool active() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
