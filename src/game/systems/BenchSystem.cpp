// BenchSystem.cpp

#include "BenchSystem.h"
#include <glm/common.hpp>
#include <cmath>

BenchSystem::BenchSystem(float cellSize, int maxSlots)
    : cellSize(cellSize), maxSlots(maxSlots)
{
    // Bench starts just in front of the grid; offset scaled by cellSize for consistency.
    benchStartZ = (8 * cellSize) / 2.0f + cellSize * 0.5f;
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
