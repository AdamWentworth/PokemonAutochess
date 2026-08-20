#include "game/preview/effects/GameplayParticlePreviewEffect.h"

#include "engine/render/Camera3D.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/vfx/AquaSwooshVFX.h"
#include "game/vfx/ClawSwipeVFX.h"
#include "game/vfx/GrassImpactVFX.h"
#include "game/vfx/HealPlusVFX.h"
#include "game/vfx/LeechSeedDrainVFX.h"
#include "game/vfx/TackleImpactVFX.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace game::preview {
namespace {

constexpr float kFixedStepSeconds = 1.0f / 60.0f;

glm::vec3 safeForward(
    const engine::tools::vfx_preview::
        PreviewSceneState& scene) {
    const glm::vec3 delta =
        scene.target - scene.emitter;
    const float lengthSquared =
        glm::dot(delta, delta);
    return lengthSquared > 0.000001f
               ? delta / std::sqrt(lengthSquared)
               : glm::vec3(0.0f, 0.0f, 1.0f);
}

} // namespace

GameplayParticlePreviewEffect::
    GameplayParticlePreviewEffect(Kind kind)
    : kind_(kind) {
    switch (kind_) {
    case Kind::AquaSwoosh:
        name_ = "Aqua Swoosh";
        break;
    case Kind::ClawSwipe:
        name_ = "Claw Swipe";
        break;
    case Kind::GrassImpact:
        name_ = "Grass Impact";
        break;
    case Kind::HealPlus:
        name_ = "Heal Plus";
        break;
    case Kind::LeechSeedDrain:
        name_ = "Leech Seed Drain";
        break;
    case Kind::TackleImpact:
        name_ = "Tackle Impact";
        break;
    }
}

GameplayParticlePreviewEffect::
    ~GameplayParticlePreviewEffect() = default;

std::string_view
GameplayParticlePreviewEffect::name() const {
    return name_;
}

void GameplayParticlePreviewEffect::onActivated(
    engine::tools::vfx_preview::
        PreviewSceneState& scene) {
    scene.emitter.y = 0.45f;
    scene.target.y = 0.35f;
}

void GameplayParticlePreviewEffect::replay(
    const engine::tools::vfx_preview::
        PreviewSceneState& scene) {
    aqua_.reset();
    claw_.reset();
    grass_.reset();
    heal_.reset();
    drain_.reset();
    tackleImpact_.reset();
    switch (kind_) {
    case Kind::AquaSwoosh:
        aqua_ = std::make_unique<AquaSwooshVFX>();
        aqua_->emitAt(
            scene.target,
            safeForward(scene),
            AquaSwooshVFX::Style::WaterGun);
        break;
    case Kind::ClawSwipe:
        claw_ = std::make_unique<ClawSwipeVFX>();
        claw_->emitAt(
            scene.target,
            safeForward(scene),
            false);
        break;
    case Kind::GrassImpact:
        grass_ =
            std::make_unique<GrassImpactVFX>();
        grass_->emitAt(scene.target);
        break;
    case Kind::HealPlus:
        heal_ = std::make_unique<HealPlusVFX>();
        heal_->emitAt(scene.emitter);
        break;
    case Kind::LeechSeedDrain:
        drain_ =
            std::make_unique<LeechSeedDrainVFX>();
        drain_->emitBetween(
            scene.target,
            scene.emitter,
            0.55f);
        break;
    case Kind::TackleImpact:
        tackleImpact_ =
            std::make_unique<TackleImpactVFX>();
        tackleImpact_->emitAt(scene.target);
        break;
    }
}

void GameplayParticlePreviewEffect::update(
    float dt,
    const engine::tools::vfx_preview::
        PreviewSceneState& scene) {
    (void)scene;
    dt = std::max(0.0f, dt);
    switch (kind_) {
    case Kind::AquaSwoosh:
        if (aqua_) aqua_->update(dt);
        break;
    case Kind::ClawSwipe:
        if (claw_) claw_->update(dt);
        break;
    case Kind::GrassImpact:
        if (grass_) grass_->update(dt);
        break;
    case Kind::HealPlus:
        if (heal_) heal_->update(dt);
        break;
    case Kind::LeechSeedDrain:
        if (drain_) drain_->update(dt);
        break;
    case Kind::TackleImpact:
        if (tackleImpact_) {
            tackleImpact_->update(dt);
        }
        break;
    }
}

void GameplayParticlePreviewEffect::stepFrames(
    int frames,
    const engine::tools::vfx_preview::
        PreviewSceneState& scene) {
    for (int frame = 0;
         frame < std::max(0, frames);
         ++frame) {
        update(kFixedStepSeconds, scene);
    }
}

void GameplayParticlePreviewEffect::appendSnapshots(
    std::vector<ParticleSystem::RenderSnapshot>&
        snapshots) const {
    const auto append =
        [&](const ParticleSystem& particles) {
            ParticleSystem::RenderSnapshot snapshot;
            if (particles.buildRenderSnapshot(
                    snapshot)) {
                snapshots.push_back(
                    std::move(snapshot));
            }
        };
    switch (kind_) {
    case Kind::AquaSwoosh:
        if (aqua_) append(aqua_->getParticles());
        break;
    case Kind::ClawSwipe:
        if (claw_) append(claw_->getParticles());
        break;
    case Kind::GrassImpact:
        if (grass_) append(grass_->getParticles());
        break;
    case Kind::HealPlus:
        if (heal_) append(heal_->getParticles());
        break;
    case Kind::LeechSeedDrain:
        if (drain_) append(drain_->getParticles());
        break;
    case Kind::TackleImpact:
        if (tackleImpact_) {
            append(
                tackleImpact_->
                    getBurstParticles());
            append(
                tackleImpact_->
                    getSparkParticles());
        }
        break;
    }
}

