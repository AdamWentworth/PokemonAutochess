// GLTFAccessors.cpp

#include "GLTFAccessors.h"

#include <algorithm>
#include <cstring>

// Keep tinygltf setup consistent with Model.cpp, but WITHOUT implementation macros.
#include "../../../third_party/nlohmann/json.hpp"
#define TINYGLTF_NO_INCLUDE_JSON
#include "../../../third_party/tinygltf/tiny_gltf.h"

namespace pac::gltfaccessors {

const unsigned char* accessorDataPtr(const tinygltf::Model& m,
                                     const tinygltf::Accessor& a)
{
    if (a.bufferView < 0 || a.bufferView >= (int)m.bufferViews.size()) return nullptr;
    const auto& bv = m.bufferViews[a.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)m.buffers.size()) return nullptr;
    const auto& buf = m.buffers[bv.buffer];

    size_t offset = bv.byteOffset + a.byteOffset;
    if (offset >= buf.data.size()) return nullptr;
    return buf.data.data() + offset;
}

size_t accessorStride(const tinygltf::Model& m,
                      const tinygltf::Accessor& a)
{
    if (a.bufferView < 0 || a.bufferView >= (int)m.bufferViews.size()) return 0;
    const auto& bv = m.bufferViews[a.bufferView];

    size_t stride = a.ByteStride(bv);
    if (stride == 0) {
        stride = tinygltf::GetComponentSizeInBytes(a.componentType) *
                 tinygltf::GetNumComponentsInType(a.type);
    }
    return stride;
}

void readAccessorScalarFloat(const tinygltf::Model& m,
                             const tinygltf::Accessor& a,
                             std::vector<float>& out)
{
    out.clear();
    if (a.type != TINYGLTF_TYPE_SCALAR) return;
    if (a.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return;

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;

    size_t stride = accessorStride(m, a);
    out.resize(a.count);

    for (size_t i = 0; i < a.count; ++i) {
        const float* p = reinterpret_cast<const float*>(base + i * stride);
        out[i] = p[0];
    }
}

void readAccessorVec4FloatLike(const tinygltf::Model& m,
                               const tinygltf::Accessor& a,
                               std::vector<glm::vec4>& out)
{
    out.clear();

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;

    size_t stride = accessorStride(m, a);
    int comps = tinygltf::GetNumComponentsInType(a.type);
    if (comps <= 0) return;

    out.resize(a.count);

    if (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
        for (size_t i = 0; i < a.count; ++i) {
            const float* p = reinterpret_cast<const float*>(base + i * stride);
            glm::vec4 v(0.0f);
            v.x = (comps > 0) ? p[0] : 0.0f;
            v.y = (comps > 1) ? p[1] : 0.0f;
            v.z = (comps > 2) ? p[2] : 0.0f;
            v.w = (comps > 3) ? p[3] : 0.0f;
            out[i] = v;
        }
        return;
    }

    auto readNorm = [&](size_t idx, int c)->float{
        const unsigned char* ptr = base + idx * stride;
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(ptr);
            return (c < comps) ? (float(p[c]) / 255.0f) : 0.0f;
        }
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* p = reinterpret_cast<const uint16_t*>(ptr);
            return (c < comps) ? (float(p[c]) / 65535.0f) : 0.0f;
        }
        return 0.0f;
    };

    for (size_t i = 0; i < a.count; ++i) {
        glm::vec4 v(0.0f);
        v.x = readNorm(i,0);
        v.y = readNorm(i,1);
        v.z = readNorm(i,2);
        v.w = readNorm(i,3);
        out[i] = v;
    }
}

