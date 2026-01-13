// ResourceManager.cpp

#include "ResourceManager.h"

// Keep your existing include style to match your current include paths:
#include "../render/Model.h"

// NEW: optional fastgltf parsing/logging (does not change loader behavior)
#include "../render/FastGltfValidator.h"

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

    // Optional fastgltf “shadow parse” for compatibility checking.
    // Enabled only if you set: PAC_FASTGLTF_VALIDATE=1
    pac::fastgltf_validator::logSummaryIfEnabled(modelPath);

    auto modelPtr = std::make_shared<Model>(modelPath);
    loadedModels.emplace(modelPath, modelPtr);
    return modelPtr;
}
