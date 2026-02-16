#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <fastgltf/tools.hpp>
#include <glm/glm.hpp>

#include "Model.h"

namespace pac::model_fastgltf {

using CPUTexture = Model::CPUTexture;

bool envTruthy(const char* name);
bool ciContains(const std::string& s, const std::string& needle);

int requiredTexCoordForMaterial(const fastgltf::Asset& asset, int materialIndex);

CPUTexture decodeBaseColorTextureFast(const fastgltf::Asset& asset,
                                      const std::filesystem::path& baseDir,
                                      int materialIndex,
                                      bool dbg,
                                      const std::string& modelPath,
                                      int* outTexCoordIndex);

CPUTexture decodeEmissiveTextureFast(const fastgltf::Asset& asset,
                                     const std::filesystem::path& baseDir,
                                     int materialIndex,
                                     bool dbg,
                                     const std::string& modelPath,
                                     int* outTexCoordIndex);

void readScalarFloat(const fastgltf::Asset& asset,
                     const fastgltf::Accessor& acc,
                     std::vector<float>& out,
                     fastgltf::DefaultBufferDataAdapter& adapter);

void readVec3AsVec4(const fastgltf::Asset& asset,
                    const fastgltf::Accessor& acc,
                    std::vector<glm::vec4>& out,
                    fastgltf::DefaultBufferDataAdapter& adapter);

void readVec4(const fastgltf::Asset& asset,
              const fastgltf::Accessor& acc,
              std::vector<glm::vec4>& out,
              fastgltf::DefaultBufferDataAdapter& adapter);

void readMat4(const fastgltf::Asset& asset,
              const fastgltf::Accessor& acc,
              std::vector<glm::mat4>& out,
              fastgltf::DefaultBufferDataAdapter& adapter);

}  // namespace pac::model_fastgltf
