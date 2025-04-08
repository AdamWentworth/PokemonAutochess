// BenchSystem.h

#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "../PokemonInstance.h"

class BenchSystem {
public:
    BenchSystem(float cellSize = 1.2f, int maxSlots = 8);

    bool isInBenchZone(const glm::vec3& pos) const;
    glm::vec3 getSnappedBenchPosition(const glm::vec3& worldPos) const;

    int getMaxSlots() const { return maxSlots; }
    glm::vec3 getSlotPosition(int index) const;

private:
    float cellSize;
    int maxSlots;
    float benchStartZ;
};
