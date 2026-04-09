#include "game/runtime/session/SessionStartupBridge.h"

#include "game/runtime/session/SessionBackendAssetBridge.h"
#include "game/runtime/session/SessionStartupRuntime.h"

namespace game::runtime::session_startup_bridge {

namespace {

startup_asset_prewarm::AuthoredVfxPrewarmEntry makeAuthoredVfxPrewarmEntry(
    startup_asset_prewarm::AuthoredVfxKind kind,
    std::string title,
    float progress,
    std::function<startup_asset_prewarm::AuthoredVfxStats()> prewarm) {
    return startup_asset_prewarm::AuthoredVfxPrewarmEntry{
        .kind = kind,
        .title = std::move(title),
        .progress = progress,
        .prewarm = std::move(prewarm),
    };
}

} // namespace

void run(const Context& context) {
    if (!context.backendAssets || !context.consoleLog) return;

    session_startup_runtime::run(
        {
            .ctx = context.ctx,
            .renderer = context.renderer,
            .engineServices = context.engineServices,
            .dataDb = context.dataDb,
            .config = context.config,
            .services = context.services,
            .gameWorld = context.gameWorld,
            .stateManager = context.stateManager,
            .log = context.log,
            .worldLayerPrewarmFramesRemaining = context.worldLayerPrewarmFramesRemaining,
            .worldLayerPrewarmFrameCount = context.worldLayerPrewarmFrameCount,
            .snapshotPath = context.snapshotPath,
            .autoLoadSnapshotOnStartup = context.autoLoadSnapshotOnStartup,
            .usesBackendGameRenderPath = context.usesBackendGameRenderPath,
            .loadModel =
                [&](const std::string& modelPath) {
                    return session_backend_asset_bridge::loadModelForStartupPrewarm(
                        *context.backendAssets,
                        modelPath);
                },
            .prewarmAnimRoles =
                [&](const std::string& modelPath,
                    const render_model::MeshData& mesh) {
                    return session_backend_asset_bridge::prewarmAnimRolesForStartupPrewarm(
                        *context.backendAssets,
                        modelPath,
                        mesh);
                },
            .prewarmTextures =
                [&](const std::string&, const render_model::MeshData& mesh) {
                    return session_backend_asset_bridge::prewarmTexturesForStartupPrewarm(
                        context.renderer,
                        mesh);
                },
            .prewarmGeometry =
                [&](const render_model::MeshData& mesh) {
                    return session_backend_asset_bridge::prewarmGeometryForStartupPrewarm(
                        context.renderer,
                        mesh);
                },
            .prewarmTailFire =
                [&]() {
                    return session_backend_asset_bridge::prewarmTailFire(
                        *context.backendAssets,
                        context.renderer);
                },
            .prewarmAuthoredVfx =
                {
                    makeAuthoredVfxPrewarmEntry(
                        startup_asset_prewarm::AuthoredVfxKind::Growl,
                        "PokemonAutochess - Loading growl VFX...",
                        0.935f,
                        [&]() {
                            return startup_asset_prewarm::toAuthoredVfxStats(
                                session_backend_asset_bridge::prewarmGrowlVfx(
                                    *context.backendAssets,
                                    context.renderer,
                                    *context.consoleLog));
                        }),
                    makeAuthoredVfxPrewarmEntry(
                        startup_asset_prewarm::AuthoredVfxKind::Tackle,
                        "PokemonAutochess - Loading tackle VFX...",
                        0.936f,
                        [&]() {
                            return startup_asset_prewarm::toAuthoredVfxStats(
                                session_backend_asset_bridge::prewarmTackleVfx(
                                    *context.backendAssets,
                                    context.renderer,
                                    *context.consoleLog));
                        }),
                },
            .prewarmParticleVfx =
                [&]() {
                    return session_backend_asset_bridge::prewarmParticleVfx(
                        *context.backendAssets,
                        context.renderer);
                },
            .renderWorldLayer = context.renderWorldLayer,
        });
}

} // namespace game::runtime::session_startup_bridge
