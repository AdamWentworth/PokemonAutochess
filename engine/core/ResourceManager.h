// ResourceManager.h

#pragma once
#include <string>
#include <unordered_map>
#include <memory>

class Model;

// This class centralizes loading & caching of models (GLTF, etc.)
class ResourceManager {
public:
    // Singleton pattern (or you can simply make normal static methods if you prefer)
    static ResourceManager& getInstance();

    // Loads (if not already loaded) and returns a pointer to the Model
    Model* getModel(const std::string& modelPath);

private:
    ResourceManager() = default; // private constructor for Singleton

    // key: filepath, value: unique_ptr to the loaded Model
    std::unordered_map<std::string, std::unique_ptr<Model>> loadedModels;
};
