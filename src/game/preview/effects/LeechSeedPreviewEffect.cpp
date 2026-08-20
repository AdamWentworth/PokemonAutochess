#include "game/preview/effects/LeechSeedPreviewEffect.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "engine/render/Camera3D.h"
#include "game/PokemonInstance.h"
#include "game/preview/PreviewSceneUtils.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <glm/gtc/type_ptr.hpp>

namespace game::preview {

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;

} // namespace

std::string_view LeechSeedPreviewEffect::name() const {
    return "Leech Seed Projectile";
}

void LeechSeedPreviewEffect::onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
    scene.emitter.y = 0.45f;
    scene.target.y = 0.35f;
}

void LeechSeedPreviewEffect::emit(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    PokemonInstance attacker;
    attacker.name = "PreviewCaster";
    attacker.position = scene.emitter;
    attacker.rotation.y = computeYawDegreesFromForward(scene.target - scene.emitter);
    attacker.visualYOffset = 0.0f;
    attacker.activeAnimIndex = -1;
    attacker.animIdleIndex = -1;

    PokemonInstance victim;
    victim.name = "PreviewTarget";
    victim.position = scene.target;
    victim.visualYOffset = 0.0f;

    effect_.emit(attacker, victim, 0.46f);
}

void LeechSeedPreviewEffect::replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    emit(scene);
}

void LeechSeedPreviewEffect::update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    effect_.update(dt);
}

void LeechSeedPreviewEffect::stepFrames(int frames,
                                        const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    frames = std::max(0, frames);
    for (int i = 0; i < frames; ++i) {
        effect_.update(kFixedDt);
    }
}

void LeechSeedPreviewEffect::render(const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    if (!frame.renderer) {
        effect_.render(frame.camera);
        return;
    }

    ParticleSystem::RenderSnapshot snapshot;
    if (!effect_.getParticles().buildRenderSnapshot(
            snapshot)) {
        return;
    }
    const glm::mat4 viewProjection =
        frame.camera.getProjectionMatrix() *
        frame.camera.getViewMatrix();
    const glm::mat4 inverseViewProjection =
        glm::inverse(viewProjection);
    batches_.clear();
    if (!game::runtime::
            shared_particle_snapshot_billboards::
                appendSnapshotAsBillboards(
                    "leech_seed_preview",
                    snapshot,
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
                    batches_)) {
        return;
    }
    game::runtime::shared_world_batches::
        submitWorldIndexedBatches(
            *frame.renderer,
            batches_,
            glm::value_ptr(viewProjection),
            frame.surfaceWidth,
            frame.surfaceHeight,
            glm::value_ptr(
                frame.camera.getPosition()),
            glm::value_ptr(
                frame.camera.getDirection()),
            glm::value_ptr(
                frame.camera.getTarget()));
}

std::uint32_t LeechSeedPreviewEffect::activeCount() const {
    return static_cast<std::uint32_t>(std::min<std::size_t>(
        effect_.getParticles().particleCount(),
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
}

engine::tools::vfx_preview::PreviewActorSelection
LeechSeedPreviewEffect::previewActors() const {
    return {
        .emitterActorId = "bulbasaur",
        .targetActorId = "charmander",
    };
}

std::vector<std::string> LeechSeedPreviewEffect::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)scene;
    return {
        "Leech Seed preview currently shows the projectile phase only.",
        "The preview still uses fallback node-less placement until a fuller rig-driven adapter is added."
    };
}

} // namespace game::preview
