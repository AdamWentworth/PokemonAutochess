#include "game/runtime/render_prep/MeshNormals.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

bool approx(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_render_prep_mesh_normals_contract(std::string& outFail) {
    using game::runtime::render_model::MeshVertex;
    using game::runtime::render_prep_mesh::computeVertexNormals;

    {
        std::vector<MeshVertex> vertices(4);
        vertices[0].position = glm::vec3(-1.0f, -1.0f, 0.0f);
        vertices[1].position = glm::vec3( 1.0f, -1.0f, 0.0f);
        vertices[2].position = glm::vec3( 1.0f,  1.0f, 0.0f);
        vertices[3].position = glm::vec3(-1.0f,  1.0f, 0.0f);
        const std::vector<std::uint32_t> indices = {
            0u, 1u, 2u,
            0u, 2u, 3u
        };
        computeVertexNormals(vertices, indices);
        for (const auto& v : vertices) {
            if (!approx(v.normal.x, 0.0f) ||
                !approx(v.normal.y, 0.0f) ||
                !approx(v.normal.z, 1.0f)) {
                outFail = "computeVertexNormals should generate +Z normals for a CCW XY quad";
                return false;
            }
        }
    }

    {
        std::vector<MeshVertex> vertices(3);
        vertices[0].position = glm::vec3(0.0f, 0.0f, 0.0f);
        vertices[1].position = glm::vec3(1.0f, 0.0f, 0.0f);
        vertices[2].position = glm::vec3(0.0f, 1.0f, 0.0f);
        const std::vector<std::uint32_t> badIndices = {0u, 1u, 9u};
        computeVertexNormals(vertices, badIndices);
        for (const auto& v : vertices) {
            if (!approx(v.normal.x, 0.0f) ||
                !approx(v.normal.y, 1.0f) ||
                !approx(v.normal.z, 0.0f)) {
                outFail = "invalid triangles should leave fallback up-normal on untouched vertices";
                return false;
            }
        }
    }

    {
        std::vector<MeshVertex> empty;
        const std::vector<std::uint32_t> indices = {0u, 1u, 2u};
        computeVertexNormals(empty, indices);
        if (!empty.empty()) {
            outFail = "computeVertexNormals should preserve empty vertex arrays";
            return false;
        }
    }

    return true;
}





