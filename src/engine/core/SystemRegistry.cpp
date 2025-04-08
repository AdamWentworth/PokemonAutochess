// SystemRegistry.cpp

#include "SystemRegistry.h"

SystemRegistry& SystemRegistry::getInstance() {
    static SystemRegistry instance;
    return instance;
}

void SystemRegistry::registerSystem(std::shared_ptr<IUpdatable> system) {
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
