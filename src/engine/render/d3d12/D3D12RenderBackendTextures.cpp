#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/SpriteTextureCardArt.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/render/d3d12/D3D12TextureUpload.h"
#include "engine/core/Environment.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <stb_image.h>
#include <stb_image_write.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

namespace {

bool isTailFireWorldTextureKey(const char* key) {
    if (!key || key[0] == '\0') return false;
    return std::string(key).find("__tailfire_") != std::string::npos;
}

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

int backendCardArtMaxDim() {
    static const int dim = []() -> int {
        constexpr int kDefault = 256;
        constexpr int kMin = 64;
        constexpr int kMax = 512;
        const auto env = engine::env::get("PAC_BACKEND_CARD_ART_MAX_DIM");
        if (!env.has_value()) return kDefault;
        try {
            return std::clamp(std::stoi(*env), kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return dim;
}

struct LoadedSpritePixels {
    std::string cacheKey;
    std::string sourcePath;
    std::string altCacheKey;
    int width = 0;
    int height = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels{nullptr, stbi_image_free};
    std::vector<unsigned char> resizedPixels;
    const unsigned char* rgba = nullptr;
};

bool loadCachedProxyPixels(const std::filesystem::path& cachePath, LoadedSpritePixels& out) {
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec) || ec) return false;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* loaded = stbi_load(cachePath.string().c_str(), &width, &height, &channels, 4);
    if (!loaded || width <= 0 || height <= 0) {
        if (loaded) stbi_image_free(loaded);
        return false;
    }

    out.pixels.reset(loaded);
    out.width = width;
    out.height = height;
    out.rgba = out.pixels.get();
    return true;
}

void writeCachedProxyPixels(const std::filesystem::path& cachePath, const LoadedSpritePixels& loaded) {
    if (!loaded.rgba || loaded.width <= 0 || loaded.height <= 0) return;

    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    (void)stbi_write_png(cachePath.string().c_str(),
                         loaded.width,
                         loaded.height,
                         4,
                         loaded.rgba,
                         loaded.width * 4);
}

bool loadSpritePixels(const std::string& texturePath, LoadedSpritePixels& out) {
    out = {};
    out.cacheKey = texturePath;
    out.sourcePath = engine::render::sprite_card_art::sourcePathFromProxy(texturePath);
    if (engine::render::sprite_card_art::isProxyPath(texturePath)) {
        const std::string normalizedSource =
            engine::render::sprite_card_art::resolveExistingPath(out.sourcePath).generic_string();
        if (!normalizedSource.empty() && normalizedSource != out.sourcePath) {
            out.altCacheKey = engine::render::sprite_card_art::makeProxyPath(normalizedSource);
        }

        const int maxDim = backendCardArtMaxDim();
        const auto cachePath = engine::render::sprite_card_art::proxyCachePath(out.sourcePath, maxDim);
        if (loadCachedProxyPixels(cachePath, out)) {
            return true;
        }
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* loaded = stbi_load(out.sourcePath.c_str(), &width, &height, &channels, 4);
    std::string altPath;
    if (!loaded) {
        altPath = out.sourcePath;
        std::replace(altPath.begin(), altPath.end(), '\\', '/');
        if (altPath != out.sourcePath) {
            stbi_set_flip_vertically_on_load(false);
            loaded = stbi_load(altPath.c_str(), &width, &height, &channels, 4);
        }
    }

    if (!loaded || width <= 0 || height <= 0) {
        if (loaded) stbi_image_free(loaded);
        return false;
    }

    out.pixels.reset(loaded);
    out.width = width;
    out.height = height;
    out.rgba = out.pixels.get();
    if (out.altCacheKey.empty() && !altPath.empty() && altPath != out.sourcePath) {
        if (engine::render::sprite_card_art::isProxyPath(texturePath)) {
            out.altCacheKey = engine::render::sprite_card_art::makeProxyPath(altPath);
        } else {
            out.altCacheKey = altPath;
        }
    }

    if (engine::render::sprite_card_art::isProxyPath(texturePath)) {
        const int maxDim = backendCardArtMaxDim();
        const int scaledW =
            engine::render::sprite_card_art::scaledDimension(width, height, maxDim, true);
        const int scaledH =
            engine::render::sprite_card_art::scaledDimension(width, height, maxDim, false);
        if (scaledW > 0 && scaledH > 0 && (scaledW != width || scaledH != height)) {
            out.resizedPixels = engine::render::sprite_card_art::resizeRgbaBilinear(
                out.pixels.get(),
                width,
                height,
                scaledW,
                scaledH);
            if (!out.resizedPixels.empty()) {
                out.width = scaledW;
                out.height = scaledH;
                out.rgba = out.resizedPixels.data();
                writeCachedProxyPixels(
                    engine::render::sprite_card_art::proxyCachePath(out.sourcePath, maxDim), out);
            }
        }
    }

    return true;
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

    LoadedSpritePixels loaded;
    if (!loadSpritePixels(texturePath, loaded)) {
        SpriteTexture failed;
        failed.valid = false;
        spriteTextures_.emplace(texturePath, failed);
        if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
            spriteTextures_.emplace(loaded.altCacheKey, failed);
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
                                                                          loaded.rgba,
                                                                          loaded.width,
                                                                          loaded.height,
                                                                          kGlClampToEdge,
                                                                          kGlClampToEdge,
                                                                          false,
                                                                          true,
                                                                          texture.resource);
    if (!ok) {
        SpriteTexture failed;
        failed.valid = false;
        spriteTextures_.emplace(texturePath, failed);
        if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
            spriteTextures_.emplace(loaded.altCacheKey, failed);
        }
        return ensureFallbackSpriteTexture();
    }

    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = spriteTextures_.emplace(texturePath, texture);
    if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
        spriteTextures_.emplace(loaded.altCacheKey, texture);
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

void D3D12RenderBackend::prewarmDebugSpriteTextures(const char* const* texturePaths,
                                                    std::size_t textureCount) {
#if defined(_WIN32)
    if (!texturePaths || textureCount == 0u) return;
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return;

    struct PendingSpriteUpload {
        std::string originalPath;
        std::string altCacheKey;
        int width = 0;
        int height = 0;
        const unsigned char* rgba = nullptr;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels{nullptr, stbi_image_free};
        std::vector<unsigned char> resizedPixels;
        std::uint32_t descriptorIndex = 0u;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    };

    std::vector<PendingSpriteUpload> pending;
    pending.reserve(textureCount);
    std::unordered_set<std::string> queuedPaths;
    queuedPaths.reserve(textureCount);
    std::uint32_t reservedDescriptorIndex = nextSrvDescriptorIndex_;

    for (std::size_t i = 0; i < textureCount; ++i) {
        const char* rawPath = texturePaths[i];
        if (!rawPath || rawPath[0] == '\0') continue;

        const std::string texturePath(rawPath);
        if (!queuedPaths.insert(texturePath).second) continue;

        auto existing = spriteTextures_.find(texturePath);
        if (existing != spriteTextures_.end()) {
            continue;
        }
        if (reservedDescriptorIndex >= kMaxSrvDescriptors) break;

        LoadedSpritePixels loaded;
        if (!loadSpritePixels(texturePath, loaded)) {
            SpriteTexture failed;
            failed.valid = false;
            spriteTextures_.emplace(texturePath, failed);
            if (!loaded.altCacheKey.empty() && loaded.altCacheKey != texturePath) {
                spriteTextures_.emplace(loaded.altCacheKey, failed);
            }
            continue;
        }
        if (!loaded.altCacheKey.empty()) {
            auto altExisting = spriteTextures_.find(loaded.altCacheKey);
            if (altExisting != spriteTextures_.end()) {
                spriteTextures_.emplace(texturePath, altExisting->second);
                continue;
            }
        }

        PendingSpriteUpload upload;
        upload.originalPath = texturePath;
        upload.altCacheKey = std::move(loaded.altCacheKey);
        upload.width = loaded.width;
        upload.height = loaded.height;
        upload.rgba = loaded.rgba;
        upload.pixels = std::move(loaded.pixels);
        upload.resizedPixels = std::move(loaded.resizedPixels);
        upload.descriptorIndex = reservedDescriptorIndex++;
        pending.push_back(std::move(upload));
    }

    if (pending.empty()) return;

    std::vector<engine::render::d3d12::RgbaTextureUploadRequest> requests;
    requests.reserve(pending.size());
    for (PendingSpriteUpload& upload : pending) {
        engine::render::d3d12::RgbaTextureUploadRequest request;
        request.rgbaPixels = upload.rgba;
        request.width = upload.width;
        request.height = upload.height;
        request.wrapS = kGlClampToEdge;
        request.wrapT = kGlClampToEdge;
        request.generateMipChain = false;
        request.srgbColorData = true;
        request.descriptorIndex = upload.descriptorIndex;
        request.outTexture = &upload.resource;
        requests.push_back(request);
    }

    if (!engine::render::d3d12::createTextureResourcesFromRgbaBatch(device_.Get(),
                                                                     commandQueue_.Get(),
                                                                     fence_.Get(),
                                                                     static_cast<HANDLE>(fenceEvent_),
                                                                     fenceValue_,
                                                                     srvHeap_.Get(),
                                                                     srvDescriptorSize_,
                                                                     requests.data(),
                                                                     requests.size())) {
        for (const PendingSpriteUpload& upload : pending) {
            (void)ensureSpriteTexture(upload.originalPath);
        }
        return;
    }

    nextSrvDescriptorIndex_ = reservedDescriptorIndex;
    for (PendingSpriteUpload& upload : pending) {
        SpriteTexture texture;
        texture.resource = upload.resource;
        texture.descriptorIndex = upload.descriptorIndex;
        texture.valid = true;
        auto [insertedIt, _] = spriteTextures_.emplace(upload.originalPath, texture);
        if (!upload.altCacheKey.empty() && upload.altCacheKey != upload.originalPath) {
            spriteTextures_.emplace(upload.altCacheKey, texture);
        }
        (void)insertedIt;
    }
#else
    (void)texturePaths;
    (void)textureCount;
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
    const bool tailFireTexture = isTailFireWorldTextureKey(key);
    const auto uploadStart = tailFireTexture ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
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
    if (tailFireTexture) {
        const auto uploadEnd = std::chrono::steady_clock::now();
        std::cout << "[TailFire][D3D12][Upload] key="
                  << key
                  << " size="
                  << width
                  << "x"
                  << height
                  << " srgb="
                  << (srgb ? 1 : 0)
                  << " mips="
                  << (generateMipChain ? 1 : 0)
                  << " wall_ms="
                  << std::chrono::duration<double, std::milli>(uploadEnd - uploadStart).count()
                  << " result="
                  << (ok ? "ok" : "failed")
                  << " note=includes_upload_and_fence_wait\n";
    }
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
