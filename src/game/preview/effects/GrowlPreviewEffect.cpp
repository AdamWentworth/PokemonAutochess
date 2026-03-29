#include "game/preview/effects/GrowlPreviewEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Paths.h"
#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBridge.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBatches.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

namespace game::preview {

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;
constexpr float kLoopCooldownSec = 0.18f;

glm::vec3 safeForwardXZ(const glm::vec3& value) {
    glm::vec3 forward(value.x, 0.0f, value.z);
    const float lenSq = glm::dot(forward, forward);
    if (lenSq <= 0.000001f) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return forward / std::sqrt(lenSq);
}

} // namespace

class GrowlPreviewEffect::SharedRenderer {
public:
    void onResize(int width, int height) {
        backend_.onResize(width, height);
    }

    void render(const GrowlWaveVFX& effect,
                const Camera3D& camera,
                int surfaceWidth,
                int surfaceHeight) {
        GrowlWaveVFX::RenderSnapshot snapshot;
        if (!effect.buildRenderSnapshot(snapshot)) return;

        std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> batches;
        batches.reserve(snapshot.drawPasses.size() * 4u);

        const auto resolveMesh =
            [&](const std::string& modelPath) -> game::runtime::render_model::MeshData* {
                return ensureBackendMeshLoaded(modelPath);
            };

        const auto resolveTexture =
            [&](const GrowlWaveVFX::Config::DrawPass& pass,
                const game::runtime::shared_growl::TevState& tev,
                game::runtime::shared_growl_batches::TextureView& outView) -> bool {
                return fillTextureView(pass, snapshot.config, tev, outView);
            };

        if (!game::runtime::shared_growl_bridge::appendBatches(
                snapshot,
                batches,
                camera.getPosition(),
                resolveMesh,
                resolveTexture)) {
            return;
        }

        const glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
        const glm::vec3 cameraPos = camera.getPosition();
        const glm::vec3 cameraForward = camera.getDirection();
        const glm::vec3 cameraTarget = camera.getTarget();
        game::runtime::shared_world_batches::submitWorldIndexedBatches(
            backend_,
            batches,
            glm::value_ptr(viewProj),
            surfaceWidth,
            surfaceHeight,
            glm::value_ptr(cameraPos),
            glm::value_ptr(cameraForward),
            glm::value_ptr(cameraTarget));
    }

private:
    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        bool reportedFailure = false;
        game::runtime::render_model::MeshData mesh;
        std::string error;
    };

    game::runtime::render_model::MeshData* ensureBackendMeshLoaded(const std::string& modelPath) {
        auto& cacheEntry = backendMeshByModelPath_[modelPath];
        if (!cacheEntry.attemptedLoad) {
            cacheEntry.attemptedLoad = true;
            std::string err;
            if (!game::runtime::render_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
                cacheEntry.error = std::move(err);
                cacheEntry.mesh = {};
            }
        }

        if (!cacheEntry.error.empty()) {
            if (!cacheEntry.reportedFailure) {
                std::cout << "[VfxPreviewer] Unable to load cached mesh '" << modelPath
                          << "' (" << cacheEntry.error << ")\n";
                cacheEntry.reportedFailure = true;
            }
            return nullptr;
        }
        if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.empty()) return nullptr;
        return &cacheEntry.mesh;
    }

    game::runtime::SharedBackendTextureCacheEntry* ensureBackendTextureLoaded(
        const std::string& texturePath,
        bool flipVertical = false) {
        return game::runtime::session_texture_cache::ensureTextureLoaded(
            backendTextureByPath_,
            texturePath,
            flipVertical);
    }

    static bool fillTextureViewFromEntry(
        const game::runtime::SharedBackendTextureCacheEntry* texture,
        game::runtime::shared_growl_batches::TextureView& outView) {
        if (!texture || !texture->valid || texture->rgba.empty() ||
            texture->width <= 0 || texture->height <= 0) {
            return false;
        }
        outView.rgba = texture->rgba.data();
        outView.width = texture->width;
        outView.height = texture->height;
        return true;
    }

    bool fillTextureView(const GrowlWaveVFX::Config::DrawPass& pass,
                         const GrowlWaveVFX::Config& config,
                         const game::runtime::shared_growl::TevState& tev,
                         game::runtime::shared_growl_batches::TextureView& outView) {
        if (game::runtime::shared_growl::isLinePass(config, pass) || pass.texturePath.empty()) {
            return fillTextureViewFromEntry(ensureBackendTextureLoaded("", false), outView);
        }

        game::runtime::SharedBackendTextureCacheEntry* rawTexture =
            ensureBackendTextureLoaded(pass.texturePath, false);
        if (!rawTexture || !rawTexture->valid || rawTexture->rgba.empty()) return false;

        const bool quarterPass =
            game::runtime::shared_growl::isQuarterRingPass(config, pass);
        const std::string bakedKey =
            game::runtime::shared_growl::makeBakedTextureKey(pass, quarterPass);
        auto& baked = backendTextureByPath_[bakedKey];
        if (!baked.attemptedLoad) {
            baked.attemptedLoad = true;
            baked.valid = false;
            baked.width = rawTexture->width;
            baked.height = rawTexture->height;
            baked.rgba.clear();
            if (!game::runtime::shared_growl::bakePassTextureRgba(
                    pass,
                    tev,
                    quarterPass,
                    rawTexture->rgba,
                    baked.rgba)) {
                return false;
            }
            baked.valid = true;
        }

        return fillTextureViewFromEntry(&baked, outView);
    }

    OpenGLRenderBackend backend_;
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath_;
    game::runtime::session_texture_cache::TextureCache backendTextureByPath_;
};

