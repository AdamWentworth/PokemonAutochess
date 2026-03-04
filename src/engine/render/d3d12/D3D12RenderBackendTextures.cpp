#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12TextureUpload.h"
#include "engine/core/Environment.h"

#include <algorithm>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <stb_image.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

namespace {

bool worldTextureMipChainEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_WORLD_TEXTURE_MIPS");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

} // namespace

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureFallbackSpriteTexture() {
#if defined(_WIN32)
    const auto it = spriteTextures_.find(kFallbackSpriteTextureKey);
    if (it != spriteTextures_.end()) {
        return const_cast<SpriteTexture*>(&it->second);
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

    static const unsigned char kFallbackRgba[16] = {
         72,  90, 108, 255,
         56,  70,  84, 255,
         56,  70,  84, 255,
         72,  90, 108, 255
    };

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    if (!engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                              commandQueue_.Get(),
                                                              fence_.Get(),
                                                              static_cast<HANDLE>(fenceEvent_),
                                                              fenceValue_,
                                                              srvHeap_.Get(),
                                                              srvDescriptorSize_,
                                                              texture.descriptorIndex,
                                                              kFallbackRgba,
                                                              2,
                                                              2,
                                                              kGlClampToEdge,
                                                              kGlClampToEdge,
                                                              true,
                                                              true,
                                                              texture.resource)) {
        return nullptr;
    }
    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = spriteTextures_.emplace(kFallbackSpriteTextureKey, std::move(texture));
    return &insertedIt->second;
#else
    return nullptr;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureSpriteTexture(const std::string& texturePath) {
#if defined(_WIN32)
    if (texturePath.empty()) return ensureFallbackSpriteTexture();

    auto existing = spriteTextures_.find(texturePath);
    if (existing != spriteTextures_.end()) {
        if (existing->second.valid) return &existing->second;
        return ensureFallbackSpriteTexture();
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return ensureFallbackSpriteTexture();
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return ensureFallbackSpriteTexture();

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    std::string altPath;
    if (!pixels) {
        altPath = texturePath;
        std::replace(altPath.begin(), altPath.end(), '\\', '/');
        if (altPath != texturePath) {
            stbi_set_flip_vertically_on_load(false);
            pixels = stbi_load(altPath.c_str(), &width, &height, &channels, 4);
        }
    }

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        SpriteTexture failed;
        failed.valid = false;
        spriteTextures_.emplace(texturePath, failed);
        if (!altPath.empty() && altPath != texturePath) {
            spriteTextures_.emplace(altPath, failed);
        }
        return ensureFallbackSpriteTexture();
    }

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool ok = engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                                          commandQueue_.Get(),
                                                                          fence_.Get(),
                                                                          static_cast<HANDLE>(fenceEvent_),
                                                                          fenceValue_,
                                                                          srvHeap_.Get(),
                                                                          srvDescriptorSize_,
                                                                          texture.descriptorIndex,
                                                                          pixels,
                                                                          width,
                                                                          height,
                                                                          kGlClampToEdge,
                                                                          kGlClampToEdge,
                                                                          false,
                                                                          true,
                                                                          texture.resource);
    stbi_image_free(pixels);
    if (!ok) {
        SpriteTexture failed;
        failed.valid = false;
        spriteTextures_.emplace(texturePath, failed);
        if (!altPath.empty() && altPath != texturePath) {
            spriteTextures_.emplace(altPath, failed);
        }
        return ensureFallbackSpriteTexture();
    }

    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = spriteTextures_.emplace(texturePath, texture);
    if (!altPath.empty() && altPath != texturePath) {
        spriteTextures_.emplace(altPath, texture);
    }
    return &insertedIt->second;
#else
    (void)texturePath;
    return nullptr;
#endif
}

