#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::backend_model {

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec4 color{1.0f};
};

struct MeshData {
    float modelScaleFactor = 1.0f;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    bool hasVertexColor = false;
};

std::string cachePathForModel(const std::string& modelPath);
bool loadMeshFromCache(const std::string& modelPath, MeshData& out, std::string* outError = nullptr);

} // namespace game::runtime::backend_model
