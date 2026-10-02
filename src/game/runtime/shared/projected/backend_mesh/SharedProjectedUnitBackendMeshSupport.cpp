#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include "engine/core/Environment.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace game::runtime::shared_projected_unit_backend_mesh_support {

const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    const game::runtime::render_model::MeshData* mesh,
    const std::vector<int>& submeshNodeFallback,
    std::size_t baseBatchCount, bool preferFullGpuSkinning) {
    std::vector<std::uint8_t> preserveVertexColor;
    if (mesh) {
        for (auto mode : mesh->submeshMaterialModes)
            preserveVertexColor.push_back(mode == runtime::render_model::kNativeLayeredUnlitMaterialMode);
    }
    return engine::models::geometry::ensureFastTexturedMeshTemplateCache(
        mesh, submeshNodeFallback, baseBatchCount, preferFullGpuSkinning, preserveVertexColor);
}

std::size_t selectUniformTriangleIndex(std::size_t sampleIndex,
                                       std::size_t sampleCount,
                                       std::size_t triangleCount) {
    if (triangleCount == 0u || sampleCount == 0u) return 0u;
    if (sampleCount >= triangleCount) return std::min(sampleIndex, triangleCount - 1u);
    const double t = (static_cast<double>(sampleIndex) + 0.5) /
                     static_cast<double>(sampleCount);
    const std::size_t idx =
        static_cast<std::size_t>(t * static_cast<double>(triangleCount));
    return std::min(idx, triangleCount - 1u);
}

bool strictGltfParityEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_GLTF_PARITY_STRICT");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}





std::size_t resolveBatchBaseSubmeshIndex(
    const game::runtime::shared_world_batches::WorldIndexedBatch& batch,
    std::size_t fallback) {
    const auto parseFromKey = [](const std::string& key,
                                 std::size_t fallbackValue) {
        constexpr std::string_view marker = "#submesh_geom:";
        const std::size_t pos = key.find(marker);
        if (pos == std::string::npos) return fallbackValue;

        std::size_t cursor = pos + marker.size();
        std::size_t value = 0u;
        bool sawDigit = false;
        while (cursor < key.size() &&
               std::isdigit(static_cast<unsigned char>(key[cursor]))) {
            sawDigit = true;
            value = value * 10u +
                    static_cast<std::size_t>(key[cursor] - '0');
            ++cursor;
        }
        return sawDigit ? value : fallbackValue;
    };

    std::size_t resolved = parseFromKey(batch.geometryCacheKey, fallback);
    if (resolved != fallback) return resolved;
    if (batch.sharedTemplate) {
        resolved = parseFromKey(batch.sharedTemplate->geometryCacheKey, fallback);
    }
    return resolved;
}

bool backendPrefersFullGpuSkinning(const char* backendId) {
    return backendId && std::string_view(backendId) == "d3d12";
}

shared_projected_unit_backend_mesh_prep::PreparedState& preparedMeshState() {
    static thread_local shared_projected_unit_backend_mesh_prep::PreparedState state;
    return state;
}

std::vector<int>& triNodeIndexByTriangleScratch() {
    static thread_local std::vector<int> scratch;
    return scratch;
}

std::unordered_map<UnitSkinMatrixKey, std::vector<float>, UnitSkinMatrixKeyHash>&
unitSkinMatrices() {
    static thread_local std::unordered_map<UnitSkinMatrixKey,
                                           std::vector<float>,
                                           UnitSkinMatrixKeyHash>
        matrices;
    return matrices;
}

std::unordered_map<UnitSkinMatrixKey, GpuSkinBatchState, UnitSkinMatrixKeyHash>&
gpuSkinBatchStateMap() {
    static thread_local std::unordered_map<UnitSkinMatrixKey,
                                           GpuSkinBatchState,
                                           UnitSkinMatrixKeyHash>
        states;
    return states;
}

std::vector<GpuSkinBatchStateEntry>& gpuSkinBatchStateEntries() {
    static thread_local std::vector<GpuSkinBatchStateEntry> entries;
    return entries;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_support

