// BenchSystem.cpp

#include "BenchSystem.h"
#include <glm/common.hpp>
#include <algorithm>
#include <cmath>

BenchSystem::BenchSystem(float cellSize,
                         int maxSlots,
                         int boardRows,
                         int benchGapCells)
    : cellSize(std::max(0.05f, cellSize)),
      maxSlots(std::max(1, maxSlots)),
      boardRows(std::max(1, boardRows)),
      benchGapCells(std::max(0, benchGapCells))
{
    refreshBenchStart();
}

void BenchSystem::refreshBenchStart() {
    benchStartZ =
        (static_cast<float>(boardRows) * cellSize) * 0.5f +
        static_cast<float>(benchGapCells) * cellSize;
}

void BenchSystem::setCellSize(float newCellSize) {
    cellSize = std::max(0.05f, newCellSize);
    refreshBenchStart();
}

bool BenchSystem::isInBenchZone(const glm::vec3& pos) const {
    float zEnd = benchStartZ + cellSize;
    return pos.z >= benchStartZ && pos.z <= zEnd;
}

glm::vec3 BenchSystem::getSnappedBenchPosition(const glm::vec3& worldPos) const {
    float totalBenchWidth = maxSlots * cellSize;
    float startX = -totalBenchWidth / 2.0f;

    int slot = static_cast<int>(std::round((worldPos.x - startX) / cellSize));
    slot = glm::clamp(slot, 0, maxSlots - 1);
    return getSlotPosition(slot);
}

glm::vec3 BenchSystem::getSlotPosition(int index) const {
    float totalBenchWidth = maxSlots * cellSize;
    float startX = -totalBenchWidth / 2.0f;
    float x = startX + index * cellSize + cellSize * 0.5f;
    float z = benchStartZ + cellSize * 0.5f;
    return glm::vec3(x, 0.0f, z);
}
