// BenchSystem.h

#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "game/PokemonInstance.h"

class BenchSystem {
public:
    BenchSystem(float cellSize = 1.2f, int maxSlots = 8);

    bool isInBenchZone(const glm::vec3& pos) const;
    glm::vec3 getSnappedBenchPosition(const glm::vec3& worldPos) const;
    void setCellSize(float newCellSize);

    int getMaxSlots() const { return maxSlots; }
    glm::vec3 getSlotPosition(int index) const;

private:
    float cellSize;
    int maxSlots;
    float benchStartZ;
};
