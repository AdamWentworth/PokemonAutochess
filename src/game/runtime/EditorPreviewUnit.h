#pragma once

#include <array>
#include <string>

namespace game::runtime {

// A deliberately small editor-facing view of a live gameplay unit. The
// runtime remains the authority for model scale, board snapping, terrain
// grounding, and combat state.
struct EditorPreviewUnit {
    int unitId = -1;
    std::string speciesName;
    bool playerSide = true;
    bool benchUnit = false;
    int boardColumn = -1;
    int boardRow = -1;
    int benchSlot = -1;
    std::array<float, 3> position{};
    std::array<float, 3> rotationDegrees{};
    float resolvedRenderScale = 1.0f;
};

} // namespace game::runtime
