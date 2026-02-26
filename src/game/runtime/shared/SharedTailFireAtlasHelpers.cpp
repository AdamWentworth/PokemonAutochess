#include "game/runtime/shared/SharedTailFireAtlasHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game::runtime::shared_tail_fire_atlas {

bool buildPremultipliedAtlas(const RgbaTextureView& src, RgbaTextureOwned& out) {
    if (!src.rgba || src.width <= 0 || src.height <= 0) return false;
    const std::size_t pixelCount = static_cast<std::size_t>(src.width) * static_cast<std::size_t>(src.height);
    out.width = src.width;
    out.height = src.height;
    out.rgba.assign(pixelCount * 4u, 0u);

    for (std::size_t i = 0; i + 3u < out.rgba.size(); i += 4u) {
        const float r = static_cast<float>(src.rgba[i + 0u]) / 255.0f;
        const float g = static_cast<float>(src.rgba[i + 1u]) / 255.0f;
        const float b = static_cast<float>(src.rgba[i + 2u]) / 255.0f;
        const float a = static_cast<float>(src.rgba[i + 3u]) / 255.0f;
        const glm::vec3 rgb = glm::vec3(r, g, b) * a;
        out.rgba[i + 0u] = static_cast<unsigned char>(
            std::clamp<int>(static_cast<int>(std::lround(rgb.r * 255.0f)), 0, 255));
        out.rgba[i + 1u] = static_cast<unsigned char>(
            std::clamp<int>(static_cast<int>(std::lround(rgb.g * 255.0f)), 0, 255));
        out.rgba[i + 2u] = static_cast<unsigned char>(
            std::clamp<int>(static_cast<int>(std::lround(rgb.b * 255.0f)), 0, 255));
        out.rgba[i + 3u] = src.rgba[i + 3u];
    }
    return true;
}

bool buildCombinedAtlas(const RgbaTextureView& primary,
                        const RgbaTextureView* secondary,
                        RgbaTextureOwned& out,
                        CombinedAtlasInfo& outInfo) {
    if (!primary.rgba || primary.width <= 0 || primary.height <= 0) return false;
    const bool hasSecondary = secondary && secondary->rgba && secondary->width > 0 && secondary->height > 0;
    outInfo.hasSecondary = hasSecondary;
    const int gutter = hasSecondary ? 2 : 0;
    const int atlasW = std::max(1, primary.width + gutter + (hasSecondary ? secondary->width : 0));
    const int atlasH = std::max(1, std::max(primary.height, hasSecondary ? secondary->height : 0));
    out.width = atlasW;
    out.height = atlasH;
    out.rgba.assign(static_cast<std::size_t>(atlasW) * static_cast<std::size_t>(atlasH) * 4u, 0u);

    auto blit = [&](const RgbaTextureView& src, int dstX, int dstY) {
        for (int y = 0; y < src.height; ++y) {
            if (dstY + y < 0 || dstY + y >= atlasH) continue;
            const std::size_t srcRowBytes = static_cast<std::size_t>(src.width) * 4u;
            const std::size_t srcIdx =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(src.width) * 4u;
            const std::size_t dstIdx =
                (static_cast<std::size_t>(dstY + y) * static_cast<std::size_t>(atlasW) +
                 static_cast<std::size_t>(dstX)) *
                4u;
            if (srcIdx + srcRowBytes <= static_cast<std::size_t>(src.width) * static_cast<std::size_t>(src.height) * 4u &&
                dstIdx + srcRowBytes <= out.rgba.size()) {
                std::memcpy(out.rgba.data() + dstIdx, src.rgba + srcIdx, srcRowBytes);
            }
        }
    };

    blit(primary, 0, 0);
    if (hasSecondary) {
        blit(*secondary, primary.width + gutter, 0);
    }

    const float invW = 1.0f / static_cast<float>(atlasW);
    const float invH = 1.0f / static_cast<float>(atlasH);
    outInfo.rect0 = glm::vec4(
        0.0f,
        0.0f,
        static_cast<float>(primary.width) * invW,
        static_cast<float>(primary.height) * invH);
    if (hasSecondary) {
        outInfo.rect1 = glm::vec4(
            static_cast<float>(primary.width + gutter) * invW,
            0.0f,
            static_cast<float>(secondary->width) * invW,
            static_cast<float>(secondary->height) * invH);
    } else {
        outInfo.rect1 = outInfo.rect0;
    }
    return true;
}

} // namespace game::runtime::shared_tail_fire_atlas
