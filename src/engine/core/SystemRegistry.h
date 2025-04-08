// SystemRegistry.h

#pragma once

#include "IUpdatable.h"
#include <vector>
#include <memory>

class SystemRegistry {
public:
    static SystemRegistry& getInstance();

    void registerSystem(std::shared_ptr<IUpdatable> system);
    void updateAll(float deltaTime);
    void clear(); // Optional, for cleanup

private:
    std::vector<std::shared_ptr<IUpdatable>> systems;
};
