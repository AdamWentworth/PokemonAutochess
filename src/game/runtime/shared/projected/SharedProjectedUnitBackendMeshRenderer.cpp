#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotAtlasCache.h"

#include "engine/core/Environment.h"
#include "engine/render/Model.h"
#include "game/runtime/BackendUnitVisuals.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
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

bool tailFireDebugEnabled() {
    static const bool enabled = engine::env::flagEnabled("PAC_TAIL_FIRE_DEBUG");
    return enabled;
}

bool tailFireDebugShouldLogAnchor(int unitId) {
    if (!tailFireDebugEnabled()) return false;
    static std::unordered_map<int, std::chrono::steady_clock::time_point> sLastLogByUnit;
    const auto now = std::chrono::steady_clock::now();
    auto it = sLastLogByUnit.find(unitId);
    if (it != sLastLogByUnit.end() &&
        (now - it->second) < std::chrono::milliseconds(750)) {
        return false;
    }
    sLastLogByUnit[unitId] = now;
    return true;
}

struct FastTexturedBatchTemplate {
    std::size_t baseSubmeshIndex = 0u;
    int triNodeIndex = -1;
    std::string geometryCacheKey;
    std::vector<std::uint32_t> sourceVertexIndices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> gpuJointPalette;
    std::vector<IRenderBackend::WorldMeshVertex> gpuTemplateVertices;
};

struct FastTexturedMeshTemplateCache {
    const game::runtime::backend_model::MeshData* mesh = nullptr;
    std::size_t meshVertexCount = 0u;
    std::size_t meshIndexCount = 0u;
    std::size_t baseBatchCount = 0u;
    int defaultSkinNodeIndex = -1;
    std::vector<int> submeshNodeFallbackSnapshot;
    std::vector<FastTexturedBatchTemplate> batches;
};

thread_local std::unordered_map<
    const game::runtime::backend_model::MeshData*,
    FastTexturedMeshTemplateCache> g_fastTexturedMeshTemplateCaches;
thread_local std::vector<int> g_triNodeIndexByTriangleScratch;
thread_local game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState
    g_preparedMeshState;

constexpr std::size_t kMaxGpuSkinMatrices = 64u;

std::vector<int> buildSubmeshNodeFallback(
    const game::runtime::backend_model::MeshData& mesh) {
    std::vector<int> submeshNodeFallback;
    if (mesh.submeshMeshIndex.empty()) return submeshNodeFallback;
    submeshNodeFallback.assign(mesh.submeshMeshIndex.size(), -1);
    for (std::size_t si = 0; si < mesh.submeshMeshIndex.size(); ++si) {
        const int meshIndex = mesh.submeshMeshIndex[si];
        if (meshIndex >= 0 &&
            static_cast<std::size_t>(meshIndex) < mesh.meshIndexToNode.size()) {
            submeshNodeFallback[si] =
                mesh.meshIndexToNode[static_cast<std::size_t>(meshIndex)];
        }
    }
    return submeshNodeFallback;
}

std::string makeIndexedGeometryCacheKey(const std::string& keyPrefix,
                                        std::size_t baseSubmeshIndex,
                                        std::size_t batchIndex,
                                        std::size_t baseBatchCount) {
    std::string key = keyPrefix + "#submesh_geom:" + std::to_string(baseSubmeshIndex);
    if (batchIndex >= baseBatchCount) {
        key += "#split:" + std::to_string(batchIndex);
    }
    return key;
}

std::string makeIndexedBatchKeyPrefix(
    const game::runtime::backend_model::MeshData& mesh) {
    return "__runtime_mesh__:" +
           std::to_string(static_cast<unsigned long long>(
               reinterpret_cast<std::uintptr_t>(&mesh)));
}

bool containsInsensitive(const std::string& haystack, const char* needle) {
    if (!needle || !*needle) return true;
    return toLowerCopy(haystack).find(toLowerCopy(std::string(needle))) != std::string::npos;
}

bool nodeNameLooksLikeFireMesh(const std::string& nodeName) {
    return containsInsensitive(nodeName, "fire_mesh");
}

constexpr const char* kCharmanderFireUvFlipbookPath =
    "assets/textures/charmander_fire_uv_flipbook.png";
constexpr float kCharmanderFireUvFlipbookCols = 8.0f;
constexpr float kCharmanderFireUvFlipbookRows = 8.0f;
constexpr float kCharmanderFireUvFlipbookFrames = 62.0f;
constexpr float kCharmanderFireUvFlipbookFps = 24.0f;
constexpr float kCharmanderFireUvFlipbookAtlasWidth = 4096.0f;
constexpr float kCharmanderFireUvFlipbookAtlasHeight = 4096.0f;
constexpr int kClampToEdge = 33071;

std::size_t resolveBatchBaseSubmeshIndex(
    const game::runtime::shared_world_batches::WorldIndexedBatch& batch,
    std::size_t fallback) {
    const auto parseFromKey = [](const std::string& key, std::size_t fallbackValue) {
        const std::string marker = "#submesh_geom:";
        const std::size_t pos = key.find(marker);
        if (pos == std::string::npos) return fallbackValue;
        std::size_t cur = pos + marker.size();
        std::size_t value = 0u;
        bool sawDigit = false;
        while (cur < key.size() && std::isdigit(static_cast<unsigned char>(key[cur]))) {
            sawDigit = true;
            value = value * 10u + static_cast<std::size_t>(key[cur] - '0');
            ++cur;
        }
        return sawDigit ? value : fallbackValue;
    };

    std::size_t out = parseFromKey(batch.geometryCacheKey, fallback);
    if (out != fallback) return out;
    if (batch.sharedTemplate) {
        out = parseFromKey(batch.sharedTemplate->geometryCacheKey, fallback);
    }
    return out;
}

bool applyCharmanderFireMeshFlipbookOverride(
    const game::runtime::shared_projected_unit_backend_mesh::Args& args,
    const game::runtime::backend_model::MeshData& mesh,
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& batches) {
    if (!args.unit || !args.sharedTailFireAnchors) {
        return false;
    }
    if (toLowerCopy(args.unit->name) != "charmander") return false;

    std::vector<std::uint8_t> fireSubmeshMask(
        std::max<std::size_t>(mesh.submeshMeshIndex.size(), batches.size()), 0u);
    std::vector<std::uint8_t> fireMeshIndexMask(mesh.meshIndexToNode.size(), 0u);
    for (std::size_t ni = 0; ni < mesh.nodeNames.size(); ++ni) {
        if (!nodeNameLooksLikeFireMesh(mesh.nodeNames[ni])) continue;
        if (ni >= mesh.nodeMesh.size()) continue;
        const int meshIndex = mesh.nodeMesh[ni];
        if (meshIndex < 0 ||
            static_cast<std::size_t>(meshIndex) >= fireMeshIndexMask.size()) {
            continue;
        }
        fireMeshIndexMask[static_cast<std::size_t>(meshIndex)] = 1u;
    }
    for (std::size_t si = 0; si < mesh.submeshMeshIndex.size(); ++si) {
        const int meshIndex = mesh.submeshMeshIndex[si];
        if (meshIndex < 0 ||
            static_cast<std::size_t>(meshIndex) >= fireMeshIndexMask.size()) {
            continue;
        }
        if (fireMeshIndexMask[static_cast<std::size_t>(meshIndex)] != 0u) {
            fireSubmeshMask[si] = 1u;
        }
    }

    if (!args.ensureBackendTextureLoaded) {
        return false;
    }
    game::runtime::SharedBackendTextureCacheEntry* atlas =
        args.ensureBackendTextureLoaded(kCharmanderFireUvFlipbookPath, false);
    if (!atlas || !atlas->valid || atlas->width <= 0 || atlas->height <= 0 ||
        atlas->rgba.empty()) {
        return false;
    }

    bool applied = false;
    for (std::size_t bi = 0; bi < batches.size(); ++bi) {
        auto& batch = batches[bi];
        const std::size_t baseSubmeshIndex = resolveBatchBaseSubmeshIndex(batch, bi);
        if (baseSubmeshIndex >= fireSubmeshMask.size() ||
            fireSubmeshMask[baseSubmeshIndex] == 0u) {
            continue;
        }

        batch.sharedTemplate = nullptr;
        batch.textureKey = kCharmanderFireUvFlipbookPath;
        batch.textureCacheKey = kCharmanderFireUvFlipbookPath;
        batch.textureRgba = atlas->rgba.data();
        batch.textureWidth = atlas->width;
        batch.textureHeight = atlas->height;
        batch.textureWrapS = kClampToEdge;
        batch.textureWrapT = kClampToEdge;

        batch.normalTextureKey.clear();
        batch.normalTextureCacheKey.clear();
        batch.normalTextureRgba = nullptr;
        batch.normalTextureWidth = 0;
        batch.normalTextureHeight = 0;
        batch.normalTextureWrapS = 10497;
        batch.normalTextureWrapT = 10497;
        batch.metallicRoughnessTextureKey.clear();
        batch.metallicRoughnessTextureCacheKey.clear();
        batch.metallicRoughnessTextureRgba = nullptr;
        batch.metallicRoughnessTextureWidth = 0;
        batch.metallicRoughnessTextureHeight = 0;
        batch.occlusionTextureKey.clear();
        batch.occlusionTextureCacheKey.clear();
        batch.occlusionTextureRgba = nullptr;
        batch.occlusionTextureWidth = 0;
        batch.occlusionTextureHeight = 0;
        batch.emissiveTextureKey.clear();
        batch.emissiveTextureCacheKey.clear();
        batch.emissiveTextureRgba = nullptr;
        batch.emissiveTextureWidth = 0;
        batch.emissiveTextureHeight = 0;

        batch.materialAlphaOverride = true;
        batch.alphaMode = 1u;
        batch.blendMode = 0u;
        batch.alphaCutoff = 0.08f;
        batch.materialMode = 1u;
        batch.characterInkingEnabled = 0u;
        batch.materialTimeSec = args.materialTimeSec;
        batch.materialFlags = 8.0f; // authoredFireMesh
        batch.materialAtlasWidth = kCharmanderFireUvFlipbookAtlasWidth;
        batch.materialAtlasHeight = kCharmanderFireUvFlipbookAtlasHeight;
        batch.materialRect0U = 0.0f;
        batch.materialRect0V = 0.0f;
        batch.materialRect0W = 1.0f;
        batch.materialRect0H = 1.0f;
        batch.materialRect1U = 0.0f;
        batch.materialRect1V = 0.0f;
        batch.materialRect1W = 1.0f;
        batch.materialRect1H = 1.0f;
        batch.materialFlipbook0Cols = kCharmanderFireUvFlipbookCols;
        batch.materialFlipbook0Rows = kCharmanderFireUvFlipbookRows;
        batch.materialFlipbook0Frames = kCharmanderFireUvFlipbookFrames;
        batch.materialFlipbook0Fps = kCharmanderFireUvFlipbookFps;
        batch.materialFlipbook1Cols = 1.0f;
        batch.materialFlipbook1Rows = 1.0f;
        batch.materialFlipbook1Frames = 1.0f;
        batch.materialFlipbook1Fps = 0.0f;
        applied = true;
    }

    if (applied) {
        auto it = args.sharedTailFireAnchors->find(args.unit->id);
        if (it != args.sharedTailFireAnchors->end()) {
            it->second.meshCarrierActive = true;
        }
    }
    return applied;
}

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

