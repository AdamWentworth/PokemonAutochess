#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotAtlasCache.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

#include "engine/core/Environment.h"
#include "engine/core/Paths.h"
#include "engine/render/SpriteTextureCardArt.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAtlasHelpers.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactGpuBatches.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotBillboards.h"

#include <stb_image.h>
#include <stb_image_write.h>

namespace game::runtime::shared_tail_fire_snapshot_billboards {
namespace {

using BackendTextureCacheEntry = SharedBackendTextureCacheEntry;
using WorldIndexedBatch = shared_world_batches::WorldIndexedBatch;
namespace fs = std::filesystem;

struct TailFireCombinedAtlasDiskMeta {
    static constexpr std::uint32_t kMagic = 0x5446414Du;
    static constexpr std::uint32_t kVersion = 1u;

    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t hasSecondary = 0u;
    std::int32_t primaryWidth = 0;
    std::int32_t primaryHeight = 0;
    std::int32_t secondaryWidth = 0;
    std::int32_t secondaryHeight = 0;
};

std::unordered_map<std::string, TailFireCombinedAtlasDiskMeta> g_tailFireCombinedAtlasMetaByCacheKey;

int tailFireCombinedAtlasMaxDim() {
    const auto env = engine::env::get("PAC_BACKEND_TAIL_FIRE_ATLAS_MAX_DIM");
    if (!env || env->empty()) return 2048;
    try {
        return std::max(0, std::stoi(*env));
    } catch (...) {
        return 2048;
    }
}

std::string hexHash64(std::uint64_t v) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << v;
    return oss.str();
}

std::uint64_t fnv1a64(const std::string& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

fs::path resolveExistingPath(const std::string& sourcePath) {
    std::error_code ec;
    fs::path direct(sourcePath);
    if (fs::exists(direct, ec) && !ec) return direct;
    std::string alt = sourcePath;
    std::replace(alt.begin(), alt.end(), '\\', '/');
    fs::path normalized(alt);
    ec.clear();
    if (fs::exists(normalized, ec) && !ec) return normalized;
    return direct;
}

std::string tailFireCombinedAtlasCacheFingerprint(const fs::path& primaryPath,
                                                  const fs::path* secondaryPath,
                                                  int maxDim) {
    auto appendFileMeta = [](std::string& out, const fs::path& path) {
        std::error_code ec;
        out += path.generic_string();
        out += "|size=";
        out += std::to_string(fs::exists(path, ec) && !ec ? fs::file_size(path, ec) : 0ull);
        out += "|mtime=";
        if (!ec && fs::exists(path, ec)) {
            out += std::to_string(fs::last_write_time(path, ec).time_since_epoch().count());
        } else {
            out += "0";
        }
    };

    std::string fingerprint;
    fingerprint.reserve(256u);
    fingerprint += "tailfire_combined|cap=";
    fingerprint += std::to_string(maxDim);
    fingerprint += "|";
    appendFileMeta(fingerprint, primaryPath);
    fingerprint += "|secondary=";
    if (secondaryPath) {
        appendFileMeta(fingerprint, *secondaryPath);
    } else {
        fingerprint += "<none>";
    }
    return fingerprint;
}

fs::path tailFireCombinedAtlasCachePath(const fs::path& primaryPath,
                                        const fs::path* secondaryPath,
                                        int maxDim) {
    const std::string fingerprint =
        tailFireCombinedAtlasCacheFingerprint(primaryPath, secondaryPath, maxDim);
    return fs::path(engine::paths::data("cache/tail_fire")) /
           ("combined_" + hexHash64(fnv1a64(fingerprint)) + ".png");
}

fs::path tailFireCombinedAtlasMetaPath(const fs::path& pngPath) {
    fs::path metaPath = pngPath;
    metaPath.replace_extension(".meta");
    return metaPath;
}

bool loadCachedCombinedAtlasPng(const fs::path& path,
                                BackendTextureCacheEntry& out,
                                double& loadMs) {
    loadMs = 0.0;
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return false;

    const auto loadStart = std::chrono::steady_clock::now();
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* loaded =
        stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    const auto loadEnd = std::chrono::steady_clock::now();
    loadMs = std::chrono::duration<double, std::milli>(loadEnd - loadStart).count();
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(loaded, stbi_image_free);
    if (!pixels || width <= 0 || height <= 0) return false;

    out.width = width;
    out.height = height;
    out.rgba.assign(
        pixels.get(),
        pixels.get() + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    out.valid = !out.rgba.empty();
    return out.valid;
}

bool writeCachedCombinedAtlasPng(const fs::path& path,
                                 const BackendTextureCacheEntry& atlas,
                                 double& writeMs) {
    writeMs = 0.0;
    if (!atlas.valid || atlas.width <= 0 || atlas.height <= 0 || atlas.rgba.empty()) return false;

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const auto writeStart = std::chrono::steady_clock::now();
    const int ok = stbi_write_png(
        path.string().c_str(),
        atlas.width,
        atlas.height,
        4,
        atlas.rgba.data(),
        atlas.width * 4);
    const auto writeEnd = std::chrono::steady_clock::now();
    writeMs = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();
    return ok != 0;
}

bool loadCachedCombinedAtlasMeta(const fs::path& path,
                                 TailFireCombinedAtlasDiskMeta& out,
                                 double& loadMs) {
    loadMs = 0.0;
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return false;

    const auto loadStart = std::chrono::steady_clock::now();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    TailFireCombinedAtlasDiskMeta meta{};
    in.read(reinterpret_cast<char*>(&meta), sizeof(meta));
    const auto loadEnd = std::chrono::steady_clock::now();
    loadMs = std::chrono::duration<double, std::milli>(loadEnd - loadStart).count();
    if (!in || meta.magic != TailFireCombinedAtlasDiskMeta::kMagic ||
        meta.version != TailFireCombinedAtlasDiskMeta::kVersion ||
        meta.primaryWidth <= 0 || meta.primaryHeight <= 0) {
        return false;
    }
    out = meta;
    return true;
}

bool writeCachedCombinedAtlasMeta(const fs::path& path,
                                  const TailFireCombinedAtlasDiskMeta& meta,
                                  double& writeMs) {
    writeMs = 0.0;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const auto writeStart = std::chrono::steady_clock::now();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(&meta), sizeof(meta));
    const auto writeEnd = std::chrono::steady_clock::now();
    writeMs = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();
    return static_cast<bool>(out);
}

} // namespace

BackendTextureCacheEntry* resolveTailFirePremulAtlas(
    const std::string& atlasPath,
    std::unordered_map<std::string, BackendTextureCacheEntry>& backendTextureByPath,
    const std::function<BackendTextureCacheEntry*(const std::string&, bool)>& ensureTextureFn) {
    if (atlasPath.empty()) return nullptr;
    if (!ensureTextureFn) return nullptr;

    const std::string key = std::string("__tailfire_premul:") + atlasPath;
    if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
    auto& baked = backendTextureByPath[key];
    if (baked.attemptedLoad) return baked.valid ? &baked : nullptr;

    // Legacy ParticleSystem loads VFX flipbooks with stb vertical flip enabled.
    // Match that texture orientation here so the shared fire_tail UV logic aligns.
    const auto rawLoadStart = std::chrono::steady_clock::now();
    BackendTextureCacheEntry* src = ensureTextureFn(atlasPath, true);
    const auto rawLoadEnd = std::chrono::steady_clock::now();
    if (!src || !src->valid || src->rgba.empty() || src->width <= 0 || src->height <= 0) {
        std::cout << "[TailFire][CPU] premul_atlas path="
                  << atlasPath
                  << " raw_load_ms="
                  << std::chrono::duration<double, std::milli>(rawLoadEnd - rawLoadStart).count()
                  << " result=raw_load_failed\n";
        return nullptr;
    }

    baked.attemptedLoad = true;
    baked.valid = false;
    const auto bakeStart = std::chrono::steady_clock::now();
    game::runtime::shared_tail_fire_atlas::RgbaTextureOwned premul;
    const game::runtime::shared_tail_fire_atlas::RgbaTextureView srcView{
        src->rgba.data(), src->width, src->height};
    if (!game::runtime::shared_tail_fire_atlas::buildPremultipliedAtlas(srcView, premul)) {
        const auto bakeEnd = std::chrono::steady_clock::now();
        std::cout << "[TailFire][CPU] premul_atlas path="
                  << atlasPath
                  << " raw_load_ms="
                  << std::chrono::duration<double, std::milli>(rawLoadEnd - rawLoadStart).count()
                  << " bake_ms="
                  << std::chrono::duration<double, std::milli>(bakeEnd - bakeStart).count()
                  << " result=bake_failed\n";
        return nullptr;
    }
    const auto bakeEnd = std::chrono::steady_clock::now();

    baked.width = premul.width;
    baked.height = premul.height;
    baked.rgba = std::move(premul.rgba);
    baked.valid = (baked.width > 0 && baked.height > 0 && !baked.rgba.empty());
    std::cout << "[TailFire][CPU] premul_atlas path="
              << atlasPath
              << " raw_load_ms="
              << std::chrono::duration<double, std::milli>(rawLoadEnd - rawLoadStart).count()
              << " bake_ms="
              << std::chrono::duration<double, std::milli>(bakeEnd - bakeStart).count()
              << " size="
              << baked.width
              << "x"
              << baked.height
              << " result="
              << (baked.valid ? "ok" : "invalid")
              << "\n";
    return &baked;
}

TailFireCombinedAtlasInfo resolveTailFireCombinedAtlas(
    const ParticleSystem::RenderSnapshot& snapshot,
    std::unordered_map<std::string, BackendTextureCacheEntry>& backendTextureByPath,
    const std::function<BackendTextureCacheEntry*(const std::string&, bool)>& ensureTextureFn) {
    TailFireCombinedAtlasInfo out;
    if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return out;
    if (!ensureTextureFn) return out;

    const auto combinedResolveStart = std::chrono::steady_clock::now();
    const bool wantsSecondary = snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty();
    const fs::path primaryDiskPath = resolveExistingPath(snapshot.flipbookPath);
    const fs::path secondaryDiskPath = wantsSecondary ? resolveExistingPath(snapshot.flipbookPath2) : fs::path();
    const int maxCombinedDim = tailFireCombinedAtlasMaxDim();
    int primaryWidth = 0;
    int primaryHeight = 0;
    int secondaryWidth = 0;
    int secondaryHeight = 0;
    int sourceCombinedWidth = 0;
    int sourceCombinedHeight = 0;
    double rawPrimaryLoadMs = 0.0;
    double rawSecondaryLoadMs = 0.0;
    double cacheMetaLoadMs = 0.0;
    double cacheImageLoadMs = 0.0;
    double cacheWriteMs = 0.0;
    double resizeMs = 0.0;
    double bakeMs = 0.0;
    bool cacheHit = false;
    TailFireCombinedAtlasDiskMeta diskMeta{};
    const fs::path diskCachePath = tailFireCombinedAtlasCachePath(
        primaryDiskPath,
        wantsSecondary ? &secondaryDiskPath : nullptr,
        maxCombinedDim);
    const fs::path metaCachePath = tailFireCombinedAtlasMetaPath(diskCachePath);

    out.cacheKey = std::string("__tailfire_combined_exact__:") +
                   snapshot.flipbookPath +
                   "|" +
                   (wantsSecondary ? snapshot.flipbookPath2 : std::string()) +
                   "|cap=" +
                   (maxCombinedDim > 0 ? std::to_string(maxCombinedDim) : std::string("full"));
    if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
    auto& combined = backendTextureByPath[out.cacheKey];
    auto metaIt = g_tailFireCombinedAtlasMetaByCacheKey.find(out.cacheKey);
    if (metaIt != g_tailFireCombinedAtlasMetaByCacheKey.end()) {
        diskMeta = metaIt->second;
    }

    if (!combined.attemptedLoad) {
        combined.attemptedLoad = true;
        combined.valid = false;

        if (metaIt == g_tailFireCombinedAtlasMetaByCacheKey.end() &&
            loadCachedCombinedAtlasMeta(metaCachePath, diskMeta, cacheMetaLoadMs)) {
            g_tailFireCombinedAtlasMetaByCacheKey[out.cacheKey] = diskMeta;
            metaIt = g_tailFireCombinedAtlasMetaByCacheKey.find(out.cacheKey);
        }

        if (metaIt != g_tailFireCombinedAtlasMetaByCacheKey.end() &&
            loadCachedCombinedAtlasPng(diskCachePath, combined, cacheImageLoadMs)) {
            cacheHit = true;
            diskMeta = metaIt->second;
        } else {
            const auto primaryLoadStart = std::chrono::steady_clock::now();
            BackendTextureCacheEntry* primaryRaw = ensureTextureFn(snapshot.flipbookPath, true);
            const auto primaryLoadEnd = std::chrono::steady_clock::now();
            rawPrimaryLoadMs =
                std::chrono::duration<double, std::milli>(primaryLoadEnd - primaryLoadStart).count();
            if (!primaryRaw || !primaryRaw->valid || primaryRaw->rgba.empty() ||
                primaryRaw->width <= 0 || primaryRaw->height <= 0) {
                std::cout << "[TailFire][CPU] combined_atlas primary="
                          << snapshot.flipbookPath
                          << " secondary="
                          << (wantsSecondary ? snapshot.flipbookPath2 : std::string("<disabled>"))
                          << " raw_primary_ms="
                          << rawPrimaryLoadMs
                          << " raw_secondary_ms=0 cache=miss result=primary_raw_load_failed\n";
                return out;
            }

            BackendTextureCacheEntry* secondaryRaw = nullptr;
            if (wantsSecondary) {
                const auto secondaryLoadStart = std::chrono::steady_clock::now();
                secondaryRaw = ensureTextureFn(snapshot.flipbookPath2, true);
                const auto secondaryLoadEnd = std::chrono::steady_clock::now();
                rawSecondaryLoadMs =
                    std::chrono::duration<double, std::milli>(secondaryLoadEnd - secondaryLoadStart).count();
                if (!(secondaryRaw && secondaryRaw->valid && !secondaryRaw->rgba.empty() &&
                      secondaryRaw->width > 0 && secondaryRaw->height > 0)) {
                    secondaryRaw = nullptr;
                }
            }

            out.hasSecondary = (secondaryRaw != nullptr);
            sourceCombinedWidth =
                primaryRaw->width + (out.hasSecondary ? (2 + secondaryRaw->width) : 0);
            sourceCombinedHeight =
                std::max(primaryRaw->height, out.hasSecondary ? secondaryRaw->height : 0);
            const int longestCombinedDim = std::max(sourceCombinedWidth, sourceCombinedHeight);
            const bool downscaleCombinedAtlas =
                maxCombinedDim > 0 && longestCombinedDim > maxCombinedDim;
            const float downscaleFactor =
                downscaleCombinedAtlas
                    ? (static_cast<float>(maxCombinedDim) /
                       static_cast<float>(std::max(1, longestCombinedDim)))
                    : 1.0f;

            primaryWidth = downscaleCombinedAtlas
                ? std::max(
                      1,
                      static_cast<int>(std::lround(static_cast<float>(primaryRaw->width) * downscaleFactor)))
                : primaryRaw->width;
            primaryHeight = downscaleCombinedAtlas
                ? std::max(
                      1,
                      static_cast<int>(std::lround(static_cast<float>(primaryRaw->height) * downscaleFactor)))
                : primaryRaw->height;
            secondaryWidth = out.hasSecondary
                ? (downscaleCombinedAtlas
                       ? std::max(
                             1,
                             static_cast<int>(
                                 std::lround(static_cast<float>(secondaryRaw->width) * downscaleFactor)))
                       : secondaryRaw->width)
                : 0;
            secondaryHeight = out.hasSecondary
                ? (downscaleCombinedAtlas
                       ? std::max(
                             1,
                             static_cast<int>(
                                 std::lround(static_cast<float>(secondaryRaw->height) * downscaleFactor)))
                       : secondaryRaw->height)
                : 0;

            std::vector<unsigned char> primaryScaledRgba;
            std::vector<unsigned char> secondaryScaledRgba;
            if (downscaleCombinedAtlas) {
                const auto resizeStart = std::chrono::steady_clock::now();
                primaryScaledRgba = engine::render::sprite_card_art::resizeRgbaBilinear(
                    primaryRaw->rgba.data(),
                    primaryRaw->width,
                    primaryRaw->height,
                    primaryWidth,
                    primaryHeight);
                if (primaryScaledRgba.empty()) {
                    primaryWidth = primaryRaw->width;
                    primaryHeight = primaryRaw->height;
                }
                if (out.hasSecondary) {
                    secondaryScaledRgba = engine::render::sprite_card_art::resizeRgbaBilinear(
                        secondaryRaw->rgba.data(),
                        secondaryRaw->width,
                        secondaryRaw->height,
                        secondaryWidth,
                        secondaryHeight);
                    if (secondaryScaledRgba.empty()) {
                        secondaryWidth = secondaryRaw->width;
                        secondaryHeight = secondaryRaw->height;
                    }
                }
                const auto resizeEnd = std::chrono::steady_clock::now();
                resizeMs =
                    std::chrono::duration<double, std::milli>(resizeEnd - resizeStart).count();
            }

            const auto bakeStart = std::chrono::steady_clock::now();
            game::runtime::shared_tail_fire_atlas::RgbaTextureOwned builtAtlas;
            game::runtime::shared_tail_fire_atlas::CombinedAtlasInfo builtInfo;
            const game::runtime::shared_tail_fire_atlas::RgbaTextureView primaryView{
                primaryScaledRgba.empty() ? primaryRaw->rgba.data() : primaryScaledRgba.data(),
                primaryScaledRgba.empty() ? primaryRaw->width : primaryWidth,
                primaryScaledRgba.empty() ? primaryRaw->height : primaryHeight};
            game::runtime::shared_tail_fire_atlas::RgbaTextureView secondaryView{};
            const game::runtime::shared_tail_fire_atlas::RgbaTextureView* secondaryViewPtr = nullptr;
            if (out.hasSecondary) {
                secondaryView = {
                    secondaryScaledRgba.empty() ? secondaryRaw->rgba.data() : secondaryScaledRgba.data(),
                    secondaryScaledRgba.empty() ? secondaryRaw->width : secondaryWidth,
                    secondaryScaledRgba.empty() ? secondaryRaw->height : secondaryHeight};
                secondaryViewPtr = &secondaryView;
            }

            if (game::runtime::shared_tail_fire_atlas::buildCombinedAtlas(
                    primaryView, secondaryViewPtr, builtAtlas, builtInfo)) {
                combined.width = builtAtlas.width;
                combined.height = builtAtlas.height;
                combined.rgba = std::move(builtAtlas.rgba);
                combined.valid = (combined.width > 0 && combined.height > 0 && !combined.rgba.empty());
                out.hasSecondary = builtInfo.hasSecondary;
                out.rect0 = builtInfo.rect0;
                out.rect1 = builtInfo.rect1;
                if (combined.valid) {
                    TailFireCombinedAtlasDiskMeta newMeta;
                    newMeta.hasSecondary = builtInfo.hasSecondary ? 1u : 0u;
                    newMeta.primaryWidth =
                        primaryScaledRgba.empty() ? primaryRaw->width : primaryWidth;
                    newMeta.primaryHeight =
                        primaryScaledRgba.empty() ? primaryRaw->height : primaryHeight;
                    newMeta.secondaryWidth =
                        builtInfo.hasSecondary
                            ? (secondaryScaledRgba.empty() ? secondaryRaw->width : secondaryWidth)
                            : 0;
                    newMeta.secondaryHeight =
                        builtInfo.hasSecondary
                            ? (secondaryScaledRgba.empty() ? secondaryRaw->height : secondaryHeight)
                            : 0;
                    double pngWriteMs = 0.0;
                    double metaWriteMs = 0.0;
                    writeCachedCombinedAtlasPng(diskCachePath, combined, pngWriteMs);
                    writeCachedCombinedAtlasMeta(metaCachePath, newMeta, metaWriteMs);
                    cacheWriteMs = pngWriteMs + metaWriteMs;
                    diskMeta = newMeta;
                    g_tailFireCombinedAtlasMetaByCacheKey[out.cacheKey] = newMeta;
                }
            }
            const auto bakeEnd = std::chrono::steady_clock::now();
            bakeMs = std::chrono::duration<double, std::milli>(bakeEnd - bakeStart).count();
        }

        const auto combinedResolveEnd = std::chrono::steady_clock::now();
        if (diskMeta.primaryWidth > 0 && diskMeta.primaryHeight > 0) {
            primaryWidth = diskMeta.primaryWidth;
            primaryHeight = diskMeta.primaryHeight;
            secondaryWidth = diskMeta.secondaryWidth;
            secondaryHeight = diskMeta.secondaryHeight;
            out.hasSecondary = (diskMeta.hasSecondary != 0u);
            sourceCombinedWidth =
                primaryWidth + (out.hasSecondary ? (2 + secondaryWidth) : 0);
            sourceCombinedHeight =
                std::max(primaryHeight, out.hasSecondary ? secondaryHeight : 0);
        }

        std::cout << "[TailFire][CPU] combined_atlas primary="
                  << snapshot.flipbookPath
                  << " secondary="
                  << (wantsSecondary ? snapshot.flipbookPath2 : std::string("<disabled>"))
                  << " raw_primary_ms="
                  << rawPrimaryLoadMs
                  << " raw_secondary_ms="
                  << rawSecondaryLoadMs
                  << " cache="
                  << (cacheHit ? "hit" : "miss")
                  << " cache_meta_ms="
                  << cacheMetaLoadMs
                  << " cache_load_ms="
                  << cacheImageLoadMs
                  << " cache_write_ms="
                  << cacheWriteMs
                  << " resize_ms="
                  << resizeMs
                  << " bake_ms="
                  << bakeMs
                  << " total_ms="
                  << std::chrono::duration<double, std::milli>(combinedResolveEnd - combinedResolveStart).count()
                  << " src_size="
                  << sourceCombinedWidth
                  << "x"
                  << sourceCombinedHeight
                  << " size="
                  << combined.width
                  << "x"
                  << combined.height
                  << " cap="
                  << (maxCombinedDim > 0 ? std::to_string(maxCombinedDim) : std::string("full"))
                  << " result="
                  << (combined.valid ? "ok" : "invalid")
                  << "\n";
    }

    if (!combined.valid || combined.rgba.empty() || combined.width <= 0 || combined.height <= 0) {
        return {};
    }

    if (primaryWidth <= 0 || primaryHeight <= 0) {
        auto metaCacheIt = g_tailFireCombinedAtlasMetaByCacheKey.find(out.cacheKey);
        if (metaCacheIt == g_tailFireCombinedAtlasMetaByCacheKey.end()) {
            double cacheMetaLoadMsUnused = 0.0;
            if (loadCachedCombinedAtlasMeta(metaCachePath, diskMeta, cacheMetaLoadMsUnused)) {
                g_tailFireCombinedAtlasMetaByCacheKey[out.cacheKey] = diskMeta;
                metaCacheIt = g_tailFireCombinedAtlasMetaByCacheKey.find(out.cacheKey);
            }
        }
        if (metaCacheIt != g_tailFireCombinedAtlasMetaByCacheKey.end()) {
            const auto& meta = metaCacheIt->second;
            primaryWidth = meta.primaryWidth;
            primaryHeight = meta.primaryHeight;
            secondaryWidth = meta.secondaryWidth;
            secondaryHeight = meta.secondaryHeight;
            out.hasSecondary = (meta.hasSecondary != 0u);
        }
    }

    out.atlas = &combined;
    if (out.rect0 == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) &&
        out.rect1 == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)) {
        const bool primaryDimsScaled = primaryWidth > 0 && primaryHeight > 0;
        const bool secondaryDimsScaled =
            out.hasSecondary && secondaryWidth > 0 && secondaryHeight > 0;
        const float invW = 1.0f / static_cast<float>(std::max(1, combined.width));
        const float invH = 1.0f / static_cast<float>(std::max(1, combined.height));
        out.rect0 = glm::vec4(
            0.0f,
            0.0f,
            static_cast<float>(primaryDimsScaled ? primaryWidth : combined.width) * invW,
            static_cast<float>(primaryDimsScaled ? primaryHeight : combined.height) * invH);
        if (out.hasSecondary) {
            const int gutter = 2;
            out.rect1 = glm::vec4(
                static_cast<float>((primaryDimsScaled ? primaryWidth : combined.width) + gutter) * invW,
                0.0f,
                static_cast<float>(secondaryDimsScaled ? secondaryWidth : 0) * invW,
                static_cast<float>(secondaryDimsScaled ? secondaryHeight : 0) * invH);
        } else {
            out.rect1 = out.rect0;
        }
    }
    return out;
}

