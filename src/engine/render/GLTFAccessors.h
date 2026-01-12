// GLTFAccessors.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>

// IMPORTANT:
// This header intentionally does NOT include tiny_gltf.h.
// Model.cpp owns the one-and-only TINYGLTF_IMPLEMENTATION include site.
// GLTFAccessors.cpp includes tiny_gltf.h normally (no implementation macros).

namespace tinygltf {
    class Model;
    struct Accessor;
}

namespace pac::gltfaccessors {

const unsigned char* accessorDataPtr(const tinygltf::Model& m,
                                     const tinygltf::Accessor& a);

size_t accessorStride(const tinygltf::Model& m,
                      const tinygltf::Accessor& a);

void readAccessorScalarFloat(const tinygltf::Model& m,
                             const tinygltf::Accessor& a,
                             std::vector<float>& out);

void readAccessorVec4FloatLike(const tinygltf::Model& m,
                               const tinygltf::Accessor& a,
                               std::vector<glm::vec4>& out);

// Convenience typed readers (mirror the prior lambdas in Model.cpp)
void readVec2Float(const tinygltf::Model& m,
                   int accessorIndex,
                   std::vector<glm::vec2>& out);

void readVec3Float(const tinygltf::Model& m,
                   int accessorIndex,
                   std::vector<glm::vec3>& out);

void readJoints4U16(const tinygltf::Model& m,
                    int accessorIndex,
                    std::vector<glm::u16vec4>& out);

void readWeights4Float(const tinygltf::Model& m,
                       int accessorIndex,
                       std::vector<glm::vec4>& out);

void readIndicesU32(const tinygltf::Model& m,
                    int accessorIndex,
                    std::vector<uint32_t>& out);

} // namespace pac::gltfaccessors
