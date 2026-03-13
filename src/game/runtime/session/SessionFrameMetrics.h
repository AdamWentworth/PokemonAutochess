#pragma once

#include "engine/core/EngineServices.h"

#include <cstdint>

namespace game::runtime::session_frame_metrics {

struct FrameMetrics {
    std::uint32_t visibleAnimatedUnits = 0u;
    std::uint32_t particleCount = 0u;
    float projectedUnitsMs = 0.0f;
    float projectedPoseEvalMs = 0.0f;
    float projectedModelMs = 0.0f;
    float projectedModelPrepMs = 0.0f;
    float projectedModelGeometryMs = 0.0f;
    float projectedOverlayMs = 0.0f;
    std::uint32_t projectedUnitsProcessed = 0u;
    std::uint32_t projectedModelUnits = 0u;
    std::uint32_t projectedClipSkinnedUnits = 0u;
    float worldComposeMs = 0.0f;
    float worldBackdropMs = 0.0f;
    float worldVfxMs = 0.0f;
    float worldDepthFlushMs = 0.0f;
};

void publish(EngineServices* engineServices, const FrameMetrics& metrics);

} // namespace game::runtime::session_frame_metrics
