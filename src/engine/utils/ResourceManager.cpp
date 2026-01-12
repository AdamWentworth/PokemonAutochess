// ResourceManager.cpp

#include "ResourceManager.h"
#include "../render/Model.h"
#include <iostream>

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    return instance;
}

std::shared_ptr<Model> ResourceManager::getModel(const std::string& modelPath) {
    auto it = loadedModels.find(modelPath);
    if (it != loadedModels.end()) {
        return it->second;
    }

#if defined(PAC_VERBOSE_STARTUP) && PAC_VERBOSE_STARTUP
    std::cout << "[ResourceManager] Loading model: " << modelPath << "\n";
#endif

    auto modelPtr = std::make_shared<Model>(modelPath);
    loadedModels.emplace(modelPath, modelPtr);
    return modelPtr;
}