// GLTFAccessors.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>

#include <nlohmann/json.hpp>

// IMPORTANT:
// - Do NOT define TINYGLTF_IMPLEMENTATION in any header.
// - Model.cpp remains the one-and-only implementation site.
#ifndef TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_INCLUDE_JSON
#endif

// FIX: rely on CMake's third_party include dir, not a fragile relative path
#include <tinygltf/tiny_gltf.h>

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

// Convenience typed readers
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
