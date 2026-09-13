#pragma once

#include "engine/core/IAssetStore.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include <glm/glm.hpp>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace game::vfx {

// Source-neutral preview asset: sampled textured meshes plus camera-facing
// cards. No importer, source-game rules, or backend-specific rendering here.
class SampledEffectClip {
  public:
    using Batch = runtime::shared_world_batches::WorldIndexedBatch;
    using Texture = runtime::SharedBackendTextureCacheEntry;
    using TextureLoader = std::function<Texture *(const std::string &, bool)>;
    bool load(const engine::IAssetStore &store, const std::string &directory, std::string &error);
    void append(float age, glm::vec3 origin, glm::vec3 scale, const glm::mat4 &viewProjection,
                glm::vec3 cameraRight, glm::vec3 cameraUp, const TextureLoader &textures,
                std::vector<Batch> &batches) const;
    float duration() const { return frameCount_ / fps_; }
    float floor() const { return floor_; }
    std::size_t meshCount() const { return meshes_.size(); }
    std::size_t cardCount() const { return cards_.size(); }
    glm::vec3 groundMinimum() const { return groundMin_; }
    glm::vec3 groundMaximum() const { return groundMax_; }
    void prewarmTextures(const TextureLoader &textures) const;

  private:
    struct Material {
        int texture = -1;
        bool depthWrite = false, additive = false;
        unsigned char wrapS = 0, wrapT = 0;
    };
    struct Mesh {
        Material material;
        std::vector<std::uint32_t> indices;
        std::vector<std::vector<IRenderBackend::WorldMeshVertex>> frames;
    };
    struct Card {
        Material material;
        std::array<glm::vec2, 4> quad{}, uv{};
        glm::vec3 direction{};
        std::vector<std::array<float, 12>> frames;
    };
    Batch batch(const Material &material, const TextureLoader &textures) const;
    std::vector<Mesh> meshes_;
    std::vector<Card> cards_;
    std::vector<std::string> textures_;
    std::string geometryPrefix_;
    float fps_ = 30, floor_ = 0;
    int frameCount_ = 0;
    glm::vec3 groundMin_{0}, groundMax_{0};
};

} // namespace game::vfx