thread_local std::unordered_map<UnitSkinMatrixKey, std::vector<float>, UnitSkinMatrixKeyHash>
    g_unitSkinMatrices;

struct GpuSkinBatchState {
    bool valid = false;
    std::array<float, 16> modelMatrix{};
    std::uint32_t skinMatrixCount = 0u;
    const float* sharedSkinMatrices = nullptr;
};

struct GpuSkinBatchStateEntry {
    UnitSkinMatrixKey key{};
    GpuSkinBatchState state{};
};

thread_local std::vector<GpuSkinBatchStateEntry> g_gpuSkinBatchStateEntries;

int resolveDefaultSkinNodeIndex(const game::runtime::backend_model::MeshData* mesh) {
    if (!mesh) return -1;
    int selectedSkin = -1;
    int selectedNode = -1;
    for (std::size_t ni = 0; ni < mesh->nodeSkin.size(); ++ni) {
        const int skinIndex = mesh->nodeSkin[ni];
        if (skinIndex < 0) continue;
        if (selectedSkin < 0) {
            selectedSkin = skinIndex;
            selectedNode = static_cast<int>(ni);
            continue;
        }
        if (selectedSkin != skinIndex) {
            return -1;
        }
    }
    return selectedNode;
}

const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    const game::runtime::backend_model::MeshData* mesh,
    const std::vector<int>& submeshNodeFallback,
    std::size_t baseBatchCount) {
    if (!mesh || baseBatchCount == 0u) return nullptr;

    auto& cache = g_fastTexturedMeshTemplateCaches[mesh];
    const bool cacheValid =
        cache.mesh == mesh &&
        cache.meshVertexCount == mesh->vertices.size() &&
        cache.meshIndexCount == mesh->indices.size() &&
        cache.baseBatchCount == baseBatchCount &&
        cache.submeshNodeFallbackSnapshot == submeshNodeFallback &&
        !cache.batches.empty();
    if (cacheValid) return &cache;

    cache = {};
    cache.mesh = mesh;
    cache.meshVertexCount = mesh->vertices.size();
    cache.meshIndexCount = mesh->indices.size();
    cache.baseBatchCount = baseBatchCount;
    cache.defaultSkinNodeIndex = resolveDefaultSkinNodeIndex(mesh);
    cache.submeshNodeFallbackSnapshot = submeshNodeFallback;
    const std::string keyPrefix = makeIndexedBatchKeyPrefix(*mesh);

    const std::size_t triangleCount = mesh->indices.size() / 3u;
    if (triangleCount == 0u || mesh->vertices.empty()) return nullptr;

    cache.batches.assign(baseBatchCount, FastTexturedBatchTemplate{});
    for (std::size_t si = 0; si < baseBatchCount; ++si) {
        cache.batches[si].baseSubmeshIndex = si;
        cache.batches[si].geometryCacheKey =
            makeIndexedGeometryCacheKey(keyPrefix, si, si, baseBatchCount);
    }

    std::vector<std::unordered_map<int, std::size_t>> nodeToBatch(baseBatchCount);
    std::vector<std::size_t> batchIndexByTriangle(triangleCount, 0u);
    std::vector<std::size_t> triangleCountByBatch(baseBatchCount, 0u);

    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        std::size_t submeshIndex = 0u;
        if (triIdx < mesh->triangleSubmesh.size()) {
            submeshIndex = static_cast<std::size_t>(mesh->triangleSubmesh[triIdx]);
            if (submeshIndex >= baseBatchCount) submeshIndex = 0u;
        }

        int triNodeIndex =
            (triIdx < mesh->triangleNodeIndex.size()) ? mesh->triangleNodeIndex[triIdx] : -1;
        if (triNodeIndex < 0 &&
            triIdx < mesh->triangleSubmesh.size() &&
            !submeshNodeFallback.empty()) {
            const std::uint16_t fallbackSubmeshIndex = mesh->triangleSubmesh[triIdx];
            if (fallbackSubmeshIndex < submeshNodeFallback.size()) {
                triNodeIndex = submeshNodeFallback[fallbackSubmeshIndex];
            }
        }

        std::size_t batchIndex = submeshIndex;
        if (triNodeIndex >= 0) {
            auto& mapForSubmesh = nodeToBatch[submeshIndex];
            const auto found = mapForSubmesh.find(triNodeIndex);
            if (found != mapForSubmesh.end()) {
                batchIndex = found->second;
            } else if (mapForSubmesh.empty()) {
                mapForSubmesh.emplace(triNodeIndex, submeshIndex);
                cache.batches[submeshIndex].triNodeIndex = triNodeIndex;
                batchIndex = submeshIndex;
            } else {
                batchIndex = cache.batches.size();
                mapForSubmesh.emplace(triNodeIndex, batchIndex);
                FastTexturedBatchTemplate newBatch{};
                newBatch.baseSubmeshIndex = submeshIndex;
                newBatch.triNodeIndex = triNodeIndex;
                newBatch.geometryCacheKey = makeIndexedGeometryCacheKey(
                    keyPrefix, submeshIndex, batchIndex, baseBatchCount);
                cache.batches.push_back(std::move(newBatch));
                triangleCountByBatch.push_back(0u);
            }
        }

        if (batchIndex >= triangleCountByBatch.size()) {
            triangleCountByBatch.resize(batchIndex + 1u, 0u);
        }
        ++triangleCountByBatch[batchIndex];
        batchIndexByTriangle[triIdx] = batchIndex;
    }

    // Split node/submesh batches further when the triangle joint union exceeds
    // GPU palette limits, so skinned textured batches can remain on GPU.
    std::vector<std::vector<std::size_t>> splitBatchCandidatesBySource(cache.batches.size());
    std::vector<std::vector<std::uint16_t>> splitJointPaletteByBatch(cache.batches.size());
    std::vector<std::size_t> splitBatchIndexByTriangle(triangleCount, 0u);
    std::vector<std::size_t> splitTriangleCountByBatch(cache.batches.size(), 0u);
    for (std::size_t bi = 0; bi < cache.batches.size(); ++bi) {
        splitBatchCandidatesBySource[bi].push_back(bi);
    }

    constexpr float kJointWeightEpsilon = 0.00001f;
    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        std::size_t sourceBatchIndex = batchIndexByTriangle[triIdx];
        if (sourceBatchIndex >= splitBatchCandidatesBySource.size()) sourceBatchIndex = 0u;

        std::array<std::uint16_t, 12> triJoints{};
        std::size_t triJointCount = 0u;
        const std::size_t i = triIdx * 3u;
        if (i + 2u < mesh->indices.size()) {
            const std::uint32_t i0 = mesh->indices[i + 0u];
            const std::uint32_t i1 = mesh->indices[i + 1u];
            const std::uint32_t i2 = mesh->indices[i + 2u];
            const auto appendWeightedJoints = [&](const game::runtime::backend_model::MeshVertex& v) {
                const std::uint16_t joints[4] = {v.j0, v.j1, v.j2, v.j3};
                const float weights[4] = {v.w0, v.w1, v.w2, v.w3};
                for (int ji = 0; ji < 4; ++ji) {
                    if (weights[ji] <= kJointWeightEpsilon) continue;
                    bool seen = false;
                    for (std::size_t existing = 0; existing < triJointCount; ++existing) {
                        if (triJoints[existing] == joints[ji]) {
                            seen = true;
                            break;
                        }
                    }
                    if (seen || triJointCount >= triJoints.size()) continue;
                    triJoints[triJointCount++] = joints[ji];
                }
            };
            if (i0 < mesh->vertices.size()) appendWeightedJoints(mesh->vertices[i0]);
            if (i1 < mesh->vertices.size()) appendWeightedJoints(mesh->vertices[i1]);
            if (i2 < mesh->vertices.size()) appendWeightedJoints(mesh->vertices[i2]);
        }

        std::size_t chosenBatchIndex = splitBatchCandidatesBySource[sourceBatchIndex][0];
        bool foundCandidate = false;
        for (const std::size_t candidateBatchIndex : splitBatchCandidatesBySource[sourceBatchIndex]) {
            if (candidateBatchIndex >= splitJointPaletteByBatch.size()) continue;
            const auto& candidatePalette = splitJointPaletteByBatch[candidateBatchIndex];
            std::size_t addedJointCount = 0u;
            for (std::size_t ji = 0; ji < triJointCount; ++ji) {
                const bool exists = std::find(
                    candidatePalette.begin(),
                    candidatePalette.end(),
                    triJoints[ji]) != candidatePalette.end();
                if (!exists) ++addedJointCount;
            }
            if (candidatePalette.size() + addedJointCount <= kMaxGpuSkinMatrices) {
                chosenBatchIndex = candidateBatchIndex;
                foundCandidate = true;
                break;
            }
        }

        if (!foundCandidate) {
            const auto& sourceBatch = cache.batches[sourceBatchIndex];
            FastTexturedBatchTemplate splitBatch{};
            splitBatch.baseSubmeshIndex = sourceBatch.baseSubmeshIndex;
            splitBatch.triNodeIndex = sourceBatch.triNodeIndex;
            splitBatch.geometryCacheKey = makeIndexedGeometryCacheKey(
                keyPrefix,
                sourceBatch.baseSubmeshIndex,
                cache.batches.size(),
                baseBatchCount);
            cache.batches.push_back(std::move(splitBatch));
            splitJointPaletteByBatch.emplace_back();
            splitTriangleCountByBatch.push_back(0u);
            chosenBatchIndex = cache.batches.size() - 1u;
            splitBatchCandidatesBySource[sourceBatchIndex].push_back(chosenBatchIndex);
        }

        auto& chosenPalette = splitJointPaletteByBatch[chosenBatchIndex];
        for (std::size_t ji = 0; ji < triJointCount; ++ji) {
            const bool exists = std::find(
                chosenPalette.begin(),
                chosenPalette.end(),
                triJoints[ji]) != chosenPalette.end();
            if (!exists) chosenPalette.push_back(triJoints[ji]);
        }

        ++splitTriangleCountByBatch[chosenBatchIndex];
        splitBatchIndexByTriangle[triIdx] = chosenBatchIndex;
    }

    batchIndexByTriangle.swap(splitBatchIndexByTriangle);
    triangleCountByBatch.swap(splitTriangleCountByBatch);

    std::vector<std::vector<int>> remapByBatch(cache.batches.size());
    for (std::size_t bi = 0; bi < cache.batches.size(); ++bi) {
        auto& batch = cache.batches[bi];
        const std::size_t triCountForBatch =
            (bi < triangleCountByBatch.size()) ? triangleCountByBatch[bi] : 0u;
        const std::size_t indexReserve = triCountForBatch * 3u;
        batch.indices.reserve(indexReserve);
        batch.sourceVertexIndices.reserve(std::min(mesh->vertices.size(), indexReserve));
        remapByBatch[bi].assign(mesh->vertices.size(), -1);
    }

    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        const std::size_t batchIndex = batchIndexByTriangle[triIdx];
        if (batchIndex >= cache.batches.size()) continue;
        const std::size_t i = triIdx * 3u;
        const std::uint32_t i0 = mesh->indices[i + 0];
        const std::uint32_t i1 = mesh->indices[i + 1];
        const std::uint32_t i2 = mesh->indices[i + 2];
        if (i0 >= mesh->vertices.size() ||
            i1 >= mesh->vertices.size() ||
            i2 >= mesh->vertices.size()) {
            continue;
        }

        auto& batch = cache.batches[batchIndex];
        auto& remap = remapByBatch[batchIndex];
        const auto appendVertex = [&](std::uint32_t src) -> std::uint32_t {
            int& mapped = remap[src];
            if (mapped >= 0) return static_cast<std::uint32_t>(mapped);
            mapped = static_cast<int>(batch.sourceVertexIndices.size());
            batch.sourceVertexIndices.push_back(src);
            return static_cast<std::uint32_t>(mapped);
        };

        batch.indices.push_back(appendVertex(i0));
        batch.indices.push_back(appendVertex(i1));
        batch.indices.push_back(appendVertex(i2));
    }

    for (auto& batch : cache.batches) {
        batch.gpuJointPalette.clear();
        std::unordered_map<std::uint16_t, std::uint16_t> jointRemap;
        jointRemap.reserve(16u);
        bool jointPaletteOverflow = false;
        for (const std::uint32_t srcIndex : batch.sourceVertexIndices) {
            if (srcIndex >= mesh->vertices.size()) continue;
            const auto& src = mesh->vertices[srcIndex];
            const std::uint16_t joints[4] = {src.j0, src.j1, src.j2, src.j3};
            const float weights[4] = {src.w0, src.w1, src.w2, src.w3};
            for (int ji = 0; ji < 4; ++ji) {
                if (weights[ji] <= 0.00001f) continue;
                if (jointRemap.find(joints[ji]) != jointRemap.end()) continue;
                if (jointRemap.size() >= kMaxGpuSkinMatrices) {
                    jointPaletteOverflow = true;
                    break;
                }
                const std::uint16_t next =
                    static_cast<std::uint16_t>(jointRemap.size());
                jointRemap.emplace(joints[ji], next);
                batch.gpuJointPalette.push_back(joints[ji]);
            }
            if (jointPaletteOverflow) break;
        }
        if (jointPaletteOverflow) {
            jointRemap.clear();
            batch.gpuJointPalette.clear();
        }

        batch.gpuTemplateVertices.resize(batch.sourceVertexIndices.size());
        for (std::size_t vi = 0; vi < batch.sourceVertexIndices.size(); ++vi) {
            const std::uint32_t srcIndex = batch.sourceVertexIndices[vi];
            const auto& src = mesh->vertices[srcIndex];
            IRenderBackend::WorldMeshVertex outVertex{};
            outVertex.x = src.position.x;
            outVertex.y = src.position.y;
            outVertex.z = src.position.z;
            outVertex.u = src.uv.x;
            outVertex.v = src.uv.y;
            const glm::vec3 authoredColor = mesh->hasVertexColor
                ? glm::clamp(glm::vec3(src.color.r, src.color.g, src.color.b), 0.0f, 1.0f)
                : glm::vec3(1.0f);
            const float authoredAlpha = mesh->hasVertexColor
                ? std::clamp(src.color.a, 0.0f, 1.0f)
                : 1.0f;
            outVertex.r = authoredColor.r;
            outVertex.g = authoredColor.g;
            outVertex.b = authoredColor.b;
            outVertex.a = authoredAlpha;
            outVertex.nx = src.normal.x;
            outVertex.ny = src.normal.y;
            outVertex.nz = src.normal.z;
            outVertex.tx = src.tangent.x;
            outVertex.ty = src.tangent.y;
            outVertex.tz = src.tangent.z;
            outVertex.tw = src.tangent.w;
            std::uint16_t mappedJ0 = src.j0;
            std::uint16_t mappedJ1 = src.j1;
            std::uint16_t mappedJ2 = src.j2;
            std::uint16_t mappedJ3 = src.j3;
            if (!jointRemap.empty()) {
                if (src.w0 > 0.00001f) {
                    const auto it = jointRemap.find(src.j0);
                    if (it != jointRemap.end()) mappedJ0 = it->second;
                }
                if (src.w1 > 0.00001f) {
                    const auto it = jointRemap.find(src.j1);
                    if (it != jointRemap.end()) mappedJ1 = it->second;
                }
                if (src.w2 > 0.00001f) {
                    const auto it = jointRemap.find(src.j2);
                    if (it != jointRemap.end()) mappedJ2 = it->second;
                }
                if (src.w3 > 0.00001f) {
                    const auto it = jointRemap.find(src.j3);
                    if (it != jointRemap.end()) mappedJ3 = it->second;
                }
            }
            outVertex.joint0 = static_cast<float>(mappedJ0);
            outVertex.joint1 = static_cast<float>(mappedJ1);
            outVertex.joint2 = static_cast<float>(mappedJ2);
            outVertex.joint3 = static_cast<float>(mappedJ3);
            outVertex.weight0 = src.w0;
            outVertex.weight1 = src.w1;
            outVertex.weight2 = src.w2;
            outVertex.weight3 = src.w3;
            batch.gpuTemplateVertices[vi] = outVertex;
        }
    }

    return cache.batches.empty() ? nullptr : &cache;
}
} // namespace

