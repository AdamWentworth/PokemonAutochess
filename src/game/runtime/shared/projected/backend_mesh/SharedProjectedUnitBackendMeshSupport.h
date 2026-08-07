#pragma once

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game::runtime::shared_projected_unit_backend_mesh_support {

using Args = shared_projected_unit_backend_mesh::Args;

std::size_t selectUniformTriangleIndex(std::size_t sampleIndex,
                                       std::size_t sampleCount,
                                       std::size_t triangleCount);
bool strictGltfParityEnabled();
bool tailFireDebugShouldLogAnchor(bool runtimeModeEnabled, int unitId);
bool backendUsesAuthoredTailFireMeshPlayback(const char* backendId);
bool backendUsesGpuClipSkinningForUnit(const char* backendId, std::string_view species);
float textureDetailLodBiasForGraphicsQuality(int graphicsQuality);
void applyGraphicsQualityToBatchTemplate(
    shared_world_batches::WorldIndexedBatch& batch,
    int graphicsQuality);
void applyGraphicsQualityToWorldSceneMaterial(
    IRenderBackend::WorldSceneMaterial& material,
    int graphicsQuality);

struct FastTexturedBatchTemplate {
    std::size_t baseSubmeshIndex = 0u;
    int triNodeIndex = -1;
    bool skinnedBatch = false;
    std::string geometryCacheKey;
    std::vector<std::uint32_t> sourceVertexIndices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> gpuJointPalette;
    std::vector<IRenderBackend::WorldMeshVertex> gpuTemplateVertices;
};

struct FastTexturedMaterialTemplateCache {
    const game::runtime::render_model::MeshData* mesh = nullptr;
    std::string assetCacheIdentitySnapshot;
    std::size_t meshVertexCount = 0u;
    std::size_t meshIndexCount = 0u;
    std::size_t baseBatchCount = 0u;
    bool characterInkingEnabled = false;
    int graphicsQuality = 3;
    std::vector<IRenderBackend::WorldSceneMaterial> materials;
};

struct FastTexturedMeshTemplateCache {
    const game::runtime::render_model::MeshData* mesh = nullptr;
    std::string assetCacheIdentitySnapshot;
    std::size_t meshVertexCount = 0u;
    std::size_t meshIndexCount = 0u;
    std::size_t baseBatchCount = 0u;
    bool preferFullGpuSkinning = false;
    int defaultSkinNodeIndex = -1;
    std::vector<int> submeshNodeFallbackSnapshot;
    std::vector<FastTexturedBatchTemplate> batches;
};

std::vector<int> buildSubmeshNodeFallback(
    const game::runtime::render_model::MeshData& mesh);
std::string makeIndexedGeometryCacheKey(const std::string& keyPrefix,
                                        std::size_t baseSubmeshIndex,
                                        std::size_t batchIndex,
                                        std::size_t baseBatchCount);
std::size_t resolveBatchBaseSubmeshIndex(
    const game::runtime::shared_world_batches::WorldIndexedBatch& batch,
    std::size_t fallback);
bool applyTailFireMeshFlipbookOverride(
    const Args& args,
    const game::runtime::render_model::MeshData& mesh,
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& batches);

inline constexpr std::size_t kMaxGpuSkinMatrices = 128u;

struct UnitSkinMatrixKey {
    int unitId = 0;
    int skinKey = -1;
    std::uint32_t paletteSize = 0u;
    std::array<std::uint16_t, kMaxGpuSkinMatrices> palette{};

    bool operator==(const UnitSkinMatrixKey& other) const {
        if (unitId != other.unitId ||
            skinKey != other.skinKey ||
            paletteSize != other.paletteSize) {
            return false;
        }
        for (std::size_t i = 0; i < paletteSize; ++i) {
            if (palette[i] != other.palette[i]) return false;
        }
        return true;
    }
};

struct UnitSkinMatrixKeyHash {
    std::size_t operator()(const UnitSkinMatrixKey& key) const noexcept {
        std::size_t h = static_cast<std::size_t>(static_cast<std::uint32_t>(key.unitId));
        h ^= (static_cast<std::size_t>(static_cast<std::uint32_t>(key.skinKey + 1)) << 1);
        h ^= (static_cast<std::size_t>(key.paletteSize) << 17);
        for (std::size_t i = 0; i < key.paletteSize; ++i) {
            h ^= static_cast<std::size_t>(key.palette[i]) + 0x9e3779b9u + (h << 6) + (h >> 2);
        }
        return h;
    }
};

struct GpuSkinBatchState {
    bool valid = false;
    std::array<float, 16> modelMatrix{};
    std::uint8_t gpuSkinningMode = 0u;
    std::uint32_t skinMatrixCount = 0u;
    const float* sharedSkinMatrices = nullptr;
};

struct GpuSkinBatchStateEntry {
    UnitSkinMatrixKey key{};
    GpuSkinBatchState state{};
};

int resolveDefaultSkinNodeIndex(const game::runtime::render_model::MeshData* mesh);
bool backendPrefersFullGpuSkinning(const char* backendId);
const FastTexturedMaterialTemplateCache* ensureFastTexturedMaterialTemplateCache(
    const game::runtime::render_model::MeshData* mesh,
    std::size_t baseBatchCount,
    bool characterInkingEnabled,
    int graphicsQuality);
const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    const game::runtime::render_model::MeshData* mesh,
    const std::vector<int>& submeshNodeFallback,
    std::size_t baseBatchCount,
    bool preferFullGpuSkinning);

shared_projected_unit_backend_mesh_prep::PreparedState& preparedMeshState();
std::vector<int>& triNodeIndexByTriangleScratch();
std::unordered_map<UnitSkinMatrixKey, std::vector<float>, UnitSkinMatrixKeyHash>&
unitSkinMatrices();
std::unordered_map<UnitSkinMatrixKey, GpuSkinBatchState, UnitSkinMatrixKeyHash>&
gpuSkinBatchStateMap();
std::vector<GpuSkinBatchStateEntry>& gpuSkinBatchStateEntries();

} // namespace game::runtime::shared_projected_unit_backend_mesh_support

