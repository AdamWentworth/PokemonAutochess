#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/shared/SharedCaptureOverlayVfx.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool approxEq(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_shared_capture_overlay_vfx_contract(std::string& outFail) {
    using namespace game::runtime::shared_capture_overlay;

    Config cfg;
    cfg.worldCellSize = 1.0f;
    cfg.line = 2.0f;
    cfg.uvMin = glm::vec2(0.10f, 0.20f);
    cfg.uvMax = glm::vec2(0.30f, 0.40f);
    cfg.atlasPath = "assets/images/items_atlas.png";

    GameWorld::CaptureAttemptRenderSnapshot throwSnap;
    throwSnap.phase = 0;
    throwSnap.ballPos = glm::vec3(100.0f, 50.0f, 0.0f);
    throwSnap.ballScale = 1.0f;
    throwSnap.ballYawDeg = 15.0f;

    GameWorld::CaptureAttemptRenderSnapshot shakeSnap;
    shakeSnap.phase = 2;
    shakeSnap.phaseTimeSec = 0.20f;
    shakeSnap.ballPos = glm::vec3(120.0f, 60.0f, 0.0f);
    shakeSnap.ballScale = 1.2f;
    shakeSnap.ballYawDeg = 35.0f;

    std::vector<GameWorld::CaptureAttemptRenderSnapshot> snapshots = {throwSnap, shakeSnap};
    std::vector<IRenderBackend::DebugSprite> sprites;
    std::vector<IRenderBackend::DebugLine> lines;

    int ringCalls = 0;
    glm::vec3 lastRingPos{0.0f};
    float lastRingRadius = 0.0f;
    const bool appended = appendOverlay(
        snapshots,
        cfg,
        [](const glm::vec3& world, float& sx, float& sy, float& sz) {
            sx = world.x;
            sy = world.y;
            sz = 0.5f;
            return true;
        },
        [&](const glm::vec3& pos,
            float radius,
            float, float, float, float,
            float,
            int) {
            ++ringCalls;
            lastRingPos = pos;
            lastRingRadius = radius;
        },
        sprites,
        lines);

    if (!expect(appended, "appendOverlay should append visible capture overlay sprites/lines.", outFail)) {
        return false;
    }
    if (!expect(sprites.size() == 2u, "appendOverlay should append one sprite per visible snapshot.", outFail)) {
        return false;
    }
    if (!expect(lines.size() == 2u, "appendOverlay should append one seam line per visible snapshot.", outFail)) {
        return false;
    }
    if (!expect(ringCalls == 1, "appendOverlay should emit a projected ring for shake phase snapshots.", outFail)) {
        return false;
    }
    if (!expect(sprites[0].texturePath == cfg.atlasPath,
                "appendOverlay should preserve the caller-provided atlas path on capture sprites.",
                outFail)) {
        return false;
    }
    if (!expect(approxEq(sprites[0].u0, cfg.uvMin.x) && approxEq(sprites[0].v0, cfg.uvMin.y) &&
                    approxEq(sprites[0].u1, cfg.uvMax.x) && approxEq(sprites[0].v1, cfg.uvMax.y),
                "appendOverlay should preserve caller-provided atlas UVs on capture sprites.",
                outFail)) {
        return false;
    }
    if (!expect(lastRingPos.y > shakeSnap.ballPos.y,
                "appendOverlay should offset shake rings slightly above the capture ball position.",
                outFail)) {
        return false;
    }
    if (!expect(lastRingRadius > 0.03f,
                "appendOverlay should generate a non-trivial ring radius for shake phases.",
                outFail)) {
        return false;
    }

    sprites.clear();
    lines.clear();
    ringCalls = 0;
    GameWorld::CaptureAttemptRenderSnapshot offscreenSnap;
    offscreenSnap.phase = 3;
    offscreenSnap.success = true;
    offscreenSnap.ballPos = glm::vec3(0.0f, 0.0f, 0.0f);
    std::vector<GameWorld::CaptureAttemptRenderSnapshot> offscreenOnly = {offscreenSnap};
    const bool appendedOffscreen = appendOverlay(
        offscreenOnly,
        cfg,
        [](const glm::vec3&, float& sx, float& sy, float& sz) {
            sx = 0.0f;
            sy = 0.0f;
            sz = 1.2f;
            return true;
        },
        [&](const glm::vec3&,
            float,
            float, float, float, float,
            float,
            int) {
            ++ringCalls;
        },
        sprites,
        lines);

    if (!expect(!appendedOffscreen,
                "appendOverlay should no-op when all capture snapshots project outside the clip depth range.",
                outFail)) {
        return false;
    }
    if (!expect(sprites.empty() && lines.empty() && ringCalls == 0,
                "appendOverlay should not append sprites/lines/rings for off-screen capture snapshots.",
                outFail)) {
        return false;
    }

    return true;
}
