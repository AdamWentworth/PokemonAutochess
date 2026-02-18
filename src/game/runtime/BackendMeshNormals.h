#pragma once

#include "game/runtime/BackendModelCache.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::backend_model {

inline void computeVertexNormals(std::vector<MeshVertex>& vertices,
                                 const std::vector<std::uint32_t>& indices) {
    if (vertices.empty()) return;

    std::vector<glm::vec3> accum(vertices.size(), glm::vec3(0.0f));
    const std::size_t triangleCount = indices.size() / 3u;
    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        const std::size_t i = triIdx * 3u;
        const std::uint32_t i0 = indices[i + 0u];
        const std::uint32_t i1 = indices[i + 1u];
        const std::uint32_t i2 = indices[i + 2u];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        const glm::vec3& p0 = vertices[i0].position;
        const glm::vec3& p1 = vertices[i1].position;
        const glm::vec3& p2 = vertices[i2].position;
        const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        const float lenSq = glm::dot(n, n);
        if (lenSq <= 1e-12f) continue;

        accum[i0] += n;
        accum[i1] += n;
        accum[i2] += n;
    }

    for (std::size_t vi = 0; vi < vertices.size(); ++vi) {
        const glm::vec3 n = accum[vi];
        const float lenSq = glm::dot(n, n);
        if (lenSq > 1e-12f) {
            vertices[vi].normal = glm::normalize(n);
        } else {
            vertices[vi].normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

} // namespace game::runtime::backend_model