void readVec2Float(const tinygltf::Model& m,
                   int accessorIndex,
                   std::vector<glm::vec2>& out)
{
    out.clear();
    if (accessorIndex < 0 || accessorIndex >= (int)m.accessors.size()) return;
    const auto& a = m.accessors[accessorIndex];
    if (a.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return;
    if (a.type != TINYGLTF_TYPE_VEC2) return;

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;
    size_t stride = accessorStride(m, a);

    out.resize(a.count);
    for (size_t i = 0; i < a.count; ++i) {
        const float* p = reinterpret_cast<const float*>(base + i * stride);
        out[i] = glm::vec2(p[0], p[1]);
    }
}

void readVec3Float(const tinygltf::Model& m,
                   int accessorIndex,
                   std::vector<glm::vec3>& out)
{
    out.clear();
    if (accessorIndex < 0 || accessorIndex >= (int)m.accessors.size()) return;
    const auto& a = m.accessors[accessorIndex];
    if (a.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return;
    if (a.type != TINYGLTF_TYPE_VEC3) return;

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;
    size_t stride = accessorStride(m, a);

    out.resize(a.count);
    for (size_t i = 0; i < a.count; ++i) {
        const float* p = reinterpret_cast<const float*>(base + i * stride);
        out[i] = glm::vec3(p[0], p[1], p[2]);
    }
}

void readJoints4U16(const tinygltf::Model& m,
                    int accessorIndex,
                    std::vector<glm::u16vec4>& out)
{
    out.clear();
    if (accessorIndex < 0 || accessorIndex >= (int)m.accessors.size()) return;
    const auto& a = m.accessors[accessorIndex];
    if (a.type != TINYGLTF_TYPE_VEC4) return;

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;
    size_t stride = accessorStride(m, a);

    out.resize(a.count, glm::u16vec4(0));

    if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        for (size_t i = 0; i < a.count; ++i) {
            const uint16_t* p = reinterpret_cast<const uint16_t*>(base + i * stride);
            out[i] = glm::u16vec4(p[0], p[1], p[2], p[3]);
        }
        return;
    }

    if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        for (size_t i = 0; i < a.count; ++i) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(base + i * stride);
            out[i] = glm::u16vec4(p[0], p[1], p[2], p[3]);
        }
        return;
    }
}

void readWeights4Float(const tinygltf::Model& m,
                       int accessorIndex,
                       std::vector<glm::vec4>& out)
{
    out.clear();
    if (accessorIndex < 0 || accessorIndex >= (int)m.accessors.size()) return;
    const auto& a = m.accessors[accessorIndex];
    if (a.type != TINYGLTF_TYPE_VEC4) return;

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;
    size_t stride = accessorStride(m, a);

    out.resize(a.count, glm::vec4(1,0,0,0));

    if (a.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
        for (size_t i = 0; i < a.count; ++i) {
            const float* p = reinterpret_cast<const float*>(base + i * stride);
            out[i] = glm::vec4(p[0], p[1], p[2], p[3]);
        }
        return;
    }

    if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        for (size_t i = 0; i < a.count; ++i) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(base + i * stride);
            out[i] = glm::vec4(p[0]/255.0f, p[1]/255.0f, p[2]/255.0f, p[3]/255.0f);
        }
        return;
    }
    if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        for (size_t i = 0; i < a.count; ++i) {
            const uint16_t* p = reinterpret_cast<const uint16_t*>(base + i * stride);
            out[i] = glm::vec4(p[0]/65535.0f, p[1]/65535.0f, p[2]/65535.0f, p[3]/65535.0f);
        }
        return;
    }
}

void readIndicesU32(const tinygltf::Model& m,
                    int accessorIndex,
                    std::vector<uint32_t>& out)
{
    out.clear();
    if (accessorIndex < 0 || accessorIndex >= (int)m.accessors.size()) return;
    const auto& a = m.accessors[accessorIndex];
    if (a.type != TINYGLTF_TYPE_SCALAR) return;

    const unsigned char* base = accessorDataPtr(m, a);
    if (!base) return;
    size_t stride = accessorStride(m, a);

    out.resize(a.count);

    if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        for (size_t i = 0; i < a.count; ++i) {
            const uint16_t* p = reinterpret_cast<const uint16_t*>(base + i * stride);
            out[i] = (uint32_t)p[0];
        }
        return;
    }
    if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        for (size_t i = 0; i < a.count; ++i) {
            const uint32_t* p = reinterpret_cast<const uint32_t*>(base + i * stride);
            out[i] = p[0];
        }
        return;
    }
    if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        for (size_t i = 0; i < a.count; ++i) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(base + i * stride);
            out[i] = (uint32_t)p[0];
        }
        return;
    }
}

} // namespace pac::gltfaccessors
