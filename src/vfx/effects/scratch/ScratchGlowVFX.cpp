#include "vfx/effects/scratch/ScratchGlowVFX.h"

#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"

#include <algorithm>

ScratchGlowVFX::ScratchGlowVFX() {
    effect_.setConfig(makeDefaultConfig());
}

ScratchGlowVFX::~ScratchGlowVFX() = default;

ScratchGlowVFX::Config ScratchGlowVFX::makeDefaultConfig() {
    constexpr float kGlowLifeSec = 13.0f / 30.0f;
    constexpr float kGlowSize = 0.38f;

    Config config{};
    config.spawnForwardOffset = 0.0f;
    config.spawnHeightOffset = 0.0f;
    config.ringForwardOffset = 0.0f;
    config.ringMinSpeed = 0.0f;
    config.ringMaxSpeed = 0.0f;
    config.ringMinLifeSec = kGlowLifeSec;
    config.ringMaxLifeSec = kGlowLifeSec;
    config.ringMinSize = kGlowSize;
    config.ringMaxSize = kGlowSize;
    config.ringTrailCount = 0;
    config.ringScaleGrowth = 1.0f;
    config.fadeStart = 0.60f;
    config.ringLeadSizeMul = 1.0f;
    config.impactGroupCount = 5;
    config.impactGroupStepSec = 2.0f / 30.0f;
    config.impactGroupMode = "random_local_jitter";
    config.impactGroupJitterRange = glm::vec2(0.10f, 0.08f);

    config.vertShaderPath = "assets/shaders/vfx/moves/scratch/scratch_glow_shared.vert";
    config.fragShaderPath = "assets/shaders/vfx/moves/scratch/scratch_glow_shared.frag";
    config.tevC0 = glm::vec3(158.0f / 255.0f, 46.0f / 255.0f, 18.0f / 255.0f);
    config.tevC1 = glm::vec3(62.0f / 255.0f, 32.0f / 255.0f, 37.0f / 255.0f);
    config.tevK0 = glm::vec3(1.0f);
    config.tevC0A = 1.0f;
    config.tevC1A = 0.0f;
    config.tevK1A = 1.0f;
    config.blendMode = 0u;
    config.drawManifestPath = "config/vfx/moves/scratch_draw_passes.json";
    config.drawPasses.clear();
    config.depthTest = true;
    config.depthWrite = false;
    return config;
}

ScratchGlowVFX::Config ScratchGlowVFX::makeGameplayConfig() {
    Config config = makeDefaultConfig();
    // Gameplay should keep the impact readable even when the target surface or board
    // plane clips through the effect footprint.
    config.depthTest = false;
    return config;
}

void ScratchGlowVFX::setConfig(const Config& config) {
    effect_.setConfig(config);
}

const ScratchGlowVFX::Config& ScratchGlowVFX::getConfig() const {
    return effect_.getConfig();
}

void ScratchGlowVFX::update(float dt) {
    effect_.update(dt);
}

void ScratchGlowVFX::render(const Camera3D& camera) {
    effect_.render(camera);
}

bool ScratchGlowVFX::buildRenderSnapshot(RenderSnapshot& out) const {
    return effect_.buildRenderSnapshot(out);
}

std::uint32_t ScratchGlowVFX::activeGlowCount() const {
    return effect_.activeRingCount();
}

void ScratchGlowVFX::emitAt(const glm::vec3& worldPos,
                            const glm::vec3& forwardDir,
                            const glm::mat4* viewMatrix) {
    glm::vec3 resolvedForward = forwardDir;
    resolvedForward.y = 0.0f;
    if (glm::dot(resolvedForward, resolvedForward) <= 0.000001f) {
        resolvedForward = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    effect_.emitFrom(worldPos, resolvedForward, viewMatrix);
}