void D3D12RenderBackend::prewarmDebugSpriteTexture(const char* texturePath) {
#if defined(_WIN32)
    if (!texturePath || texturePath[0] == '\0') return;
    (void)ensureSpriteTexture(texturePath);
#else
    (void)texturePath;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureWorldTexture(const WorldTextureData* textureData) {
#if defined(_WIN32)
    if (!textureData) return nullptr;
    return ensureWorldTextureRaw(
        textureData->key,
        textureData->rgba,
        textureData->width,
        textureData->height,
        textureData->wrapS,
        textureData->wrapT,
        /*srgb=*/true);
#else
    (void)textureData;
    return nullptr;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureWorldTextureRaw(const char* key,
                                                                              const unsigned char* rgba,
                                                                              int width,
                                                                              int height,
                                                                              int wrapS,
                                                                              int wrapT,
                                                                              bool srgb) {
#if defined(_WIN32)
    if (!key || key[0] == '\0' || !rgba || width <= 0 || height <= 0) {
        return nullptr;
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

    // Include wrap + dimensions to avoid accidental cache aliasing for reused key strings.
    std::string cacheKey = key;
    cacheKey += "|";
    cacheKey += std::to_string(width);
    cacheKey += "x";
    cacheKey += std::to_string(height);
    cacheKey += "|ws=";
    cacheKey += std::to_string(wrapS);
    cacheKey += "|wt=";
    cacheKey += std::to_string(wrapT);
    cacheKey += srgb ? "|srgb" : "|lin";

    auto it = worldTextures_.find(cacheKey);
    if (it != worldTextures_.end()) {
        return &it->second;
    }

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool generateMipChain = worldTextureMipChainEnabled();
    const bool ok = engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                                          commandQueue_.Get(),
                                                                          fence_.Get(),
                                                                          static_cast<HANDLE>(fenceEvent_),
                                                                          fenceValue_,
                                                                          srvHeap_.Get(),
                                                                          srvDescriptorSize_,
                                                                          texture.descriptorIndex,
                                                                          rgba,
                                                                          width,
                                                                          height,
                                                                          wrapS,
                                                                          wrapT,
                                                                          generateMipChain,
                                                                          srgb,
                                                                          texture.resource);
    if (!ok) return nullptr;

    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = worldTextures_.emplace(cacheKey, std::move(texture));
    return &insertedIt->second;
#else
    (void)key;
    (void)rgba;
    (void)width;
    (void)height;
    (void)wrapS;
    (void)wrapT;
    (void)srgb;
    return nullptr;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureWorldTextureRawHalfFloat(
    const char* key,
    const std::uint16_t* rgba16f,
    int width,
    int height,
    int wrapS,
    int wrapT) {
#if defined(_WIN32)
    if (!key || key[0] == '\0' || !rgba16f || width <= 0 || height <= 0) {
        return nullptr;
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

    std::string cacheKey = key;
    cacheKey += "|";
    cacheKey += std::to_string(width);
    cacheKey += "x";
    cacheKey += std::to_string(height);
    cacheKey += "|ws=";
    cacheKey += std::to_string(wrapS);
    cacheKey += "|wt=";
    cacheKey += std::to_string(wrapT);
    cacheKey += "|rgba16f";

    auto it = worldTextures_.find(cacheKey);
    if (it != worldTextures_.end()) {
        return &it->second;
    }

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool ok = engine::render::d3d12::createTextureResourceFromRgba16F(
        device_.Get(),
        commandQueue_.Get(),
        fence_.Get(),
        static_cast<HANDLE>(fenceEvent_),
        fenceValue_,
        srvHeap_.Get(),
        srvDescriptorSize_,
        texture.descriptorIndex,
        rgba16f,
        width,
        height,
        texture.resource);
    if (!ok) return nullptr;

    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = worldTextures_.emplace(cacheKey, std::move(texture));
    return &insertedIt->second;
#else
    (void)key;
    (void)rgba16f;
    (void)width;
    (void)height;
    (void)wrapS;
    (void)wrapT;
    return nullptr;
#endif
}
