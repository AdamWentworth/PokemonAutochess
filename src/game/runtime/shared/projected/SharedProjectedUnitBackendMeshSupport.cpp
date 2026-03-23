#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"

#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"

#include "engine/core/Environment.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::string makeIndexedBatchKeyPrefix(
    const game::runtime::render_model::MeshData& mesh) {
    return "__runtime_mesh__:" +
           std::to_string(static_cast<unsigned long long>(
               reinterpret_cast<std::uintptr_t>(&mesh)));
}

bool tailFireDebugEnabled() {
    static const bool enabled = engine::env::flagEnabled("PAC_TAIL_FIRE_DEBUG");
    return enabled;
}

constexpr int kClampToEdge = 33071;
constexpr unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};

std::string buildWorldTextureCacheKey(const std::string& key,
                                      int width,
                                      int height,
                                      int wrapS,
                                      int wrapT,
                                      bool srgb) {
    if (key.empty() || width <= 0 || height <= 0) return {};
    std::string cacheKey = key;
    cacheKey += "|";
    cacheKey += std::to_string(width);
    cacheKey += "x";
    cacheKey += std::to_string(height);
    cacheKey += "|ws=";
    cacheKey += std::to_string(wrapS);
    cacheKey += "|wt=";
    cacheKey += std::to_string(wrapT);
    cacheKey += srgb ? "|srgb" : "|lin";
    return cacheKey;
}

auto& fastTexturedMeshTemplateCaches() {
    using Cache = game::runtime::shared_projected_unit_backend_mesh_support::
        FastTexturedMeshTemplateCache;
    static thread_local std::unordered_map<
        const game::runtime::render_model::MeshData*,
        Cache> caches;
    return caches;
}

auto& fastTexturedMaterialTemplateCaches() {
    using Cache = game::runtime::shared_projected_unit_backend_mesh_support::
        FastTexturedMaterialTemplateCache;
    static thread_local std::unordered_map<
        const game::runtime::render_model::MeshData*,
        Cache> caches;
    return caches;
}

} // namespace

