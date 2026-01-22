// SystemRegistry.cpp

#include "SystemRegistry.h"

void SystemRegistry::registerSystem(std::shared_ptr<Updatable> system) {
    systems.push_back(system);
}

void SystemRegistry::updateAll(float deltaTime) {
    for (auto& system : systems) {
        system->update(deltaTime);
    }
}

void SystemRegistry::clear() {
    systems.clear();
}
