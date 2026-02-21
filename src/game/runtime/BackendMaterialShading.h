#pragma once

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>

namespace game::runtime::backend_material {

enum class AlphaMode : std::uint8_t {
    Opaque = 0u,
    Mask = 1u,
    Blend = 2u
};

inline AlphaMode alphaModeFromByte(std::uint8_t raw) {
    if (raw == static_cast<std::uint8_t>(AlphaMode::Mask)) return AlphaMode::Mask;
    if (raw == static_cast<std::uint8_t>(AlphaMode::Blend)) return AlphaMode::Blend;
    return AlphaMode::Opaque;
}

inline float opacityFromAlphaMode(AlphaMode mode, float texAlpha, float alphaCutoff) {
    const float a = std::clamp(texAlpha, 0.0f, 1.0f);
    switch (mode) {
        case AlphaMode::Mask:
            return (a >= std::clamp(alphaCutoff, 0.0f, 1.0f)) ? 1.0f : 0.0f;
        case AlphaMode::Blend:
            return a;
        case AlphaMode::Opaque:
        default:
            return 1.0f;
    }
}

inline glm::vec3 blendBaseAndTexture(const glm::vec3& baseColor,
                                     const glm::vec3& texColor,
                                     float texAlpha,
                                     float minTextureBlend = 0.35f) {
    const float blend = std::max(std::clamp(minTextureBlend, 0.0f, 1.0f), std::clamp(texAlpha, 0.0f, 1.0f));
    return glm::clamp(baseColor * (1.0f - blend) + texColor * blend, 0.0f, 1.0f);
}

inline glm::vec3 modulateBaseAndTexture(const glm::vec3& baseColor,
                                        const glm::vec3& texColor) {
    return glm::clamp(baseColor * texColor, 0.0f, 1.0f);
}

} // namespace game::runtime::backend_material