bool appendTailFireExactGpuBatch(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    std::uint8_t blendMode,
    const AppendContext& ctx,
    std::vector<WorldIndexedBatch>& worldIndexedBatches) {
    if (!label || snapshot.particles.empty()) return false;
    if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return false;

    TailFireCombinedAtlasInfo atlasInfo =
        resolveTailFireCombinedAtlas(snapshot, ctx.backendTextureByPath, ctx.ensureTextureFn);
    if (!atlasInfo.atlas || !atlasInfo.atlas->valid || atlasInfo.atlas->rgba.empty()) {
        return false;
    }

    game::runtime::shared_tail_fire_exact_gpu::BuildContext tailCtx;
    tailCtx.viewProj = ctx.viewProj;
    tailCtx.invViewProj = ctx.invViewProj;
    tailCtx.cameraWorldPos = ctx.cameraWorldPos;
    tailCtx.drawableW = ctx.drawableW;
    tailCtx.drawableH = ctx.drawableH;
    tailCtx.blendMode = blendMode;

    game::runtime::shared_tail_fire_exact_gpu::AtlasView tailAtlas;
    tailAtlas.rgba = atlasInfo.atlas->rgba.data();
    tailAtlas.width = atlasInfo.atlas->width;
    tailAtlas.height = atlasInfo.atlas->height;
    tailAtlas.cacheKey = atlasInfo.cacheKey;
    tailAtlas.rect0 = atlasInfo.rect0;
    tailAtlas.rect1 = atlasInfo.rect1;
    tailAtlas.hasSecondary = atlasInfo.hasSecondary;

    return game::runtime::shared_tail_fire_exact_gpu::appendBatch(
        label, snapshot, tailCtx, tailAtlas, worldIndexedBatches);
}

} // namespace game::runtime::shared_tail_fire_snapshot_billboards
