// ResourceManager.cpp

#include "ResourceManager.h"
#include "../render/Model.h"
#include <iostream>

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    return instance;
}

std::shared_ptr<Model> ResourceManager::getModel(const std::string& modelPath) {
    // Check if the model is already loaded.
    auto it = loadedModels.find(modelPath);
    if (it != loadedModels.end()) {
        return it->second;
    }

    std::cout << "[ResourceManager] Loading model: " << modelPath << "\n";
    // Load model and store in a shared_ptr.
    auto modelPtr = std::make_shared<Model>(modelPath);
    loadedModels[modelPath] = modelPtr;
    return modelPtr;
}