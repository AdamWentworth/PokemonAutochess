// SystemRegistry.h

#pragma once

#include "Updatable.h"
#include <vector>
#include <memory>

class SystemRegistry {
public:
    SystemRegistry() = default;

    static SystemRegistry& getInstance();

    void registerSystem(std::shared_ptr<Updatable> system);
    void updateAll(float deltaTime);
    void clear(); // Optional, for cleanup

private:
    std::vector<std::shared_ptr<Updatable>> systems;
};