namespace game::runtime::shared_projected_unit_backend_mesh_support {

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

std::vector<int> buildSubmeshNodeFallback(
    const game::runtime::render_model::MeshData& mesh) {
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

bool applyTailFireMeshFlipbookOverride(
    const Args& args,
    const game::runtime::render_model::MeshData& mesh,
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& batches) {
    if (!args.unit || !args.sharedTailFireAnchors) {
        return false;
    }
    if (!game::runtime::shared_tail_fire_mesh_playback::isTailFireMeshPlaybackSpecies(
            args.unit->name)) {
        return false;
    }

    const auto& profile =
        game::runtime::shared_tail_fire_mesh_playback::resolveProfile(mesh);
    if (!profile.hasFireSubmesh || profile.spec.path == nullptr) {
        return false;
    }

    if (!args.ensureBackendTextureLoaded) {
        return false;
    }
    game::runtime::SharedBackendTextureCacheEntry* atlas =
        args.ensureBackendTextureLoaded(profile.spec.path, false);
    if (!atlas || !atlas->valid || atlas->width <= 0 || atlas->height <= 0 ||
        atlas->rgba.empty()) {
        return false;
    }

    bool applied = false;
    for (std::size_t bi = 0; bi < batches.size(); ++bi) {
        auto& batch = batches[bi];
        const std::size_t baseSubmeshIndex = resolveBatchBaseSubmeshIndex(batch, bi);
        if (baseSubmeshIndex >= profile.fireSubmeshMask.size() ||
            profile.fireSubmeshMask[baseSubmeshIndex] == 0u) {
            continue;
        }

        batch.sharedTemplate = nullptr;
        batch.textureKey = profile.spec.path;
        batch.textureCacheKey = profile.spec.path;
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
        batch.materialAtlasWidth = profile.spec.atlasWidth;
        batch.materialAtlasHeight = profile.spec.atlasHeight;
        batch.materialRect0U = 0.0f;
        batch.materialRect0V = 0.0f;
        batch.materialRect0W = 1.0f;
        batch.materialRect0H = 1.0f;
        batch.materialRect1U = 0.0f;
        batch.materialRect1V = 0.0f;
        batch.materialRect1W = 1.0f;
        batch.materialRect1H = 1.0f;
        batch.materialFlipbook0Cols = profile.spec.cols;
        batch.materialFlipbook0Rows = profile.spec.rows;
        batch.materialFlipbook0Frames = profile.spec.frames;
        batch.materialFlipbook0Fps = profile.spec.fps;
        batch.materialFlipbook1Cols = profile.uvShift.x;
        batch.materialFlipbook1Rows = profile.uvShift.y;
        batch.materialFlipbook1Frames = 1.0f;
        batch.materialFlipbook1Fps = 0.0f;
        applied = true;
    }

    if (applied) {
        auto& anchor = (*args.sharedTailFireAnchors)[args.unit->id];
        anchor.valid = true;
        anchor.meshCarrierActive = true;
    }
    return applied;
}

int resolveDefaultSkinNodeIndex(const game::runtime::render_model::MeshData* mesh) {
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

bool nodeUsesGpuFullSkinning(const game::runtime::render_model::MeshData* mesh,
                             int triNodeIndex,
                             bool preferFullGpuSkinning) {
    if (!preferFullGpuSkinning) return false;
    if (!mesh || triNodeIndex < 0) return false;
    const std::size_t nodeIndex = static_cast<std::size_t>(triNodeIndex);
    if (nodeIndex >= mesh->nodeSkin.size()) return false;
    const int skinIndex = mesh->nodeSkin[nodeIndex];
    if (skinIndex < 0 || static_cast<std::size_t>(skinIndex) >= mesh->skins.size()) return false;
    const auto& skin = mesh->skins[static_cast<std::size_t>(skinIndex)];
    return !skin.joints.empty() && skin.joints.size() <= kMaxGpuSkinMatrices;
}

bool backendPrefersFullGpuSkinning(const char* backendId) {
    return backendId && std::string_view(backendId) == "d3d12";
}

const FastTexturedMaterialTemplateCache* ensureFastTexturedMaterialTemplateCache(
    const game::runtime::render_model::MeshData* mesh,
    std::size_t baseBatchCount,
    bool characterInkingEnabled) {
    if (!mesh || baseBatchCount == 0u) return nullptr;

    auto& cache = fastTexturedMaterialTemplateCaches()[mesh];
    const bool cacheValid =
        cache.mesh == mesh &&
        cache.meshVertexCount == mesh->vertices.size() &&
        cache.meshIndexCount == mesh->indices.size() &&
        cache.baseBatchCount == baseBatchCount &&
        cache.characterInkingEnabled == characterInkingEnabled &&
        cache.materials.size() == baseBatchCount;
    if (cacheValid) return &cache;

    cache = {};
    cache.mesh = mesh;
    cache.meshVertexCount = mesh->vertices.size();
    cache.meshIndexCount = mesh->indices.size();
    cache.baseBatchCount = baseBatchCount;
    cache.characterInkingEnabled = characterInkingEnabled;
    cache.materials.resize(baseBatchCount);
    const std::string keyPrefix = makeIndexedBatchKeyPrefix(*mesh);

    for (std::size_t si = 0; si < baseBatchCount; ++si) {
        auto& material = cache.materials[si];
        if (si < mesh->submeshBaseTextures.size()) {
            const auto& tex = mesh->submeshBaseTextures[si];
            if (tex.hasPixels()) {
                material.textureKey = keyPrefix + "#submesh:" + std::to_string(si);
                material.textureCacheKey = buildWorldTextureCacheKey(
                    material.textureKey,
                    tex.width,
                    tex.height,
                    tex.wrapS,
                    tex.wrapT,
                    true);
                material.textureRgba = tex.rgba.data();
                material.textureWidth = tex.width;
                material.textureHeight = tex.height;
                material.textureWrapS = tex.wrapS;
                material.textureWrapT = tex.wrapT;
            }
        }
        if (!material.textureRgba || material.textureWidth <= 0 || material.textureHeight <= 0) {
            material.textureKey = "__fallback_white_1x1__";
            material.textureCacheKey = buildWorldTextureCacheKey(
                material.textureKey, 1, 1, kClampToEdge, kClampToEdge, true);
            material.textureRgba = kFallbackWhiteRgba;
            material.textureWidth = 1;
            material.textureHeight = 1;
            material.textureWrapS = kClampToEdge;
            material.textureWrapT = kClampToEdge;
        }

        if (si < mesh->submeshNormalTextures.size()) {
            const auto& normalTex = mesh->submeshNormalTextures[si];
            if (normalTex.hasPixels()) {
                material.normalTextureKey =
                    keyPrefix + "#submesh_normal:" + std::to_string(si);
                material.normalTextureCacheKey = buildWorldTextureCacheKey(
                    material.normalTextureKey,
                    normalTex.width,
                    normalTex.height,
                    normalTex.wrapS,
                    normalTex.wrapT,
                    false);
                material.normalTextureRgba = normalTex.rgba.data();
                material.normalTextureWidth = normalTex.width;
                material.normalTextureHeight = normalTex.height;
                material.normalTextureWrapS = normalTex.wrapS;
                material.normalTextureWrapT = normalTex.wrapT;
            }
        }
        if (si < mesh->submeshMetallicRoughnessTextures.size()) {
            const auto& metallicRoughnessTex = mesh->submeshMetallicRoughnessTextures[si];
            if (metallicRoughnessTex.hasPixels()) {
                material.metallicRoughnessTextureKey =
                    keyPrefix + "#submesh_mr:" + std::to_string(si);
                material.metallicRoughnessTextureCacheKey = buildWorldTextureCacheKey(
                    material.metallicRoughnessTextureKey,
                    metallicRoughnessTex.width,
                    metallicRoughnessTex.height,
                    metallicRoughnessTex.wrapS,
                    metallicRoughnessTex.wrapT,
                    false);
                material.metallicRoughnessTextureRgba = metallicRoughnessTex.rgba.data();
                material.metallicRoughnessTextureWidth = metallicRoughnessTex.width;
                material.metallicRoughnessTextureHeight = metallicRoughnessTex.height;
                material.metallicRoughnessTextureWrapS = metallicRoughnessTex.wrapS;
                material.metallicRoughnessTextureWrapT = metallicRoughnessTex.wrapT;
            }
        }
        if (si < mesh->submeshOcclusionTextures.size()) {
            const auto& occlusionTex = mesh->submeshOcclusionTextures[si];
            if (occlusionTex.hasPixels()) {
                material.occlusionTextureKey =
                    keyPrefix + "#submesh_occ:" + std::to_string(si);
                material.occlusionTextureCacheKey = buildWorldTextureCacheKey(
                    material.occlusionTextureKey,
                    occlusionTex.width,
                    occlusionTex.height,
                    occlusionTex.wrapS,
                    occlusionTex.wrapT,
                    false);
                material.occlusionTextureRgba = occlusionTex.rgba.data();
                material.occlusionTextureWidth = occlusionTex.width;
                material.occlusionTextureHeight = occlusionTex.height;
                material.occlusionTextureWrapS = occlusionTex.wrapS;
                material.occlusionTextureWrapT = occlusionTex.wrapT;
            }
        }
        if (si < mesh->submeshEmissiveTextures.size()) {
            const auto& emissiveTex = mesh->submeshEmissiveTextures[si];
            if (emissiveTex.hasPixels()) {
                material.emissiveTextureKey =
                    keyPrefix + "#submesh_emissive:" + std::to_string(si);
                material.emissiveTextureCacheKey = buildWorldTextureCacheKey(
                    material.emissiveTextureKey,
                    emissiveTex.width,
                    emissiveTex.height,
                    emissiveTex.wrapS,
                    emissiveTex.wrapT,
                    true);
                material.emissiveTextureRgba = emissiveTex.rgba.data();
                material.emissiveTextureWidth = emissiveTex.width;
                material.emissiveTextureHeight = emissiveTex.height;
                material.emissiveTextureWrapS = emissiveTex.wrapS;
                material.emissiveTextureWrapT = emissiveTex.wrapT;
            }
        }

        if (si < mesh->submeshAlphaMode.size()) {
            material.alphaMode = mesh->submeshAlphaMode[si];
        }
        if (si < mesh->submeshAlphaCutoff.size()) {
            material.alphaCutoff = mesh->submeshAlphaCutoff[si];
        }
        if (si < mesh->submeshNormalScale.size()) {
            material.normalScale = std::max(0.0f, mesh->submeshNormalScale[si]);
        }
        if (si < mesh->submeshMetallicFactor.size()) {
            material.metallicFactor =
                std::clamp(mesh->submeshMetallicFactor[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshRoughnessFactor.size()) {
            material.roughnessFactor =
                std::clamp(mesh->submeshRoughnessFactor[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshOcclusionStrength.size()) {
            material.occlusionStrength =
                std::clamp(mesh->submeshOcclusionStrength[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshEmissiveFactors.size()) {
            const glm::vec3& emissive = mesh->submeshEmissiveFactors[si];
            material.emissiveFactorR = std::max(0.0f, emissive.r);
            material.emissiveFactorG = std::max(0.0f, emissive.g);
            material.emissiveFactorB = std::max(0.0f, emissive.b);
        }
        material.characterInkingEnabled = characterInkingEnabled ? 1u : 0u;
        material.materialMode = 2u;
    }

    return &cache;
}

const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    const game::runtime::render_model::MeshData* mesh,
    const std::vector<int>& submeshNodeFallback,
    std::size_t baseBatchCount,
    bool preferFullGpuSkinning) {
    if (!mesh || baseBatchCount == 0u) return nullptr;

    auto& cache = fastTexturedMeshTemplateCaches()[mesh];
    const bool cacheValid =
        cache.mesh == mesh &&
        cache.meshVertexCount == mesh->vertices.size() &&
        cache.meshIndexCount == mesh->indices.size() &&
        cache.baseBatchCount == baseBatchCount &&
        cache.preferFullGpuSkinning == preferFullGpuSkinning &&
        cache.submeshNodeFallbackSnapshot == submeshNodeFallback &&
        !cache.batches.empty();
    if (cacheValid) return &cache;

    cache = {};
    cache.mesh = mesh;
    cache.meshVertexCount = mesh->vertices.size();
    cache.meshIndexCount = mesh->indices.size();
    cache.baseBatchCount = baseBatchCount;
    cache.preferFullGpuSkinning = preferFullGpuSkinning;
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
                newBatch.skinnedBatch =
                    nodeUsesGpuFullSkinning(mesh, triNodeIndex, preferFullGpuSkinning);
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

    std::vector<std::vector<std::size_t>> splitBatchCandidatesBySource(cache.batches.size());
    std::vector<std::vector<std::uint16_t>> splitJointPaletteByBatch(cache.batches.size());
    std::vector<std::uint8_t> fullSkinBatchBySource(cache.batches.size(), 0u);
    std::vector<std::size_t> splitBatchIndexByTriangle(triangleCount, 0u);
    std::vector<std::size_t> splitTriangleCountByBatch(cache.batches.size(), 0u);
    for (std::size_t bi = 0; bi < cache.batches.size(); ++bi) {
        auto& batch = cache.batches[bi];
        if (batch.triNodeIndex >= 0) {
            batch.skinnedBatch =
                nodeUsesGpuFullSkinning(mesh, batch.triNodeIndex, preferFullGpuSkinning);
        } else {
            batch.skinnedBatch = false;
        }
        fullSkinBatchBySource[bi] = batch.skinnedBatch ? 1u : 0u;
        splitBatchCandidatesBySource[bi].push_back(bi);
    }

    constexpr float kJointWeightEpsilon = 0.00001f;
    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        std::size_t sourceBatchIndex = batchIndexByTriangle[triIdx];
        if (sourceBatchIndex >= splitBatchCandidatesBySource.size()) sourceBatchIndex = 0u;

        if (fullSkinBatchBySource[sourceBatchIndex] != 0u) {
            ++splitTriangleCountByBatch[sourceBatchIndex];
            splitBatchIndexByTriangle[triIdx] = sourceBatchIndex;
            continue;
        }

        std::array<std::uint16_t, 12> triJoints{};
        std::size_t triJointCount = 0u;
        const std::size_t i = triIdx * 3u;
        if (i + 2u < mesh->indices.size()) {
            const std::uint32_t i0 = mesh->indices[i + 0u];
            const std::uint32_t i1 = mesh->indices[i + 1u];
            const std::uint32_t i2 = mesh->indices[i + 2u];
            const auto appendWeightedJoints = [&](const game::runtime::render_model::MeshVertex& v) {
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
        for (const std::size_t candidateBatchIndex :
             splitBatchCandidatesBySource[sourceBatchIndex]) {
            if (candidateBatchIndex >= splitJointPaletteByBatch.size()) continue;
            const auto& candidatePalette = splitJointPaletteByBatch[candidateBatchIndex];
            std::size_t addedJointCount = 0u;
            for (std::size_t ji = 0; ji < triJointCount; ++ji) {
                const bool exists =
                    std::find(candidatePalette.begin(), candidatePalette.end(), triJoints[ji]) !=
                    candidatePalette.end();
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
            splitBatch.skinnedBatch = false;
            splitBatch.geometryCacheKey = makeIndexedGeometryCacheKey(
                keyPrefix,
                sourceBatch.baseSubmeshIndex,
                cache.batches.size(),
                baseBatchCount);
            cache.batches.push_back(std::move(splitBatch));
            splitJointPaletteByBatch.emplace_back();
            splitTriangleCountByBatch.push_back(0u);
            fullSkinBatchBySource.push_back(0u);
            chosenBatchIndex = cache.batches.size() - 1u;
            splitBatchCandidatesBySource[sourceBatchIndex].push_back(chosenBatchIndex);
        }

        auto& chosenPalette = splitJointPaletteByBatch[chosenBatchIndex];
        for (std::size_t ji = 0; ji < triJointCount; ++ji) {
            const bool exists =
                std::find(chosenPalette.begin(), chosenPalette.end(), triJoints[ji]) !=
                chosenPalette.end();
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
        const bool useFullSkinning = batch.skinnedBatch;
        if (!useFullSkinning) {
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
                    const std::uint16_t next = static_cast<std::uint16_t>(jointRemap.size());
                    jointRemap.emplace(joints[ji], next);
                    batch.gpuJointPalette.push_back(joints[ji]);
                }
                if (jointPaletteOverflow) break;
            }
            if (jointPaletteOverflow) {
                jointRemap.clear();
                batch.gpuJointPalette.clear();
            }
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
            if (!useFullSkinning && !jointRemap.empty()) {
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
