#pragma once

#include <string>

#include <glm/glm.hpp>

class Model;

namespace game::preview {

inline constexpr int kPreviewBoardCols = 8;
inline constexpr int kPreviewBoardRows = 8;
inline constexpr int kPreviewBenchSlots = 8;

struct PreviewCombatTuning {
    float chargedCdMult = 2.0f;
    float minChargedRequestSec = 0.85f;
    float attackSpeedScale = 1.0f;
    float speedBaseline = 1.0f;
    float speedMin = 0.35f;
    float speedMax = 3.0f;
};

PreviewCombatTuning loadPreviewCombatTuningFromConfig();
std::string lowerCopy(std::string value);
float resolvePreviewModelScaleCorrection(const Model* model,
                                         const std::string& scaleModeRaw,
                                         const std::string& axisModeRaw);
float computeYawDegreesFromForward(const glm::vec3& forward);
glm::mat4 makePreviewInstanceTransform(const glm::vec3& pos, float yawDeg, float scale);
float previewBoardOriginX(float cellSize);
float previewBoardOriginZ(float cellSize);
glm::ivec2 clampBoardCell(const glm::ivec2& cell);
glm::ivec2 previewWorldToBoardCell(const glm::vec3& pos, float cellSize);
glm::vec3 previewBoardCellToWorld(int col, int row, float y, float cellSize);
glm::ivec2 previewAdjacentCell(const glm::ivec2& origin, glm::ivec2 dir);

} // namespace game::preview
