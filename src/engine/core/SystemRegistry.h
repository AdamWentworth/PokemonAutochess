// SystemRegistry.h

#pragma once

#include "IUpdatable.h"
#include <vector>
#include <memory>

class SystemRegistry {
public:
    SystemRegistry() = default;

    static SystemRegistry& getInstance();

    void registerSystem(std::shared_ptr<IUpdatable> system);
    void updateAll(float deltaTime);
    void clear(); // Optional, for cleanup

private:
    std::vector<std::shared_ptr<IUpdatable>> systems;
};


