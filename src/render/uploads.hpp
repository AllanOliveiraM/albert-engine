#pragma once
#include "threepp/textures/Texture.hpp"
#include <memory>
namespace albert {
// Owns unpublished textures; upload only from the graphics thread.
class TextureUploads {
public:
    TextureUploads();
    ~TextureUploads();
    void add(std::shared_ptr<threepp::Texture> texture);
    // Publishes the next texture whole (one a frame); the scene renderer uploads it on first use,
    // streaming a large one while the previous copy keeps drawing.
    void tick();
    bool active() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
