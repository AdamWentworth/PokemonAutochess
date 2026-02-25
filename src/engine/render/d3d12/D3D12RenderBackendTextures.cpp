#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12TextureUpload.h"

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
        return &existing->second;
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
                                                                          true,
                                                                          texture.resource);
    stbi_image_free(pixels);
    if (!ok) {
        return ensureFallbackSpriteTexture();
    }

    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = spriteTextures_.emplace(texturePath, std::move(texture));
    return &insertedIt->second;
#else
    (void)texturePath;
    return nullptr;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureWorldTexture(const WorldTextureData* textureData) {
#if defined(_WIN32)
    if (!textureData ||
        !textureData->rgba ||
        textureData->width <= 0 ||
        textureData->height <= 0 ||
        !textureData->key ||
        textureData->key[0] == '\0') {
        return nullptr;
    }

    const std::string key(textureData->key);
    auto it = worldTextures_.find(key);
    if (it != worldTextures_.end()) {
        return &it->second;
    }

    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

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
                                                                          textureData->rgba,
                                                                          textureData->width,
                                                                          textureData->height,
                                                                          textureData->wrapS,
                                                                          textureData->wrapT,
                                                                          false,
                                                                          texture.resource);
    if (!ok) return nullptr;

    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = worldTextures_.emplace(key, std::move(texture));
    return &insertedIt->second;
#else
    (void)textureData;
    return nullptr;
#endif
}
