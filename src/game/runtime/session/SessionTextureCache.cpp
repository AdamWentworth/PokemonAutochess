#include "game/runtime/session/SessionTextureCache.h"

#include "engine/core/Paths.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <stb_image.h>

namespace game::runtime::session_texture_cache {

namespace {

void initializeCacheEntry(SharedBackendTextureCacheEntry& cacheEntry) {
    cacheEntry.attemptedLoad = true;
    cacheEntry.valid = false;
    cacheEntry.width = 0;
    cacheEntry.height = 0;
    cacheEntry.rgba.clear();
}

void storeWhiteTexture(SharedBackendTextureCacheEntry& cacheEntry) {
    cacheEntry.width = 1;
    cacheEntry.height = 1;
    cacheEntry.rgba = {255u, 255u, 255u, 255u};
    cacheEntry.valid = true;
}

void rasterizeProceduralTexture(SharedBackendTextureCacheEntry& cacheEntry,
                                const std::string& procId) {
    constexpr int width = 64;
    constexpr int height = 64;
    cacheEntry.width = width;
    cacheEntry.height = height;
    cacheEntry.rgba.resize(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        0u);

    auto putPixel = [&](int x, int y, float alpha) {
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        const std::size_t idx =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            4u;
        cacheEntry.rgba[idx + 0] = 255u;
        cacheEntry.rgba[idx + 1] = 255u;
        cacheEntry.rgba[idx + 2] = 255u;
        cacheEntry.rgba[idx + 3] =
            static_cast<unsigned char>(std::round(alpha * 255.0f));
    };

    auto smooth = [](float e0, float e1, float x) {
        const float t = std::clamp(
            (x - e0) / std::max(0.0001f, (e1 - e0)),
            0.0f,
            1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float fx =
                ((static_cast<float>(x) + 0.5f) / static_cast<float>(width)) * 2.0f - 1.0f;
            const float fy =
                ((static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * 2.0f - 1.0f;
            const float r = std::sqrt(fx * fx + fy * fy);
            float alpha = 0.0f;

            if (procId == "soft_circle" || procId == "dot") {
                alpha = 1.0f - smooth(0.55f, 1.0f, r);
            } else if (procId == "plus") {
                const float h = (1.0f - smooth(0.18f, 0.26f, std::fabs(fy))) *
                                (1.0f - smooth(0.78f, 0.98f, std::fabs(fx)));
                const float v = (1.0f - smooth(0.18f, 0.26f, std::fabs(fx))) *
                                (1.0f - smooth(0.78f, 0.98f, std::fabs(fy)));
                alpha = std::max(h, v);
            } else if (procId == "leaf" || procId == "seed") {
                float px = fx;
                float py = fy * 1.12f + 0.03f;
                const float t = std::clamp((py + 1.0f) * 0.5f, 0.0f, 1.0f);
                const float widthScale = (procId == "seed")
                    ? std::max(0.20f, (0.85f - 0.60f * t))
                    : std::max(0.20f, (0.95f - 0.70f * t));
                const float d =
                    std::sqrt((px / widthScale) * (px / widthScale) + py * py);
                alpha = 1.0f - smooth(0.82f, 1.02f, d);
                alpha *= smooth(-1.0f, -0.68f, py);
            } else if (procId == "starburst") {
                const float ang = std::atan2(fy, fx);
                const float spikes = std::pow(std::fabs(std::sin(ang * 11.0f)), 0.75f);
                const float core = 1.0f - smooth(0.0f, 0.74f, r);
                const float streak = (1.0f - smooth(0.0f, 0.92f, r)) * spikes;
                alpha = std::max(core * 0.9f, streak);
            } else if (procId == "claw") {
                const float ca = std::cos(-0.60f);
                const float sa = std::sin(-0.60f);
                const float qx = ca * fx - sa * fy;
                const float qy = sa * fx + ca * fy;
                auto stroke = [&](float xOff, float halfLen, float width0) {
                    const float lx = std::fabs(qx - xOff);
                    const float ly = std::fabs(qy);
                    const float tipT =
                        std::clamp(ly / std::max(0.0001f, halfLen), 0.0f, 1.0f);
                    const float tipNarrow = 1.0f - smooth(0.58f, 1.0f, tipT) * 0.96f;
                    const float localWidth = width0 * tipNarrow;
                    const float core = 1.0f - smooth(localWidth, localWidth + 0.02f, lx);
                    const float lenMask = 1.0f - smooth(halfLen, halfLen + 0.05f, ly);
                    return core * lenMask;
                };
                const float s1 = stroke(-0.34f, 0.90f, 0.075f);
                const float s2 = stroke(0.00f, 0.90f, 0.075f);
                const float s3 = stroke(0.34f, 0.90f, 0.075f);
                alpha = std::max(s1, std::max(s2, s3));
            } else if (procId == "swoosh") {
                const float band = std::fabs(fy - 0.6f * fx);
                const float arc =
                    (1.0f - smooth(0.0f, 0.36f, band)) *
                    (1.0f - smooth(0.32f, 1.0f, r));
                const float core = 1.0f - smooth(0.0f, 0.62f, r);
                alpha = std::max(arc, core * 0.35f);
            } else {
                alpha = 1.0f - smooth(0.55f, 1.0f, r);
            }

            putPixel(x, y, alpha);
        }
    }

    cacheEntry.valid = true;
}

bool loadTexturePixels(const std::string& texturePath,
                       bool flipVertical,
                       SharedBackendTextureCacheEntry& cacheEntry) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
    unsigned char* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        const std::string dataPath = engine::paths::data(texturePath);
        if (dataPath != texturePath) {
            stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
            pixels = stbi_load(dataPath.c_str(), &width, &height, &channels, 4);
        }
    }
    stbi_set_flip_vertically_on_load(false);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    const std::size_t rgbaSize =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    cacheEntry.rgba.resize(rgbaSize);
    std::memcpy(cacheEntry.rgba.data(), pixels, rgbaSize);
    stbi_image_free(pixels);
    cacheEntry.width = width;
    cacheEntry.height = height;
    cacheEntry.valid = true;
    return true;
}

} // namespace

SharedBackendTextureCacheEntry* ensureTextureLoaded(
    TextureCache& backendTextureByPath,
    const std::string& texturePath,
    bool flipVertical) {
    if (backendTextureByPath.empty()) {
        backendTextureByPath.reserve(64u);
    }
    const std::string key = texturePath.empty()
        ? "__white__"
        : ((flipVertical ? "__flipv__:" : "__noflip__:") + texturePath);
    auto& cacheEntry = backendTextureByPath[key];
    if (cacheEntry.attemptedLoad) {
        return cacheEntry.valid ? &cacheEntry : nullptr;
    }

    initializeCacheEntry(cacheEntry);
    if (texturePath.empty()) {
        storeWhiteTexture(cacheEntry);
        return &cacheEntry;
    }

    if (texturePath.rfind("__proc:", 0) == 0) {
        rasterizeProceduralTexture(cacheEntry, texturePath.substr(7));
        return &cacheEntry;
    }

    if (!loadTexturePixels(texturePath, flipVertical, cacheEntry)) {
        return nullptr;
    }
    return &cacheEntry;
}

} // namespace game::runtime::session_texture_cache
