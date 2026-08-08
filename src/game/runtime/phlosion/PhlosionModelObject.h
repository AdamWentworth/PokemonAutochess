#pragma once

#include "game/runtime/render_model_cache/RenderModelCache.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace game::runtime::phlosion {

inline constexpr char kCookedRoot[] = "content/phlosion";

struct ModelCookStats {
    std::uint64_t sourceBytes = 0u;
    std::uint64_t cookedBytes = 0u;
    std::uint32_t textureCount = 0u;
};

struct ModelTextureDependency {
    std::string assetId;
    std::string physicalPath;
    std::uint64_t expectedContentHash = 0u;
    std::uint64_t byteCount = 0u;
};

std::string objectPathForModel(
    const std::string& sourceModelPath,
    const std::string& cookedRoot = kCookedRoot);

bool cookModelObject(
    const std::string& sourceModelPath,
    const render_model::MeshData& source,
    const std::string& cookedRoot,
    std::string_view prefabKind,
    ModelCookStats& outStats,
    std::string* outError = nullptr);

bool loadModelObject(
    const std::string& phloPath,
    render_model::MeshData& out,
    std::string* outError = nullptr);

bool listModelObjectTextureDependencies(
    const std::string& phloPath,
    std::vector<ModelTextureDependency>& out,
    std::string* outError = nullptr);

} // namespace game::runtime::phlosion
