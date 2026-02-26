#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/IRenderBackend.h"
#include "game/world/GameWorld.h"

namespace game::runtime::shared_capture_overlay {

struct Config {
    float worldCellSize = 1.0f;
    float line = 1.0f;
    glm::vec2 uvMin{0.0f, 0.0f};
    glm::vec2 uvMax{1.0f, 1.0f};
    std::string atlasPath;
};

using ProjectWorldFn = std::function<bool(const glm::vec3&, float&, float&, float&)>;
using AppendProjectedRingFn =
    std::function<void(const glm::vec3&,
                       float,
                       float,
                       float,
                       float,
                       float,
                       float,
                       int)>;

bool appendOverlay(const std::vector<GameWorld::CaptureAttemptRenderSnapshot>& snapshots,
                   const Config& config,
                   const ProjectWorldFn& projectWorld,
                   const AppendProjectedRingFn& appendProjectedRing,
                   std::vector<IRenderBackend::DebugSprite>& outSprites,
                   std::vector<IRenderBackend::DebugLine>& outLines);

} // namespace game::runtime::shared_capture_overlay
