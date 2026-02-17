#include "ModelFastGltfLoaderHelpers.h"
#include "engine/core/Environment.h"

#include <fastgltf/glm_element_traits.hpp>

#include <algorithm>
#include <cctype>

namespace pac::model_fastgltf {

namespace {

std::string toLowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool envTruthy(const char* name) {
    return engine::env::truthyNonZero(name);
}

bool ciContains(const std::string& s, const std::string& needle) {
    return toLowerCopy(s).find(toLowerCopy(needle)) != std::string::npos;
}

int requiredTexCoordForMaterial(const fastgltf::Asset& asset, int materialIndex) {
    if (materialIndex < 0 || materialIndex >= static_cast<int>(asset.materials.size())) return 0;

    const auto& m = asset.materials[static_cast<size_t>(materialIndex)];

    if (m.pbrData.baseColorTexture.has_value()) {
        return static_cast<int>(m.pbrData.baseColorTexture->texCoordIndex);
    }
    if (m.emissiveTexture.has_value()) {
        return static_cast<int>(m.emissiveTexture->texCoordIndex);
    }
    return 0;
}

void readScalarFloat(const fastgltf::Asset& asset,
                     const fastgltf::Accessor& acc,
                     std::vector<float>& out,
                     fastgltf::DefaultBufferDataAdapter& adapter) {
    out.clear();
    out.reserve(acc.count);
    fastgltf::iterateAccessorWithIndex<float>(
        asset,
        acc,
        [&](float v, size_t) { out.push_back(v); },
        adapter);
}

void readVec3AsVec4(const fastgltf::Asset& asset,
                    const fastgltf::Accessor& acc,
                    std::vector<glm::vec4>& out,
                    fastgltf::DefaultBufferDataAdapter& adapter) {
    out.clear();
    out.reserve(acc.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(
        asset,
        acc,
        [&](glm::vec3 v, size_t) { out.emplace_back(v.x, v.y, v.z, 0.0f); },
        adapter);
}

void readVec4(const fastgltf::Asset& asset,
              const fastgltf::Accessor& acc,
              std::vector<glm::vec4>& out,
              fastgltf::DefaultBufferDataAdapter& adapter) {
    out.clear();
    out.reserve(acc.count);
    fastgltf::iterateAccessorWithIndex<glm::vec4>(
        asset,
        acc,
        [&](glm::vec4 v, size_t) { out.push_back(v); },
        adapter);
}

void readMat4(const fastgltf::Asset& asset,
              const fastgltf::Accessor& acc,
              std::vector<glm::mat4>& out,
              fastgltf::DefaultBufferDataAdapter& adapter) {
    out.clear();
    out.reserve(acc.count);
    fastgltf::iterateAccessorWithIndex<glm::mat4>(
        asset,
        acc,
        [&](glm::mat4 m, size_t) { out.push_back(m); },
        adapter);
}

}  // namespace pac::model_fastgltf