namespace game::runtime::shared_projected_unit_backend_mesh {

std::size_t prewarmProjectedUnitBackendMeshGeometryCache(
    IRenderBackend& renderer,
    const runtime::backend_model::MeshData& mesh) {
    if (!renderer.supportsWorldIndexedMeshes()) return 0u;
    if (mesh.vertices.empty() || mesh.indices.size() < 3u) return 0u;

    const std::size_t baseBatchCount =
        std::max<std::size_t>(1u, mesh.submeshBaseTextures.size());
    const std::vector<int> submeshNodeFallback = buildSubmeshNodeFallback(mesh);
    const FastTexturedMeshTemplateCache* fastCache =
        ensureFastTexturedMeshTemplateCache(&mesh, submeshNodeFallback, baseBatchCount);
    if (!fastCache) return 0u;

    std::size_t warmed = 0u;
    for (std::size_t bi = 0; bi < fastCache->batches.size(); ++bi) {
        const auto& batch = fastCache->batches[bi];
        if (batch.gpuTemplateVertices.empty() || batch.indices.size() < 3u) continue;
        renderer.prewarmWorldIndexedMeshCached(
            batch.geometryCacheKey.c_str(),
            batch.gpuTemplateVertices.data(),
            batch.gpuTemplateVertices.size(),
            batch.indices.data(),
            batch.indices.size());
        ++warmed;
    }
    return warmed;
}

Result renderProjectedUnitBackendMesh(const Args& args) {
    Result out{};
    if (!args.dataDb || !args.unit || !args.pose || !args.meshForUnit || !args.scenePose ||
        !args.tint || !args.projectedDebug || !args.sharedTailFireAnchors ||
        !args.worldIndexedBatches || !args.modelDepthTris || !args.modelDepthWorldTris ||
        !args.remainingModelTrianglesBudget || !args.world3DTriangles ||
        !args.backendModelTriangleLimit || !args.backendModelFullMeshEnabled ||
        !args.backendModelFastTexturedPathEnabled || !args.backendModelBackfaceCullingEnabled) {
        return out;
    }

    const auto& unit = *args.unit;
    const auto* meshForUnit = args.meshForUnit;

    const float captureVisualTintStrength = args.captureVisualTintStrength;
    const float modelFadeAlpha = args.modelFadeAlpha;
    const glm::vec3 captureTintColor = args.captureTintColor;
    const glm::vec3 cameraWorldPos = args.cameraWorldPos;

    const bool supportsWorldTriangles3D = args.supportsWorldTriangles3D;

    auto& projectedDebug = *args.projectedDebug;
    auto& sharedTailFireAnchors = *args.sharedTailFireAnchors;
    auto& worldIndexedBatches = *args.worldIndexedBatches;
    auto& modelDepthTris = *args.modelDepthTris;
    auto& modelDepthWorldTris = *args.modelDepthWorldTris;
    auto& world3DTriangles = *args.world3DTriangles;

    const auto& backendModelFastTexturedPathEnabled = args.backendModelFastTexturedPathEnabled;
    const auto& backendModelBackfaceCullingEnabled = args.backendModelBackfaceCullingEnabled;
    const bool strictGltfParity = strictGltfParityEnabled();

    using SharedTailFireAnchor = game::runtime::shared_tail_fire_fallback::Anchor;

    bool drewModelMesh = false;
    if (meshForUnit) {
        using Clock = std::chrono::high_resolution_clock;
        const auto prepStart = Clock::now();
        auto& prep = g_preparedMeshState;
        if (!shared_projected_unit_backend_mesh_prep::prepareProjectedUnitBackendMesh(args, out, prep)) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            return out;
        }

        const runtime::backend_model::MeshData* mesh = prep.mesh;
        const std::size_t triangleCount = prep.triangleCount;
        const std::size_t effectiveUnitTriangleBudget = prep.effectiveUnitTriangleBudget;
        const bool useIndexedWorldModelPath = prep.useIndexedWorldModelPath;
        const bool fullIndexedMeshPath = prep.fullIndexedMeshPath;
        const bool useFastTexturedFullMeshPath = prep.useFastTexturedFullMeshPath;
        const float resolvedScaleCorrection = prep.resolvedScaleCorrection;
        const std::size_t modelDepthCountBefore = prep.modelDepthCountBefore;
        const std::size_t modelDepthWorldCountBefore = prep.modelDepthWorldCountBefore;
        const std::size_t world3DTriangleCountBefore = prep.world3DTriangleCountBefore;
        const auto& submeshNodeFallback = *prep.submeshNodeFallback;
        auto& modelIndexedBatchesPerSubmesh = prep.modelIndexedBatchesPerSubmesh;
        auto& modelIndexedVertexRemap = prep.modelIndexedVertexRemap;
        const auto& nodeGlobals =
            prep.scenePose.hasScenePose ? prep.scenePose.nodeGlobals : mesh->bindNodeGlobals;
        const glm::vec3& lightDir = prep.lightDir;
        const glm::vec3& fallbackBase = prep.fallbackBase;
        const bool downsampleModelTriangles = prep.downsampleModelTriangles;
        const float fastTexturedAlpha = prep.fastTexturedAlpha;
        const glm::vec3& fastTexturedTint = prep.fastTexturedTint;
        shared_projected_unit_backend_mesh_transforms::Resolver transforms;
        transforms.initialize(args, prep);
        const auto geometryStart = Clock::now();
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(geometryStart - prepStart).count();
        }