void GameplayParticlePreviewEffect::renderSnapshots(
    const std::vector<
        ParticleSystem::RenderSnapshot>& snapshots,
    const engine::tools::vfx_preview::
        PreviewFrameContext& frame) {
    if (!frame.renderer || snapshots.empty()) {
        return;
    }
    const glm::mat4 viewProjection =
        frame.camera.getProjectionMatrix() *
        frame.camera.getViewMatrix();
    const glm::mat4 inverseViewProjection =
        glm::inverse(viewProjection);
    batches_.clear();
    for (std::size_t index = 0u;
         index < snapshots.size();
         ++index) {
        const std::string label =
            "gameplay_particle_preview_" +
            std::to_string(index);
        game::runtime::
            shared_particle_snapshot_billboards::
                appendSnapshotAsBillboards(
                    label.c_str(),
                    snapshots[index],
                    viewProjection,
                    inverseViewProjection,
                    frame.camera.getPosition(),
                    frame.surfaceWidth,
                    frame.surfaceHeight,
                    [&](const std::string& texturePath,
                        bool flipVertical) {
                        return game::runtime::
                            session_texture_cache::
                                ensureTextureLoaded(
                                    textureCache_,
                                    texturePath,
                                    flipVertical);
                    },
                    batches_);
    }
    if (batches_.empty()) {
        return;
    }
    const glm::vec3 cameraPosition =
        frame.camera.getPosition();
    const glm::vec3 cameraDirection =
        frame.camera.getDirection();
    const glm::vec3 cameraTarget =
        frame.camera.getTarget();
    game::runtime::shared_world_batches::
        submitWorldIndexedBatches(
            *frame.renderer,
            batches_,
            glm::value_ptr(viewProjection),
            frame.surfaceWidth,
            frame.surfaceHeight,
            glm::value_ptr(cameraPosition),
            glm::value_ptr(cameraDirection),
            glm::value_ptr(cameraTarget));
}

void GameplayParticlePreviewEffect::render(
    const engine::tools::vfx_preview::
        PreviewFrameContext& frame) {
    if (!frame.renderer) {
        switch (kind_) {
        case Kind::AquaSwoosh:
            if (aqua_) aqua_->render(frame.camera);
            return;
        case Kind::ClawSwipe:
            if (claw_) claw_->render(frame.camera);
            return;
        case Kind::GrassImpact:
            if (grass_) grass_->render(frame.camera);
            return;
        case Kind::HealPlus:
            if (heal_) heal_->render(frame.camera);
            return;
        case Kind::LeechSeedDrain:
            if (drain_) drain_->render(frame.camera);
            return;
        case Kind::TackleImpact:
            if (tackleImpact_) {
                tackleImpact_->render(frame.camera);
            }
            return;
        }
    }
    std::vector<ParticleSystem::RenderSnapshot>
        snapshots;
    snapshots.reserve(2u);
    appendSnapshots(snapshots);
    renderSnapshots(snapshots, frame);
}

std::uint32_t
GameplayParticlePreviewEffect::activeCount() const {
    std::size_t count = 0u;
    switch (kind_) {
    case Kind::AquaSwoosh:
        count = aqua_
                    ? aqua_->getParticles()
                          .particleCount()
                    : 0u;
        break;
    case Kind::ClawSwipe:
        count = claw_
                    ? claw_->getParticles()
                          .particleCount()
                    : 0u;
        break;
    case Kind::GrassImpact:
        count = grass_
                    ? grass_->getParticles()
                          .particleCount()
                    : 0u;
        break;
    case Kind::HealPlus:
        count = heal_
                    ? heal_->getParticles()
                          .particleCount()
                    : 0u;
        break;
    case Kind::LeechSeedDrain:
        count = drain_
                    ? drain_->getParticles()
                          .particleCount()
                    : 0u;
        break;
    case Kind::TackleImpact:
        count =
            tackleImpact_
                ? tackleImpact_->
                          getBurstParticles()
                          .particleCount() +
                      tackleImpact_->
                          getSparkParticles()
                          .particleCount()
                : 0u;
        break;
    }
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(
            count,
            std::numeric_limits<
                std::uint32_t>::max()));
}

engine::tools::vfx_preview::
    PreviewEffectFocusFrame
GameplayParticlePreviewEffect::previewFocusFrame(
    const engine::tools::vfx_preview::
        PreviewSceneState& scene) const {
    engine::tools::vfx_preview::
        PreviewEffectFocusFrame focus;
    focus.enabled = true;
    focus.center =
        kind_ == Kind::HealPlus
            ? scene.emitter
            : scene.target;
    focus.radius = 0.8f;
    focus.yawDeg = -12.0f;
    focus.pitchDeg = 18.0f;
    focus.distanceMul = 1.0f;
    return focus;
}

} // namespace game::preview
