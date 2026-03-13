#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

namespace game::runtime::render_prep_material {

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

inline float srgbChannelToLinear(float c) {
    const float v = std::clamp(c, 0.0f, 1.0f);
    if (v <= 0.04045f) return v / 12.92f;
    return std::pow((v + 0.055f) / 1.055f, 2.4f);
}

inline float linearChannelToSrgb(float c) {
    const float v = std::max(0.0f, c);
    if (v <= 0.0031308f) return v * 12.92f;
    return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

inline glm::vec3 srgbToLinear(const glm::vec3& c) {
    return glm::vec3(
        srgbChannelToLinear(c.r),
        srgbChannelToLinear(c.g),
        srgbChannelToLinear(c.b));
}

inline glm::vec3 linearToSrgb(const glm::vec3& c) {
    return glm::vec3(
        linearChannelToSrgb(c.r),
        linearChannelToSrgb(c.g),
        linearChannelToSrgb(c.b));
}

inline glm::vec3 tonemapACES(const glm::vec3& x) {
    const glm::vec3 a(2.51f);
    const glm::vec3 b(0.03f);
    const glm::vec3 c(2.43f);
    const glm::vec3 d(0.59f);
    const glm::vec3 e(0.14f);
    return glm::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

inline glm::vec3 composeGltfLikeColor(const glm::vec3& baseColorSrgb,
                                      const glm::vec3& emissiveSrgb,
                                      const glm::vec3& emissiveFactor,
                                      float exposure = 0.85f) {
    const glm::vec3 baseLin = srgbToLinear(glm::clamp(baseColorSrgb, 0.0f, 1.0f));
    const glm::vec3 emiLin =
        srgbToLinear(glm::clamp(emissiveSrgb, 0.0f, 1.0f)) *
        glm::clamp(emissiveFactor, 0.0f, 8.0f);
    const glm::vec3 mapped = tonemapACES((baseLin + emiLin) * std::max(0.0f, exposure));
    return glm::clamp(linearToSrgb(mapped), 0.0f, 1.0f);
}

inline glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-12f) return glm::normalize(v);
    return fallback;
}

inline glm::vec3 shadeVertexLitColor(const glm::vec3& baseColorSrgb,
                                     const glm::vec3& worldNormal,
                                     const glm::vec3& lightDirWorld,
                                     const glm::vec3& viewDirWorld,
                                     bool flipForBackface = false) {
    glm::vec3 n = safeNormalize(worldNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    if (flipForBackface) n = -n;
    const glm::vec3 l = safeNormalize(lightDirWorld, glm::vec3(0.45f, 0.90f, 0.35f));
    const glm::vec3 v = safeNormalize(viewDirWorld, glm::vec3(0.0f, 0.0f, 1.0f));

    const float ndotl = std::clamp(glm::dot(n, l), 0.0f, 1.0f);
    const float diffuse = 0.32f + 0.68f * ndotl;
    const float hemi = 0.06f + 0.14f * std::clamp(n.y * 0.5f + 0.5f, 0.0f, 1.0f);
    const float rimBase = 1.0f - std::clamp(glm::dot(n, v), 0.0f, 1.0f);
    const float rim = std::pow(rimBase, 2.0f) * 0.10f;

    return glm::clamp(baseColorSrgb * (diffuse + hemi) + glm::vec3(rim), 0.0f, 1.0f);
}

} // namespace game::runtime::render_prep_material

