#include "game/preview/PreviewSceneUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/core/Paths.h"
#include "engine/render/Model.h"

namespace game::preview {

namespace {

bool assignLuaFloatOverride(const std::string& text, const char* key, float& outValue) {
    if (!key || !key[0]) return false;
    const std::string needle = std::string(key) + " =";
    const std::size_t pos = text.find(needle);
    if (pos == std::string::npos) return false;
    std::size_t cursor = pos + needle.size();
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    std::size_t end = cursor;
    while (end < text.size()) {
        const char c = text[end];
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+')) {
            break;
        }
        ++end;
    }
    if (end <= cursor) return false;
    try {
        outValue = std::stof(text.substr(cursor, end - cursor));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

PreviewCombatTuning loadPreviewCombatTuningFromConfig() {
    PreviewCombatTuning tuning;
    const std::string path = engine::paths::data("scripts/config/combat_tuning.lua");
    std::ifstream in(path);
    if (!in.is_open()) return tuning;

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    (void)assignLuaFloatOverride(text, "CHARGED_CD_MULT", tuning.chargedCdMult);
    (void)assignLuaFloatOverride(text, "MIN_CHARGED_REQUEST_SEC", tuning.minChargedRequestSec);
    (void)assignLuaFloatOverride(text, "ATTACK_SPEED_SCALE", tuning.attackSpeedScale);
    (void)assignLuaFloatOverride(text, "SPEED_BASELINE", tuning.speedBaseline);
    (void)assignLuaFloatOverride(text, "SPEED_MIN", tuning.speedMin);
    (void)assignLuaFloatOverride(text, "SPEED_MAX", tuning.speedMax);
    return tuning;
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

float resolvePreviewModelScaleCorrection(const Model* model,
                                         const std::string& scaleModeRaw,
                                         const std::string& axisModeRaw) {
    if (!model) return 1.0f;

    const std::string scaleMode = lowerCopy(scaleModeRaw);
    if (scaleMode.empty() || scaleMode == "native" || scaleMode == "raw") {
        const float importerScale = std::max(0.0f, model->getScaleFactor());
        if (importerScale <= 1e-6f) return 1.0f;
        return 1.0f / importerScale;
    }

    if (scaleMode != "normalized") {
        const float importerScale = std::max(0.0f, model->getScaleFactor());
        if (importerScale <= 1e-6f) return 1.0f;
        return 1.0f / importerScale;
    }

    if (!model->hasBounds()) return 1.0f;

    const std::string axisMode = lowerCopy(axisModeRaw);
    if (axisMode.empty() || axisMode == "max") return 1.0f;

    const glm::vec3 ext = model->getBoundsMax() - model->getBoundsMin();
    const float ex = std::max(0.0f, ext.x);
    const float ey = std::max(0.0f, ext.y);
    const float ez = std::max(0.0f, ext.z);
    const float maxExtent = std::max(ex, std::max(ey, ez));
    if (maxExtent <= 1e-6f) return 1.0f;

    float chosenExtent = maxExtent;
    if (axisMode == "x") chosenExtent = ex;
    else if (axisMode == "y") chosenExtent = ey;
    else if (axisMode == "z") chosenExtent = ez;
    else if (axisMode == "median") {
        std::array<float, 3> arr{ex, ey, ez};
        std::sort(arr.begin(), arr.end());
        chosenExtent = arr[1];
    }

    if (chosenExtent <= 1e-6f) return 1.0f;
    return maxExtent / chosenExtent;
}

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

glm::mat4 makePreviewInstanceTransform(const glm::vec3& pos, float yawDeg, float scale) {
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), pos);
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0));
    const glm::mat4 scaling = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    return translation * rotationY * scaling;
}

float previewBoardOriginX(float cellSize) {
    return -((static_cast<float>(kPreviewBoardCols) * cellSize) / 2.0f) +
           cellSize * 0.5f;
}

float previewBoardOriginZ(float cellSize) {
    return -((static_cast<float>(kPreviewBoardRows) * cellSize) / 2.0f) +
           cellSize * 0.5f;
}

glm::ivec2 clampBoardCell(const glm::ivec2& cell) {
    return glm::ivec2(
        std::clamp(cell.x, 0, kPreviewBoardCols - 1),
        std::clamp(cell.y, 0, kPreviewBoardRows - 1));
}

glm::ivec2 previewWorldToBoardCell(const glm::vec3& pos, float cellSize) {
    const int col = static_cast<int>(std::round((pos.x - previewBoardOriginX(cellSize)) / cellSize));
    const int row = static_cast<int>(std::round((pos.z - previewBoardOriginZ(cellSize)) / cellSize));
    return clampBoardCell(glm::ivec2(col, row));
}

glm::vec3 previewBoardCellToWorld(int col, int row, float y, float cellSize) {
    const glm::ivec2 clamped = clampBoardCell(glm::ivec2(col, row));
    return glm::vec3(
        previewBoardOriginX(cellSize) + static_cast<float>(clamped.x) * cellSize,
        y,
        previewBoardOriginZ(cellSize) + static_cast<float>(clamped.y) * cellSize);
}

glm::ivec2 previewAdjacentCell(const glm::ivec2& origin, glm::ivec2 dir) {
    if (dir == glm::ivec2(0)) dir = glm::ivec2(0, 1);
    glm::ivec2 candidate = origin + dir;
    if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
        candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
        return candidate;
    }

    candidate = origin - dir;
    if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
        candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
        return candidate;
    }

    static constexpr glm::ivec2 kFallbackDirs[] = {
        {0, 1}, {1, 0}, {0, -1}, {-1, 0}
    };
    for (const glm::ivec2 fallback : kFallbackDirs) {
        candidate = origin + fallback;
        if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
            candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
            return candidate;
        }
    }

    return origin;
}

} // namespace game::preview
