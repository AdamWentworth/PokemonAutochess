#include "game/runtime/session/SessionTailFirePrewarm.h"

#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotAtlasCache.h"
#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"

#include <chrono>
#include <iostream>
#include <string>

namespace game::runtime::session_tail_fire_prewarm {

startup_asset_prewarm::TailFireStats prewarm(const Args& args) {
    if (!args.renderer || !args.backendTextureByPath || !args.ensureBackendTextureLoaded) {
        return {};
    }

    const TailFireVFXConfig& cfg =
        game::runtime::shared_projected_scene::getTailFireFallbackCfg();
    if (!cfg.useFlipbook || cfg.flipbookPath.empty()) return {};

    ParticleSystem::RenderSnapshot snapshot{};
    snapshot.useFlipbook = cfg.useFlipbook;
    snapshot.flipbookPath = cfg.flipbookPath;
    snapshot.flipbookCols = cfg.flipbookCols;
    snapshot.flipbookRows = cfg.flipbookRows;
    snapshot.flipbookFrames = cfg.flipbookFrames;
    snapshot.flipbookFps = cfg.flipbookFps;
    snapshot.useSecondaryFlipbook = cfg.useFlipbook2 && !cfg.flipbook2Path.empty();
    snapshot.flipbookPath2 = cfg.flipbook2Path;
    snapshot.flipbookCols2 = cfg.flipbook2Cols;
    snapshot.flipbookRows2 = cfg.flipbook2Rows;
    snapshot.flipbookFrames2 = cfg.flipbook2Frames;
    snapshot.flipbookFps2 = cfg.flipbook2Fps;

    auto ensureTextureFn =
        [&](const std::string& path, bool flip) -> SharedBackendTextureCacheEntry* {
            return args.ensureBackendTextureLoaded(path, flip);
        };

    startup_asset_prewarm::TailFireStats warmed{};
    const auto prewarmAtlas = [&](const std::string& key,
                                  const SharedBackendTextureCacheEntry* atlas) {
        if (!atlas || !atlas->valid || atlas->rgba.empty() ||
            atlas->width <= 0 || atlas->height <= 0) {
            return;
        }

        IRenderBackend::WorldTextureData tex{};
        tex.key = key.c_str();
        tex.rgba = atlas->rgba.data();
        tex.width = atlas->width;
        tex.height = atlas->height;
        tex.wrapS = 33071; // GL_CLAMP_TO_EDGE
        tex.wrapT = 33071; // GL_CLAMP_TO_EDGE
        tex.alphaMode = 2u;
        tex.blendMode = 2u;
        args.renderer->prewarmWorldTextureData(&tex);
        ++warmed.legacyAtlases;
    };

    const auto combined =
        game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFireCombinedAtlas(
            snapshot,
            *args.backendTextureByPath,
            ensureTextureFn);
    if (!combined.cacheKey.empty()) {
        prewarmAtlas(combined.cacheKey, combined.atlas);
    }

    const bool prewarmLegacyPremul =
        game::runtime::session_render_config::backendPrewarmLegacyTailFirePremulEnabled() ||
        !snapshot.useSecondaryFlipbook ||
        !(combined.atlas && combined.atlas->valid);
    if (prewarmLegacyPremul) {
        const std::string primaryPremulKey =
            std::string("__tailfire_premul:") + snapshot.flipbookPath;
        SharedBackendTextureCacheEntry* primaryPremul =
            game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFirePremulAtlas(
                snapshot.flipbookPath,
                *args.backendTextureByPath,
                ensureTextureFn);
        prewarmAtlas(primaryPremulKey, primaryPremul);

        if (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty()) {
            const std::string secondaryPremulKey =
                std::string("__tailfire_premul:") + snapshot.flipbookPath2;
            SharedBackendTextureCacheEntry* secondaryPremul =
                game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFirePremulAtlas(
                    snapshot.flipbookPath2,
                    *args.backendTextureByPath,
                    ensureTextureFn);
            prewarmAtlas(secondaryPremulKey, secondaryPremul);
        }
    }

    const auto& authoredSpecs =
        game::runtime::shared_tail_fire_mesh_playback::authoredFlipbookSpecs();
    if (!authoredSpecs.empty()) {
        const auto& charmanderSpec = authoredSpecs.front();
        if (charmanderSpec.path && charmanderSpec.path[0] != '\0') {
            const auto cpuLoadStart = std::chrono::steady_clock::now();
            SharedBackendTextureCacheEntry* authoredCpuTexture =
                args.ensureBackendTextureLoaded(charmanderSpec.path, false);
            const auto cpuLoadEnd = std::chrono::steady_clock::now();
            std::cout << "[TailFire][CPU] authored_mesh_flipbook path="
                      << charmanderSpec.path
                      << " load_ms="
                      << std::chrono::duration<double, std::milli>(cpuLoadEnd - cpuLoadStart).count()
                      << " size="
                      << ((authoredCpuTexture && authoredCpuTexture->valid) ? authoredCpuTexture->width : 0)
                      << "x"
                      << ((authoredCpuTexture && authoredCpuTexture->valid) ? authoredCpuTexture->height : 0)
                      << " result="
                      << ((authoredCpuTexture && authoredCpuTexture->valid) ? "ok" : "failed")
                      << "\n";
            if (authoredCpuTexture && authoredCpuTexture->valid) {
                ++warmed.meshFlipbookCpu;
                IRenderBackend::WorldTextureData tex{};
                tex.key = charmanderSpec.path;
                tex.cacheKey = charmanderSpec.path;
                tex.rgba = authoredCpuTexture->rgba.data();
                tex.width = authoredCpuTexture->width;
                tex.height = authoredCpuTexture->height;
                tex.wrapS = 33071; // GL_CLAMP_TO_EDGE
                tex.wrapT = 33071; // GL_CLAMP_TO_EDGE
                tex.alphaMode = 1u;
                tex.blendMode = 0u;
                args.renderer->prewarmWorldTextureData(&tex);
                ++warmed.meshFlipbookGpu;
            }
        }
    }

    return warmed;
}

} // namespace game::runtime::session_tail_fire_prewarm