GrowlPreviewEffect::GrowlPreviewEffect()
    : manifestPath_(engine::paths::data("config/vfx/moves/growl_draw_passes.json")) {
    config_.spawnForwardOffset = 0.0f;
    config_.spawnHeightOffset = 0.0f;
    config_.drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
}

GrowlPreviewEffect::~GrowlPreviewEffect() = default;

std::string_view GrowlPreviewEffect::name() const {
    return "Growl";
}

void GrowlPreviewEffect::ensureConfigured() {
    effect_.setConfig(config_);
    if (!manifestWriteTime_.has_value()) {
        refreshManifestWriteTime();
    }
}

void GrowlPreviewEffect::onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
    scene.emitter.y = 0.42f;
    scene.target.y = 0.35f;
    ensureConfigured();
}

void GrowlPreviewEffect::emit(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    ensureConfigured();
    const glm::vec3 forward = safeForwardXZ(scene.target - scene.emitter);
    effect_.emitFrom(scene.emitter, forward, nullptr);
}

void GrowlPreviewEffect::replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    accumulator_ = 0.0f;
    elapsedSinceIdle_ = 0.0f;
    emit(scene);
}

void GrowlPreviewEffect::reload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    refreshManifestWriteTime();
    effect_.setConfig(config_);
    accumulator_ = 0.0f;
    elapsedSinceIdle_ = 0.0f;
    emit(scene);
    std::cout << "[VfxPreviewer] Reloaded Growl preview\n";
}

void GrowlPreviewEffect::update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    pollManifestHotReload(scene);

    dt = std::max(0.0f, dt);
    accumulator_ += dt;

    while (accumulator_ >= kFixedDt) {
        effect_.update(kFixedDt);
        accumulator_ -= kFixedDt;

        if (effect_.activeRingCount() == 0u) {
            elapsedSinceIdle_ += kFixedDt;
            if (scene.loopPlayback && elapsedSinceIdle_ >= kLoopCooldownSec) {
                emit(scene);
                elapsedSinceIdle_ = 0.0f;
            }
        } else {
            elapsedSinceIdle_ = 0.0f;
        }
    }
}

void GrowlPreviewEffect::stepFrames(int frames, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    frames = std::max(0, frames);
    for (int i = 0; i < frames; ++i) {
        effect_.update(kFixedDt);
    }
}

void GrowlPreviewEffect::render(const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    if (!renderer_) renderer_ = std::make_unique<SharedRenderer>();
    renderer_->render(effect_, frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

void GrowlPreviewEffect::onResize(int width, int height) {
    if (!renderer_) renderer_ = std::make_unique<SharedRenderer>();
    renderer_->onResize(width, height);
}

std::uint32_t GrowlPreviewEffect::activeCount() const {
    return effect_.activeRingCount();
}

engine::tools::vfx_preview::PreviewCasterAnimationRequest
GrowlPreviewEffect::casterAnimationRequest() const {
    return {
        .kind = "charged",
        .move = "growl",
        .phase = "one_shot",
    };
}

std::vector<std::string> GrowlPreviewEffect::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)scene;
    return {
        "Growl uses the shared/backend batch path and hot reloads its draw-pass manifest."
    };
}

void GrowlPreviewEffect::refreshManifestWriteTime() {
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath_, ec) || ec) {
        manifestWriteTime_.reset();
        return;
    }
    manifestWriteTime_ = std::filesystem::last_write_time(manifestPath_, ec);
    if (ec) manifestWriteTime_.reset();
}

void GrowlPreviewEffect::pollManifestHotReload(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    std::error_code ec;
    if (!manifestWriteTime_.has_value()) {
        refreshManifestWriteTime();
    }
    if (!std::filesystem::exists(manifestPath_, ec) || ec || !manifestWriteTime_.has_value()) return;

    const auto latest = std::filesystem::last_write_time(manifestPath_, ec);
    if (ec || latest == *manifestWriteTime_) return;

    manifestWriteTime_ = latest;
    effect_.setConfig(config_);
    accumulator_ = 0.0f;
    elapsedSinceIdle_ = 0.0f;
    emit(scene);
    std::cout << "[VfxPreviewer] Detected Growl manifest change, hot reloaded preview\n";
}

} // namespace game::preview
