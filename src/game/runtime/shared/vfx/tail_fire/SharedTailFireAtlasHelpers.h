#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_tail_fire_atlas {

struct RgbaTextureView {
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
};

struct RgbaTextureOwned {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

struct CombinedAtlasInfo {
    bool hasSecondary = false;
    glm::vec4 rect0{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec4 rect1{0.0f, 0.0f, 1.0f, 1.0f};
};

bool buildPremultipliedAtlas(const RgbaTextureView& src, RgbaTextureOwned& out);

bool buildCombinedAtlas(const RgbaTextureView& primary,
                        const RgbaTextureView* secondary,
                        RgbaTextureOwned& out,
                        CombinedAtlasInfo& outInfo);

} // namespace game::runtime::shared_tail_fire_atlas
