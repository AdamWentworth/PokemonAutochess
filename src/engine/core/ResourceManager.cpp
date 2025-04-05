// ResourceManager.cpp

#include "ResourceManager.h"
#include "../render/Model.h"
#include <iostream>

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    return instance;
}

Model* ResourceManager::getModel(const std::string& modelPath) {
    // Check if it's already in the cache
    auto it = loadedModels.find(modelPath);
    if (it != loadedModels.end()) {
        return it->second.get();
    }

    // Otherwise, load it
    std::cout << "[ResourceManager] Loading model: " << modelPath << "\n";
    loadedModels[modelPath] = std::make_unique<Model>(modelPath);
    return loadedModels[modelPath].get();
}
