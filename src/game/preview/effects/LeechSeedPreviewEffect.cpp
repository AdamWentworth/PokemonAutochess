#include "game/preview/effects/LeechSeedPreviewEffect.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "engine/render/Camera3D.h"
#include "game/PokemonInstance.h"

namespace game::preview {

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;
float computeYawDegreesFromForward(const glm::vec3& forward) {
    glm::vec3 safe(forward.x, 0.0f, forward.z);
    const float lenSq = glm::dot(safe, safe);
    if (lenSq <= 0.000001f) {
        safe = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        safe /= std::sqrt(lenSq);
    }
    return glm::degrees(std::atan2(safe.x, safe.z));
}

} // namespace

std::string_view LeechSeedPreviewEffect::name() const {
    return "Leech Seed";
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
    effect_.render(frame.camera);
}

std::uint32_t LeechSeedPreviewEffect::activeCount() const {
    return static_cast<std::uint32_t>(std::min<std::size_t>(
        effect_.getParticles().particleCount(),
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
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
