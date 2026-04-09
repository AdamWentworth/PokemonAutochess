#include "vfx/effects/tackle/TackleSmokeVFX.h"

#include "engine/render/Model.h"
#include "engine/render/Camera3D.h"

#include <algorithm>

namespace {

void scaleTackleSpatialConfig(TackleSmokeVFX::Config& config, float scaleMul) {
    const float scale = std::max(0.0f, scaleMul);
    config.spawnForwardOffset *= scale;
    config.spawnHeightOffset *= scale;
    config.ringForwardOffset *= scale;
    config.ringMinSpeed *= scale;
    config.ringMaxSpeed *= scale;
    config.ringMinSize *= scale;
    config.ringMaxSize *= scale;
    config.ringTrailSpacingMin *= scale;
    config.ringTrailSpacingMax *= scale;
    config.ringTrailLateralJitter *= scale;

    for (auto& pass : config.drawPasses) {
        pass.forwardOffset *= scale;
        pass.heightOffset *= scale;
        pass.authoredBillboardPositionScale *= scale;
        pass.authoredSegmentPositionScale *= scale;
        pass.authoredSegmentTravelMul *= scale;
        if (pass.authoredSegmentMaxVisibleDistance > 0.0f) {
            pass.authoredSegmentMaxVisibleDistance *= scale;
        }

        if (!pass.authoredSegmentsLocal.empty() ||
            pass.renderMode == "streak_quad") {
            pass.scaleMul *= scale;
            pass.authoredSegmentLengthScale *= scale;
        }
    }
}

} // namespace

TackleSmokeVFX::TackleSmokeVFX() {
    effect_.setConfig(makeDefaultConfig());
}

TackleSmokeVFX::~TackleSmokeVFX() = default;

TackleSmokeVFX::Config TackleSmokeVFX::makeDefaultConfig() {
    constexpr float kFsysTotalFrames = 45.500656f;
    constexpr float kFsysTotalSec = kFsysTotalFrames / 30.0f;
    Config config{};
    config.spawnForwardOffset = 0.0f;
    config.spawnHeightOffset = 0.06f;
    config.ringForwardOffset = 0.0f;
    config.ringMinSpeed = 0.01f;
    config.ringMaxSpeed = 0.04f;
    config.ringMinLifeSec = kFsysTotalSec;
    config.ringMaxLifeSec = kFsysTotalSec;
    config.ringMinSize = 0.88f;
    config.ringMaxSize = 1.08f;
    config.ringTrailCount = 0;
    config.ringScaleGrowth = 1.60f;
    config.fadeStart = 0.65f;
    config.vertShaderPath = "assets/shaders/vfx/moves/tackle/tackle_smoke_shared.vert";
    config.fragShaderPath = "assets/shaders/vfx/moves/tackle/tackle_smoke_shared.frag";
    config.tevC0 = glm::vec3(118.0f / 255.0f, 100.0f / 255.0f, 80.0f / 255.0f);
    config.tevC1 = glm::vec3(69.0f / 255.0f, 66.0f / 255.0f, 54.0f / 255.0f);
    config.tevK0 = glm::vec3(1.0f);
    config.tevK1A = 25.0f / 255.0f;
    config.blendMode = 0u;
    config.drawManifestPath = "config/vfx/moves/tackle_draw_passes.json";
    config.drawPasses.clear();
    config.depthTest = true;
    config.depthWrite = false;
    return config;
}

TackleSmokeVFX::Config TackleSmokeVFX::makeGameplayConfig() {
    Config config = makeDefaultConfig();
    constexpr float kGameplaySpatialScale = 0.5f;
    scaleTackleSpatialConfig(config, kGameplaySpatialScale);
    return config;
}

void TackleSmokeVFX::setConfig(const Config& config) {
    effect_.setConfig(config);
}

const TackleSmokeVFX::Config& TackleSmokeVFX::getConfig() const {
    return effect_.getConfig();
}

void TackleSmokeVFX::update(float dt) {
    effect_.update(dt);
}

void TackleSmokeVFX::render(const Camera3D& camera) {
    effect_.render(camera);
}

bool TackleSmokeVFX::buildRenderSnapshot(RenderSnapshot& out) const {
    return effect_.buildRenderSnapshot(out);
}

std::uint32_t TackleSmokeVFX::activeCloudCount() const {
    return effect_.activeRingCount();
}

void TackleSmokeVFX::emitAt(const glm::vec3& worldPos, const glm::vec3& forwardDir) {
    glm::vec3 resolvedForward = forwardDir;
    resolvedForward.y = 0.0f;
    if (glm::dot(resolvedForward, resolvedForward) <= 0.000001f) {
        resolvedForward = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    effect_.emitFrom(worldPos, resolvedForward, nullptr);
}