        bool handledFastTexturedPath = false;
        const FastTexturedMeshTemplateCache* fastCachePtr = nullptr;
        if (useFastTexturedFullMeshPath && !modelIndexedBatchesPerSubmesh.empty()) {
            fastCachePtr = ensureFastTexturedMeshTemplateCache(
                mesh,
                submeshNodeFallback,
                modelIndexedBatchesPerSubmesh.size());
        }
        if (fastCachePtr) {
            const auto& fastCache = *fastCachePtr;

            const std::size_t initialBatchCount = modelIndexedBatchesPerSubmesh.size();
            if (fastCache.batches.size() > initialBatchCount) {
                for (std::size_t bi = initialBatchCount; bi < fastCache.batches.size(); ++bi) {
                    const std::size_t sourceBatchIndex = fastCache.batches[bi].baseSubmeshIndex;
                    if (sourceBatchIndex >= initialBatchCount) continue;
                    const auto& templateBatch = modelIndexedBatchesPerSubmesh[sourceBatchIndex];
                    modelIndexedBatchesPerSubmesh.push_back(templateBatch);
                    auto& newBatch = modelIndexedBatchesPerSubmesh.back();
                    newBatch.geometryCacheKey = fastCache.batches[bi].geometryCacheKey;
                    newBatch.vertices.clear();
                    newBatch.indices.clear();
                    newBatch.vertices.reserve(fastCache.batches[bi].sourceVertexIndices.size());
                    newBatch.indices.reserve(fastCache.batches[bi].indices.size());
                    newBatch.sharedVertices = nullptr;
                    newBatch.sharedVertexCount = 0u;
                    newBatch.sharedIndices = nullptr;
                    newBatch.sharedIndexCount = 0u;
                    newBatch.gpuSkinning = 0u;
                    newBatch.skinMatrixCount = 0u;
                    newBatch.sharedSkinMatrices = nullptr;
                    newBatch.skinMatrices.clear();
                }
            }

            for (auto& batch : modelIndexedBatchesPerSubmesh) {
                batch.gpuSkinning = 0u;
                batch.skinMatrixCount = 0u;
                batch.sharedSkinMatrices = nullptr;
                batch.skinMatrices.clear();
                batch.sharedVertices = nullptr;
                batch.sharedVertexCount = 0u;
                batch.sharedIndices = nullptr;
                batch.sharedIndexCount = 0u;
            }

            auto& gpuSkinBatchStates = g_gpuSkinBatchStateEntries;
            gpuSkinBatchStates.clear();
            if (gpuSkinBatchStates.capacity() < fastCache.batches.size()) {
                gpuSkinBatchStates.reserve(fastCache.batches.size());
            }
            for (std::size_t bi = 0; bi < fastCache.batches.size(); ++bi) {
                if (bi >= modelIndexedBatchesPerSubmesh.size()) continue;
                const auto& srcBatch = fastCache.batches[bi];
                auto& dstBatch = modelIndexedBatchesPerSubmesh[bi];
                dstBatch.vertexColorMulR = fastTexturedTint.r;
                dstBatch.vertexColorMulG = fastTexturedTint.g;
                dstBatch.vertexColorMulB = fastTexturedTint.b;
                dstBatch.vertexColorMulA = fastTexturedAlpha;
                int resolvedTriNodeIndex = srcBatch.triNodeIndex;
                if (resolvedTriNodeIndex < 0 && fastCache.defaultSkinNodeIndex >= 0) {
                    resolvedTriNodeIndex = fastCache.defaultSkinNodeIndex;
                }

                if (args.enableGpuClipSkinning) {
                    const int skinCacheKey = transforms.gpuSkinningCacheKeyForNode(
                        resolvedTriNodeIndex);
                    if (skinCacheKey >= 0) {
                        const bool hasJointPalette = !srcBatch.gpuJointPalette.empty();
                        UnitSkinMatrixKey batchStateKey{};
                        batchStateKey.unitId = unit.id;
                        batchStateKey.skinKey = skinCacheKey;
                        if (hasJointPalette) {
                            const std::size_t paletteCount = std::min(
                                srcBatch.gpuJointPalette.size(),
                                kMaxGpuSkinMatrices);
                            batchStateKey.paletteSize =
                                static_cast<std::uint32_t>(paletteCount);
                            for (std::size_t pi = 0; pi < paletteCount; ++pi) {
                                batchStateKey.palette[pi] = srcBatch.gpuJointPalette[pi];
                            }
                        }

                        auto stateIt = std::find_if(
                            gpuSkinBatchStates.begin(),
                            gpuSkinBatchStates.end(),
                            [&](const GpuSkinBatchStateEntry& entry) {
                                return entry.key == batchStateKey;
                            });
                        if (stateIt == gpuSkinBatchStates.end()) {
                            GpuSkinBatchState newState{};
                            auto& sharedSkinMatrices = g_unitSkinMatrices[batchStateKey];
                            if (transforms.configureGpuClipSkinningBatch(
                                    resolvedTriNodeIndex,
                                    hasJointPalette ? &srcBatch.gpuJointPalette : nullptr,
                                    newState.modelMatrix,
                                    sharedSkinMatrices,
                                    newState.skinMatrixCount)) {
                                newState.valid = true;
                                newState.sharedSkinMatrices = sharedSkinMatrices.data();
                            }
                            gpuSkinBatchStates.push_back(
                                GpuSkinBatchStateEntry{batchStateKey, newState});
                            stateIt = std::prev(gpuSkinBatchStates.end());
                        }
                        if (stateIt->state.valid) {
                            dstBatch.gpuSkinning = 1u;
                            dstBatch.modelMatrix = stateIt->state.modelMatrix;
                            dstBatch.skinMatrixCount = stateIt->state.skinMatrixCount;
                            dstBatch.sharedSkinMatrices = stateIt->state.sharedSkinMatrices;
                            dstBatch.skinMatrices.clear();
                        }
                    }
                }

                if (dstBatch.gpuSkinning != 0u) {
                    if (!srcBatch.gpuTemplateVertices.empty() && !srcBatch.indices.empty()) {
                        dstBatch.vertices.clear();
                        dstBatch.indices.clear();
                        dstBatch.sharedVertices = srcBatch.gpuTemplateVertices.data();
                        dstBatch.sharedVertexCount = srcBatch.gpuTemplateVertices.size();
                        dstBatch.sharedIndices = srcBatch.indices.data();
                        dstBatch.sharedIndexCount = srcBatch.indices.size();
                    } else {
                        dstBatch.geometryCacheKey.clear();
                        dstBatch.sharedVertices = nullptr;
                        dstBatch.sharedVertexCount = 0u;
                        dstBatch.indices.clear();
                        if (!srcBatch.indices.empty()) {
                            dstBatch.sharedIndices = srcBatch.indices.data();
                            dstBatch.sharedIndexCount = srcBatch.indices.size();
                        } else {
                            dstBatch.sharedIndices = nullptr;
                            dstBatch.sharedIndexCount = 0u;
                        }
                        dstBatch.vertices.resize(srcBatch.gpuTemplateVertices.size());
                        if (!srcBatch.gpuTemplateVertices.empty()) {
                            std::memcpy(
                                dstBatch.vertices.data(),
                                srcBatch.gpuTemplateVertices.data(),
                                srcBatch.gpuTemplateVertices.size() *
                                    sizeof(IRenderBackend::WorldMeshVertex));
                        }
                    }
                } else {
                    dstBatch.sharedVertices = nullptr;
                    dstBatch.sharedVertexCount = 0u;
                    dstBatch.sharedSkinMatrices = nullptr;
                    const auto& materialBatch =
                        shared_world_batches::resolvedMaterialBatch(dstBatch);
                    const bool needsLitNormals = materialBatch.materialMode >= 2u;
                    const bool hasNormalTexture =
                        shared_world_batches::resolvedHasNormalTexture(dstBatch);
                    const bool needsTangents = needsLitNormals && hasNormalTexture;
                    const bool canUseRigidNodeTransform =
                        prep.scenePose.hasClipPose &&
                        srcBatch.gpuJointPalette.empty() &&
                        !srcBatch.gpuTemplateVertices.empty() &&
                        !srcBatch.indices.empty();
                    const bool canUseDynamicLocalPosNoSkin =
                        !prep.scenePose.hasClipPose &&
                        srcBatch.gpuJointPalette.empty() &&
                        !srcBatch.gpuTemplateVertices.empty() &&
                        !srcBatch.indices.empty();
                    if (canUseRigidNodeTransform) {
                        dstBatch.vertices.clear();
                        dstBatch.indices.clear();
                        dstBatch.sharedVertices = srcBatch.gpuTemplateVertices.data();
                        dstBatch.sharedVertexCount = srcBatch.gpuTemplateVertices.size();
                        dstBatch.sharedIndices = srcBatch.indices.data();
                        dstBatch.sharedIndexCount = srcBatch.indices.size();

                        glm::mat4 nodeGlobal(1.0f);
                        if (resolvedTriNodeIndex >= 0 &&
                            static_cast<std::size_t>(resolvedTriNodeIndex) < nodeGlobals.size()) {
                            nodeGlobal = nodeGlobals[static_cast<std::size_t>(resolvedTriNodeIndex)];
                        }
                        const glm::mat4 batchModel = prep.modelM * nodeGlobal;
                        const float* batchModelData = glm::value_ptr(batchModel);
                        std::copy(batchModelData, batchModelData + 16, dstBatch.modelMatrix.begin());
                    } else if (canUseDynamicLocalPosNoSkin) {
                        dstBatch.geometryCacheKey.clear();
                        dstBatch.indices.clear();
                        dstBatch.sharedIndices = srcBatch.indices.data();
                        dstBatch.sharedIndexCount = srcBatch.indices.size();
                        dstBatch.vertices.resize(srcBatch.sourceVertexIndices.size());
                        for (std::size_t vi = 0; vi < srcBatch.sourceVertexIndices.size(); ++vi) {
                            const std::uint32_t srcIndex = srcBatch.sourceVertexIndices[vi];
                            if (srcIndex >= mesh->vertices.size()) continue;
                            const auto& srcVertex = mesh->vertices[srcIndex];
                            IRenderBackend::WorldMeshVertex outVertex = srcBatch.gpuTemplateVertices[vi];
                            const glm::vec3 localPos = transforms.resolveDeformedLocalVertexPos(
                                srcIndex, srcVertex);
                            outVertex.x = localPos.x;
                            outVertex.y = localPos.y;
                            outVertex.z = localPos.z;
                            dstBatch.vertices[vi] = outVertex;
                        }

                        glm::mat4 nodeGlobal(1.0f);
                        if (resolvedTriNodeIndex >= 0 &&
                            static_cast<std::size_t>(resolvedTriNodeIndex) < nodeGlobals.size()) {
                            nodeGlobal = nodeGlobals[static_cast<std::size_t>(resolvedTriNodeIndex)];
                        }
                        const glm::mat4 batchModel = prep.modelM * nodeGlobal;
                        const float* batchModelData = glm::value_ptr(batchModel);
                        std::copy(batchModelData, batchModelData + 16, dstBatch.modelMatrix.begin());
                    } else {
                        dstBatch.geometryCacheKey.clear();
                        dstBatch.indices.clear();
                        if (!srcBatch.indices.empty()) {
                            dstBatch.sharedIndices = srcBatch.indices.data();
                            dstBatch.sharedIndexCount = srcBatch.indices.size();
                        } else {
                            dstBatch.sharedIndices = nullptr;
                            dstBatch.sharedIndexCount = 0u;
                        }
                        dstBatch.vertices.resize(srcBatch.sourceVertexIndices.size());
                        for (std::size_t vi = 0; vi < srcBatch.sourceVertexIndices.size(); ++vi) {
                            const std::uint32_t srcIndex = srcBatch.sourceVertexIndices[vi];
                            if (srcIndex >= mesh->vertices.size()) continue;
                            const auto& srcVertex = mesh->vertices[srcIndex];

                            IRenderBackend::WorldMeshVertex outVertex = srcBatch.gpuTemplateVertices[vi];
                            const glm::vec3 pos = transforms.resolveWorldVertexPos(
                                resolvedTriNodeIndex, srcIndex, srcVertex);
                            outVertex.x = pos.x;
                            outVertex.y = pos.y;
                            outVertex.z = pos.z;
                            if (needsLitNormals) {
                                const glm::vec3 nrm = transforms.resolveModelVertexNormal(
                                    resolvedTriNodeIndex, srcIndex, srcVertex);
                                outVertex.nx = nrm.x;
                                outVertex.ny = nrm.y;
                                outVertex.nz = nrm.z;
                            }
                            if (needsTangents) {
                                const glm::vec4 tan = transforms.resolveModelVertexTangent(
                                    resolvedTriNodeIndex, srcIndex, srcVertex);
                                outVertex.tx = tan.x;
                                outVertex.ty = tan.y;
                                outVertex.tz = tan.z;
                                outVertex.tw = tan.w;
                            }
                            dstBatch.vertices[vi] = outVertex;
                        }
                    }
                }
            }
            handledFastTexturedPath = true;
            bool fastPathHasGeometry = false;
            for (const auto& batch : modelIndexedBatchesPerSubmesh) {
                if (batch.hasGeometry()) {
                    fastPathHasGeometry = true;
                    break;
                }
            }
            if (!fastPathHasGeometry) {
                handledFastTexturedPath = false;
                for (auto& batch : modelIndexedBatchesPerSubmesh) {
                    batch.vertices.clear();
                    batch.indices.clear();
                    batch.geometryCacheKey.clear();
                    batch.sharedVertices = nullptr;
                    batch.sharedVertexCount = 0u;
                    batch.sharedIndices = nullptr;
                    batch.sharedIndexCount = 0u;
                    batch.gpuSkinning = 0u;
                    batch.skinMatrixCount = 0u;
                    batch.sharedSkinMatrices = nullptr;
                    batch.skinMatrices.clear();
                }
            }
        }

