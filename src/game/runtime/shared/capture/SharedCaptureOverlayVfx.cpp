#include "game/runtime/shared/capture/SharedCaptureOverlayVfx.h"

#include <algorithm>
#include <cmath>

namespace game::runtime::shared_capture_overlay {

bool appendOverlay(const std::vector<GameWorld::CaptureAttemptRenderSnapshot>& snapshots,
                   const Config& config,
                   const ProjectWorldFn& projectWorld,
                   const AppendProjectedRingFn& appendProjectedRing,
                   std::vector<IRenderBackend::DebugSprite>& outSprites,
                   std::vector<IRenderBackend::DebugLine>& outLines) {
    if (!projectWorld) return false;
    bool appendedAny = false;

    for (const auto& snap : snapshots) {
        float sx = 0.0f;
        float sy = 0.0f;
        float sz = 0.0f;
        if (!projectWorld(snap.ballPos, sx, sy, sz)) continue;
        if (sz < 0.0f || sz > 1.0f) continue;

        const float worldRadius = std::max(
            0.03f,
            config.worldCellSize * 0.17f * std::max(0.05f, snap.ballScale));
        float sx2 = 0.0f;
        float sy2 = 0.0f;
        float sz2 = 0.0f;
        float ballPx = std::max(12.0f, config.line * 8.0f);
        if (projectWorld(snap.ballPos + glm::vec3(worldRadius, 0.0f, 0.0f), sx2, sy2, sz2)) {
            ballPx = std::max(ballPx, std::abs(sx2 - sx) * 2.0f);
        }
        ballPx = std::clamp(ballPx, 12.0f, 96.0f);

        IRenderBackend::DebugSprite ball;
        ball.x = sx - ballPx * 0.5f;
        ball.y = sy - ballPx * 0.5f;
        ball.w = ballPx;
        ball.h = ballPx;
        ball.u0 = config.uvMin.x;
        ball.v0 = config.uvMin.y;
        ball.u1 = config.uvMax.x;
        ball.v1 = config.uvMax.y;
        ball.r = 1.0f;
        ball.g = 1.0f;
        ball.b = 1.0f;
        ball.a = std::clamp(0.82f + (snap.phase == 3 && snap.success ? 0.12f : 0.0f), 0.0f, 1.0f);
        ball.texturePath = config.atlasPath;
        outSprites.push_back(std::move(ball));

        const float seamAng = glm::radians(
            snap.ballYawDeg + (snap.phase == 2 ? std::sin(snap.phaseTimeSec * 18.0f) * 8.0f : 0.0f));
        IRenderBackend::DebugLine seam;
        seam.x1 = sx - std::cos(seamAng) * (ballPx * 0.28f);
        seam.y1 = sy - std::sin(seamAng) * (ballPx * 0.18f);
        seam.x2 = sx + std::cos(seamAng) * (ballPx * 0.28f);
        seam.y2 = sy + std::sin(seamAng) * (ballPx * 0.18f);
        seam.thickness = std::max(1.0f, ballPx * 0.06f);
        seam.r = 0.10f;
        seam.g = 0.10f;
        seam.b = 0.10f;
        seam.a = 0.72f;
        outLines.push_back(seam);
        appendedAny = true;

        if (!appendProjectedRing) continue;
        if (snap.phase == 2) {
            const float pulse = 0.5f + 0.5f * std::sin(snap.phaseTimeSec * 16.0f);
            appendProjectedRing(
                snap.ballPos + glm::vec3(0.0f, 0.03f, 0.0f),
                std::max(0.03f, worldRadius * (0.9f + pulse * 0.35f)),
                0.96f, 0.94f, 0.86f, 0.48f + pulse * 0.18f,
                std::max(1.0f, config.line * 0.9f),
                10);
        } else if (snap.phase == 3) {
            const float t = std::clamp(snap.phaseTimeSec / 0.35f, 0.0f, 1.0f);
            if (snap.success) {
                appendProjectedRing(
                    snap.ballPos + glm::vec3(0.0f, 0.04f, 0.0f),
                    std::max(0.03f, worldRadius * (1.0f + t * 1.2f)),
                    1.00f, 0.86f, 0.28f, (1.0f - t) * 0.65f,
                    std::max(1.0f, config.line * 1.0f),
                    12);
            } else {
                appendProjectedRing(
                    snap.ballPos + glm::vec3(0.0f, 0.04f, 0.0f),
                    std::max(0.03f, worldRadius * (0.9f + t * 0.9f)),
                    1.00f, 0.42f, 0.30f, (1.0f - t) * 0.58f,
                    std::max(1.0f, config.line * 0.95f),
                    12);
            }
        }
    }

    return appendedAny;
}

} // namespace game::runtime::shared_capture_overlay