        if (unit.alive && !unit.fainting && toLowerCopy(unit.name) == "charmander") {
            const TailFireVFX::Config& tailCfg =
                game::runtime::shared_projected_scene::getTailFireFallbackCfg();
            auto resolveNodeIndex = [&](const std::string& nodeName, int fallbackIndex) {
                int idx = fallbackIndex;
                if (!nodeName.empty()) {
                    bool resolvedByName = false;
                    if (!mesh->nodeNames.empty()) {
                        for (std::size_t ni = 0; ni < mesh->nodeNames.size(); ++ni) {
                            if (mesh->nodeNames[ni] == nodeName) {
                                idx = static_cast<int>(ni);
                                resolvedByName = true;
                                break;
                            }
                        }
                    } else if (unit.model) {
                        int namedNodeIndex = -1;
                        if (unit.model->getNodeIndexByName(nodeName, namedNodeIndex)) {
                            idx = namedNodeIndex;
                            resolvedByName = true;
                        }
                    }

                    if (!resolvedByName && fallbackIndex < 0) {
                        idx = -1;
                    }
                }
                return idx;
            };
            auto safeNorm = [](glm::vec3 v, const glm::vec3& fallback) {
                const float len2 = glm::dot(v, v);
                if (len2 <= 1e-10f) return fallback;
                return v * (1.0f / std::sqrt(len2));
            };
            auto buildFireAnchorBasis = [&](const glm::mat4& baseWorldM,
                                            const glm::vec3& basePosWorld,
                                            const glm::vec3& tipPosWorld) {
                const glm::vec3 upAxis =
                    safeNorm(tipPosWorld - basePosWorld,
                             safeNorm(glm::vec3(baseWorldM[1]), glm::vec3(0.0f, 1.0f, 0.0f)));

                glm::vec3 xHint = glm::vec3(baseWorldM[0]);
                xHint -= upAxis * glm::dot(xHint, upAxis);
                if (glm::dot(xHint, xHint) <= 1e-10f) {
                    xHint = glm::vec3(baseWorldM[2]);
                    xHint -= upAxis * glm::dot(xHint, upAxis);
                }

                glm::vec3 xFallback = (std::fabs(upAxis.y) < 0.95f)
                    ? safeNorm(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), upAxis), glm::vec3(1.0f, 0.0f, 0.0f))
                    : glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 xAxis = safeNorm(xHint, xFallback);
                glm::vec3 zAxis = safeNorm(glm::cross(xAxis, upAxis), glm::vec3(0.0f, 0.0f, 1.0f));
                xAxis = safeNorm(glm::cross(upAxis, zAxis), xAxis);
                if (glm::dot(glm::cross(xAxis, upAxis), zAxis) < 0.0f) {
                    zAxis = -zAxis;
                }
                return glm::mat3(xAxis, upAxis, zAxis);
            };

            const int tailNodeIndex = resolveNodeIndex(tailCfg.tailTipNodeName, tailCfg.tailTipNodeIndex);
            const int fireAnchorBaseNodeIndex = resolveNodeIndex(tailCfg.fireAnchorBaseNodeName, -1);
            const int fireAnchorTipNodeIndex = resolveNodeIndex(tailCfg.fireAnchorTipNodeName, -1);

            SharedTailFireAnchor& anchor = sharedTailFireAnchors[unit.id];
            anchor.valid = false;
            anchor.meshCarrierActive = false;

            const bool hasExactFireAnchorNodes =
                fireAnchorBaseNodeIndex >= 0 &&
                fireAnchorTipNodeIndex >= 0 &&
                static_cast<std::size_t>(fireAnchorBaseNodeIndex) < nodeGlobals.size() &&
                static_cast<std::size_t>(fireAnchorTipNodeIndex) < nodeGlobals.size();
            if (hasExactFireAnchorNodes) {
                const glm::mat4& baseWorldM = transforms.worldMatrixForNode(fireAnchorBaseNodeIndex);
                const glm::mat4& tipWorldM = transforms.worldMatrixForNode(fireAnchorTipNodeIndex);
                const glm::vec3 basePosWorld = glm::vec3(baseWorldM[3]);
                const glm::vec3 tipPosWorld = glm::vec3(tipWorldM[3]);
                const glm::mat3 fireBasis = buildFireAnchorBasis(baseWorldM, basePosWorld, tipPosWorld);
                glm::vec3 backDirWorld = fireBasis * tailCfg.backDir;
                backDirWorld = safeNorm(backDirWorld, glm::vec3(0.0f, 1.0f, 0.0f));

                anchor.valid = true;
                anchor.exactFireAnchor = true;
                anchor.pos = basePosWorld;
                anchor.tipPos = tipPosWorld;
                anchor.basis = fireBasis;
                anchor.backDir = backDirWorld;
                anchor.particleSizeScale =
                    std::max(0.01f, std::max(0.01f, mesh->modelScaleFactor) * resolvedScaleCorrection);
                if (tailFireDebugShouldLogAnchor(unit.id)) {
                    std::cout
                        << "[TailFire][Debug][Anchor] unit=" << unit.id
                        << " exact=1"
                        << " tailNode=" << tailNodeIndex
                        << " baseNode=" << fireAnchorBaseNodeIndex
                        << " tipNode=" << fireAnchorTipNodeIndex
                        << " basePos=(" << basePosWorld.x << "," << basePosWorld.y << "," << basePosWorld.z << ")"
                        << " tipPos=(" << tipPosWorld.x << "," << tipPosWorld.y << "," << tipPosWorld.z << ")"
                        << " up=(" << fireBasis[1].x << "," << fireBasis[1].y << "," << fireBasis[1].z << ")"
                        << " back=(" << backDirWorld.x << "," << backDirWorld.y << "," << backDirWorld.z << ")"
                        << " scale=" << anchor.particleSizeScale
                        << "\n";
                }
            } else if (tailNodeIndex >= 0 &&
                       static_cast<std::size_t>(tailNodeIndex) < nodeGlobals.size()) {
                const glm::mat4& tailWorldM = transforms.worldMatrixForNode(tailNodeIndex);
                glm::vec3 bx = safeNorm(glm::vec3(tailWorldM[0]), glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 by = glm::vec3(tailWorldM[1]);
                by = by - bx * glm::dot(by, bx);
                by = safeNorm(by, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 bz = safeNorm(glm::cross(bx, by), glm::vec3(0.0f, 0.0f, 1.0f));
                if (glm::dot(glm::cross(bx, by), bz) < 0.0f) {
                    bz = -bz;
                }
                const glm::mat3 tailBasis(bx, by, bz);
                glm::vec3 backDirWorld = tailBasis * tailCfg.backDir;
                backDirWorld = safeNorm(backDirWorld, glm::vec3(0.0f, 1.0f, 0.0f));

                anchor.valid = true;
                anchor.exactFireAnchor = false;
                anchor.pos = glm::vec3(tailWorldM[3]);
                anchor.tipPos = anchor.pos;
                anchor.basis = tailBasis;
                anchor.backDir = backDirWorld;
                anchor.particleSizeScale =
                    std::max(0.01f, std::max(0.01f, mesh->modelScaleFactor) * resolvedScaleCorrection);
                if (tailFireDebugShouldLogAnchor(unit.id)) {
                    std::cout
                        << "[TailFire][Debug][Anchor] unit=" << unit.id
                        << " exact=0"
                        << " tailNode=" << tailNodeIndex
                        << " baseNode=" << fireAnchorBaseNodeIndex
                        << " tipNode=" << fireAnchorTipNodeIndex
                        << " tailPos=(" << anchor.pos.x << "," << anchor.pos.y << "," << anchor.pos.z << ")"
                        << " up=(" << tailBasis[1].x << "," << tailBasis[1].y << "," << tailBasis[1].z << ")"
                        << " back=(" << backDirWorld.x << "," << backDirWorld.y << "," << backDirWorld.z << ")"
                        << " scale=" << anchor.particleSizeScale
                        << "\n";
                }
            }
        }

        if (!handledFastTexturedPath) {
            shared_projected_unit_backend_mesh_submit::TriangleSubmitter triangleSubmitter;
            triangleSubmitter.initialize(
                shared_projected_unit_backend_mesh_submit::TriangleSubmitter::Args{
                    supportsWorldTriangles3D,
                    useIndexedWorldModelPath,
                    fullIndexedMeshPath,
                    backendModelFastTexturedPathEnabled(),
                    backendModelBackfaceCullingEnabled(),
                    cameraWorldPos,
                    lightDir,
                    &projectedDebug,
                    &modelIndexedBatchesPerSubmesh,
                    &modelIndexedVertexRemap,
                    &modelDepthTris,
                    &world3DTriangles});

            if (useFastTexturedFullMeshPath &&
                modelIndexedVertexRemap.empty() &&
                !mesh->vertices.empty() &&
                !modelIndexedBatchesPerSubmesh.empty()) {
                modelIndexedVertexRemap.resize(modelIndexedBatchesPerSubmesh.size());
                for (auto& remap : modelIndexedVertexRemap) {
                    remap.assign(mesh->vertices.size(), -1);
                }
            }

            auto& triNodeIndexByTriangle = g_triNodeIndexByTriangleScratch;
            triNodeIndexByTriangle.assign(triangleCount, -1);
            for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                int triNodeIndex =
                    (triIdx < mesh->triangleNodeIndex.size())
                        ? mesh->triangleNodeIndex[triIdx]
                        : -1;
                if (triNodeIndex < 0 &&
                    triIdx < mesh->triangleSubmesh.size() &&
                    !submeshNodeFallback.empty()) {
                    const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                    if (submeshIndex < submeshNodeFallback.size()) {
                        triNodeIndex = submeshNodeFallback[submeshIndex];
                    }
                }
                triNodeIndexByTriangle[triIdx] = triNodeIndex;
            }

            std::size_t previousTriSample = triangleCount;
            for (std::size_t sampleIdx = 0; sampleIdx < effectiveUnitTriangleBudget; ++sampleIdx) {
            std::size_t triIdx = sampleIdx;
            if (downsampleModelTriangles) {
                triIdx =
                    selectUniformTriangleIndex(sampleIdx, effectiveUnitTriangleBudget, triangleCount);
                if (triIdx == previousTriSample && triIdx + 1u < triangleCount) ++triIdx;
            }
            previousTriSample = triIdx;

            const std::size_t i = triIdx * 3u;
            const std::uint32_t i0 = mesh->indices[i + 0];
            const std::uint32_t i1 = mesh->indices[i + 1];
            const std::uint32_t i2 = mesh->indices[i + 2];
            if (i0 >= mesh->vertices.size() ||
                i1 >= mesh->vertices.size() ||
                i2 >= mesh->vertices.size()) {
                continue;
            }

            const auto& v0 = mesh->vertices[i0];
            const auto& v1 = mesh->vertices[i1];
            const auto& v2 = mesh->vertices[i2];

            const int triNodeIndex = triNodeIndexByTriangle[triIdx];

            const std::uint16_t triSubmeshIndex =
                (triIdx < mesh->triangleSubmesh.size())
                    ? mesh->triangleSubmesh[triIdx]
                    : static_cast<std::uint16_t>(0u);
            bool needsLitNormalsForSubmesh = true;
            bool needsTangentsForSubmesh = true;
            if (useIndexedWorldModelPath && !modelIndexedBatchesPerSubmesh.empty()) {
                std::size_t submeshBatchIndex = static_cast<std::size_t>(triSubmeshIndex);
                if (submeshBatchIndex >= modelIndexedBatchesPerSubmesh.size()) {
                    submeshBatchIndex = 0u;
                }
                const auto& submeshBatch = modelIndexedBatchesPerSubmesh[submeshBatchIndex];
                const auto& materialBatch =
                    shared_world_batches::resolvedMaterialBatch(submeshBatch);
                needsLitNormalsForSubmesh = materialBatch.materialMode >= 2u;
                const bool hasNormalTexture =
                    shared_world_batches::resolvedHasNormalTexture(submeshBatch);
                needsTangentsForSubmesh = needsLitNormalsForSubmesh && hasNormalTexture;
            }
            const bool texturedSubmesh =
                useIndexedWorldModelPath &&
                static_cast<std::size_t>(triSubmeshIndex) <
                    modelIndexedBatchesPerSubmesh.size() &&
                shared_world_batches::resolvedHasBaseTexture(
                    modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]);
            if (useFastTexturedFullMeshPath && texturedSubmesh) {
                std::size_t fastBatchIndex = static_cast<std::size_t>(triSubmeshIndex);
                if (fastBatchIndex >= modelIndexedBatchesPerSubmesh.size()) fastBatchIndex = 0u;
                auto& fastBatch = modelIndexedBatchesPerSubmesh[fastBatchIndex];
                fastBatch.vertexColorMulR = fastTexturedTint.r;
                fastBatch.vertexColorMulG = fastTexturedTint.g;
                fastBatch.vertexColorMulB = fastTexturedTint.b;
                fastBatch.vertexColorMulA = fastTexturedAlpha;
                const bool useGpuSkinning = (fastBatch.gpuSkinning != 0u);
                const bool canReuseIndexedVertices =
                    fastBatchIndex < modelIndexedVertexRemap.size();
                const auto appendFastVertex = [&](std::uint32_t src,
                                                  const runtime::backend_model::MeshVertex& srcVertex)
                    -> std::uint32_t {
                    if (canReuseIndexedVertices &&
                        src < modelIndexedVertexRemap[fastBatchIndex].size()) {
                        int& mapped = modelIndexedVertexRemap[fastBatchIndex][src];
                        if (mapped >= 0) {
                            return static_cast<std::uint32_t>(mapped);
                        }
                        if (fastBatch.vertices.size() >=
                            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                            return std::numeric_limits<std::uint32_t>::max();
                        }
                        const glm::vec3 pos = useGpuSkinning
                            ? transforms.resolveGpuSkinningInputPos(src, srcVertex)
                            : transforms.resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                        const std::uint32_t next =
                            static_cast<std::uint32_t>(fastBatch.vertices.size());
                        IRenderBackend::WorldMeshVertex outVertex{};
                        outVertex.x = pos.x;
                        outVertex.y = pos.y;
                        outVertex.z = pos.z;
                        outVertex.u = srcVertex.uv.x;
                        outVertex.v = srcVertex.uv.y;
                        const glm::vec3 authoredVertexColor = mesh->hasVertexColor
                            ? glm::clamp(
                                glm::vec3(srcVertex.color.r, srcVertex.color.g, srcVertex.color.b),
                                0.0f,
                                1.0f)
                            : glm::vec3(1.0f);
                        const float authoredVertexAlpha = mesh->hasVertexColor
                            ? std::clamp(srcVertex.color.a, 0.0f, 1.0f)
                            : 1.0f;
                        outVertex.r = authoredVertexColor.r;
                        outVertex.g = authoredVertexColor.g;
                        outVertex.b = authoredVertexColor.b;
                        outVertex.a = authoredVertexAlpha;
                        outVertex.nx = srcVertex.normal.x;
                        outVertex.ny = srcVertex.normal.y;
                        outVertex.nz = srcVertex.normal.z;
                        outVertex.tx = srcVertex.tangent.x;
                        outVertex.ty = srcVertex.tangent.y;
                        outVertex.tz = srcVertex.tangent.z;
                        outVertex.tw = srcVertex.tangent.w;
                        if (useGpuSkinning) {
                            // Authored tangent frame is consumed by GPU skinning path.
                        } else {
                            if (needsLitNormalsForSubmesh) {
                            const glm::vec3 nrm =
                                transforms.resolveModelVertexNormal(triNodeIndex, src, srcVertex);
                            outVertex.nx = nrm.x;
                            outVertex.ny = nrm.y;
                            outVertex.nz = nrm.z;
                            }
                            if (needsTangentsForSubmesh) {
                            const glm::vec4 tan =
                                transforms.resolveModelVertexTangent(triNodeIndex, src, srcVertex);
                            outVertex.tx = tan.x;
                            outVertex.ty = tan.y;
                            outVertex.tz = tan.z;
                            outVertex.tw = tan.w;
                            }
                        }
                        if (useGpuSkinning) {
                            outVertex.joint0 = static_cast<float>(srcVertex.j0);
                            outVertex.joint1 = static_cast<float>(srcVertex.j1);
                            outVertex.joint2 = static_cast<float>(srcVertex.j2);
                            outVertex.joint3 = static_cast<float>(srcVertex.j3);
                            outVertex.weight0 = srcVertex.w0;
                            outVertex.weight1 = srcVertex.w1;
                            outVertex.weight2 = srcVertex.w2;
                            outVertex.weight3 = srcVertex.w3;
                        }
                        fastBatch.vertices.push_back(outVertex);
                        mapped = static_cast<int>(next);
                        return next;
                    }
                    if (fastBatch.vertices.size() >=
                        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                        return std::numeric_limits<std::uint32_t>::max();
                    }
                    const glm::vec3 pos = useGpuSkinning
                        ? transforms.resolveGpuSkinningInputPos(src, srcVertex)
                        : transforms.resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                    const std::uint32_t next =
                        static_cast<std::uint32_t>(fastBatch.vertices.size());
                    IRenderBackend::WorldMeshVertex outVertex{};
                    outVertex.x = pos.x;
                    outVertex.y = pos.y;
                    outVertex.z = pos.z;
                    outVertex.u = srcVertex.uv.x;
                    outVertex.v = srcVertex.uv.y;
                    const glm::vec3 authoredVertexColor = mesh->hasVertexColor
                        ? glm::clamp(
                            glm::vec3(srcVertex.color.r, srcVertex.color.g, srcVertex.color.b),
                            0.0f,
                            1.0f)
                        : glm::vec3(1.0f);
                    const float authoredVertexAlpha = mesh->hasVertexColor
                        ? std::clamp(srcVertex.color.a, 0.0f, 1.0f)
                        : 1.0f;
                    outVertex.r = authoredVertexColor.r;
                    outVertex.g = authoredVertexColor.g;
                    outVertex.b = authoredVertexColor.b;
                    outVertex.a = authoredVertexAlpha;
                    outVertex.nx = srcVertex.normal.x;
                    outVertex.ny = srcVertex.normal.y;
                    outVertex.nz = srcVertex.normal.z;
                    outVertex.tx = srcVertex.tangent.x;
                    outVertex.ty = srcVertex.tangent.y;
                    outVertex.tz = srcVertex.tangent.z;
                    outVertex.tw = srcVertex.tangent.w;
                    if (useGpuSkinning) {
                        // Authored tangent frame is consumed by GPU skinning path.
                    } else {
                        if (needsLitNormalsForSubmesh) {
                        const glm::vec3 nrm =
                            transforms.resolveModelVertexNormal(triNodeIndex, src, srcVertex);
                        outVertex.nx = nrm.x;
                        outVertex.ny = nrm.y;
                        outVertex.nz = nrm.z;
                        }
                        if (needsTangentsForSubmesh) {
                        const glm::vec4 tan =
                            transforms.resolveModelVertexTangent(triNodeIndex, src, srcVertex);
                        outVertex.tx = tan.x;
                        outVertex.ty = tan.y;
                        outVertex.tz = tan.z;
                        outVertex.tw = tan.w;
                        }
                    }
                    if (useGpuSkinning) {
                        outVertex.joint0 = static_cast<float>(srcVertex.j0);
                        outVertex.joint1 = static_cast<float>(srcVertex.j1);
                        outVertex.joint2 = static_cast<float>(srcVertex.j2);
                        outVertex.joint3 = static_cast<float>(srcVertex.j3);
                        outVertex.weight0 = srcVertex.w0;
                        outVertex.weight1 = srcVertex.w1;
                        outVertex.weight2 = srcVertex.w2;
                        outVertex.weight3 = srcVertex.w3;
                    }
                    fastBatch.vertices.push_back(outVertex);
                    return next;
                };

                const std::uint32_t outI0 = appendFastVertex(i0, v0);
                const std::uint32_t outI1 = appendFastVertex(i1, v1);
                const std::uint32_t outI2 = appendFastVertex(i2, v2);
                if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
                    outI1 == std::numeric_limits<std::uint32_t>::max() ||
                    outI2 == std::numeric_limits<std::uint32_t>::max()) {
                    continue;
                }
                fastBatch.indices.push_back(outI0);
                fastBatch.indices.push_back(outI1);
                fastBatch.indices.push_back(outI2);
                continue;
            }

            const float triOpacity = (triIdx < mesh->triangleOpacity.size())
                ? mesh->triangleOpacity[triIdx]
                : 1.0f;
            // Textured indexed batches apply alpha in the pixel shader.
            // Avoid pre-multiplying with sampled triangle opacity (which would double-attenuate).
            const float alphaBase = std::clamp(modelFadeAlpha, 0.0f, 1.0f);
            const float alpha = texturedSubmesh
                ? alphaBase
                : alphaBase * std::clamp(triOpacity, 0.0f, 1.0f);
            if (alpha < 0.03f && !texturedSubmesh) continue;
            const bool triDoubleSided =
                (triIdx < mesh->triangleDoubleSided.size()) &&
                (mesh->triangleDoubleSided[triIdx] != 0u);

            glm::vec3 a(0.0f);
            glm::vec3 b(0.0f);
            glm::vec3 c(0.0f);
            glm::vec3 n0(0.0f, 1.0f, 0.0f);
            glm::vec3 n1(0.0f, 1.0f, 0.0f);
            glm::vec3 n2(0.0f, 1.0f, 0.0f);
            glm::vec4 t0(0.0f, 0.0f, 0.0f, 1.0f);
            glm::vec4 t1(0.0f, 0.0f, 0.0f, 1.0f);
            glm::vec4 t2(0.0f, 0.0f, 0.0f, 1.0f);
            if (useIndexedWorldModelPath) {
                a = transforms.resolveWorldVertexPos(triNodeIndex, i0, v0);
                b = transforms.resolveWorldVertexPos(triNodeIndex, i1, v1);
                c = transforms.resolveWorldVertexPos(triNodeIndex, i2, v2);
                if (needsLitNormalsForSubmesh) {
                    n0 = transforms.resolveModelVertexNormal(triNodeIndex, i0, v0);
                    n1 = transforms.resolveModelVertexNormal(triNodeIndex, i1, v1);
                    n2 = transforms.resolveModelVertexNormal(triNodeIndex, i2, v2);
                } else {
                    n0 = v0.normal;
                    n1 = v1.normal;
                    n2 = v2.normal;
                }
                if (needsTangentsForSubmesh) {
                    t0 = transforms.resolveModelVertexTangent(triNodeIndex, i0, v0);
                    t1 = transforms.resolveModelVertexTangent(triNodeIndex, i1, v1);
                    t2 = transforms.resolveModelVertexTangent(triNodeIndex, i2, v2);
                } else {
                    t0 = v0.tangent;
                    t1 = v1.tangent;
                    t2 = v2.tangent;
                }
            } else {
                const auto sk0 = transforms.resolveWorldVertex(triNodeIndex, i0, v0);
                const auto sk1 = transforms.resolveWorldVertex(triNodeIndex, i1, v1);
                const auto sk2 = transforms.resolveWorldVertex(triNodeIndex, i2, v2);
                a = sk0.pos;
                b = sk1.pos;
                c = sk2.pos;
                n0 = sk0.normal;
                n1 = sk1.normal;
                n2 = sk2.normal;
                t0 = v0.tangent;
                t1 = v1.tangent;
                t2 = v2.tangent;
            }

            glm::vec3 baseColor0 = fallbackBase;
            glm::vec3 baseColor1 = fallbackBase;
            glm::vec3 baseColor2 = fallbackBase;
            auto resolveVertexBase = [&](std::uint32_t vi,
                                         const runtime::backend_model::MeshVertex& v) {
                if (texturedSubmesh) {
                    // For textured glTF submeshes, preserve texture albedo.
                    // Use authored vertex color only when it exists in source.
                    if (mesh->hasVertexColor) {
                        return glm::clamp(
                            glm::vec3(v.color.r, v.color.g, v.color.b), 0.0f, 1.0f);
                    }
                    return glm::vec3(1.0f);
                }
                // For backend world-lit model rendering, do NOT use cached vertexBaseColors.
                // Those are legacy precomposed/tonemapped colors and will darken/desaturate when
                // fed through the modern PBR+ACES path again.
                if (mesh->hasVertexColor) {
                    return glm::clamp(
                        glm::vec3(v.color.r, v.color.g, v.color.b), 0.0f, 1.0f);
                }
                if (triIdx < mesh->triangleSubmesh.size() &&
                    !mesh->submeshBaseColors.empty()) {
                    const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                    if (submeshIndex < mesh->submeshBaseColors.size()) {
                        const glm::vec4 subColor = mesh->submeshBaseColors[submeshIndex];
                        return glm::clamp(
                            glm::vec3(subColor.r, subColor.g, subColor.b), 0.0f, 1.0f);
                    }
                }
                (void)vi;
                return fallbackBase;
            };
            baseColor0 = resolveVertexBase(i0, v0);
            baseColor1 = resolveVertexBase(i1, v1);
            baseColor2 = resolveVertexBase(i2, v2);
            if (!strictGltfParity && captureVisualTintStrength > 0.001f) {
                const float tintAmt = std::clamp(captureVisualTintStrength, 0.0f, 1.0f);
                baseColor0 = glm::mix(baseColor0, captureTintColor, tintAmt);
                baseColor1 = glm::mix(baseColor1, captureTintColor, tintAmt);
                baseColor2 = glm::mix(baseColor2, captureTintColor, tintAmt);
            }
            triangleSubmitter.pushTriangle(
                a,
                b,
                c,
                i0,
                i1,
                i2,
                v0.uv,
                v1.uv,
                v2.uv,
                n0,
                n1,
                n2,
                t0,
                t1,
                t2,
                baseColor0,
                baseColor1,
                baseColor2,
                triSubmeshIndex,
                alpha,
                triDoubleSided);
            }
        }
        bool queuedIndexedBatch = false;
        if (useIndexedWorldModelPath && !modelIndexedBatchesPerSubmesh.empty()) {
            (void)applyCharmanderFireMeshFlipbookOverride(args, *mesh, modelIndexedBatchesPerSubmesh);
            for (auto& batch : modelIndexedBatchesPerSubmesh) {
                if (!batch.hasGeometry()) continue;
                worldIndexedBatches.push_back(std::move(batch));
                queuedIndexedBatch = true;
            }
        }

        drewModelMesh = runtime::backend_units::didAccumulateModelGeometry(
            modelDepthCountBefore,
            modelDepthTris.size(),
            modelDepthWorldCountBefore,
            modelDepthWorldTris.size()) ||
            (world3DTriangles.size() > world3DTriangleCountBefore) ||
            queuedIndexedBatch;
        if (args.perfBreakdown) {
            args.perfBreakdown->geometryMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - geometryStart).count();
        }
    }
    out.drewModelMesh = drewModelMesh;
    return out;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh
