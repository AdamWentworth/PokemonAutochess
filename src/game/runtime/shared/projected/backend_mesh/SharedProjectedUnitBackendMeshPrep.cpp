#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include "engine/core/Environment.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
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

constexpr unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};

struct IndexedBatchTemplateCacheEntry {
    const game::runtime::render_model::MeshData* mesh = nullptr;
    std::size_t meshVertexCount = 0u;
    std::size_t meshIndexCount = 0u;
    bool characterInkingEnabled = false;
    int graphicsQuality = 3;
    std::string keyPrefix;
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> batches;
};

thread_local std::deque<IndexedBatchTemplateCacheEntry> g_indexedBatchTemplateCache;
thread_local std::unordered_map<const game::runtime::render_model::MeshData*, std::string>
    g_indexedBatchKeyPrefixes;
struct SubmeshNodeFallbackCacheEntry {
    std::string assetCacheIdentitySnapshot;
    std::size_t submeshMeshIndexCount = 0u;
    std::size_t meshIndexToNodeCount = 0u;
    std::vector<int> fallback;
};
thread_local std::unordered_map<
    const game::runtime::render_model::MeshData*,
    SubmeshNodeFallbackCacheEntry>
    g_submeshNodeFallbackCache;

const std::string& getIndexedBatchKeyPrefix(
    const game::runtime::render_model::MeshData* mesh) {
    static const std::string fallback = "__runtime_model__";
    if (!mesh) return fallback;

    const std::string expected =
        mesh->assetCacheIdentity.empty()
            ? "__runtime_mesh__:" +
                  std::to_string(
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(
                              mesh)))
            : "__runtime_mesh_id__:" +
                  mesh->assetCacheIdentity;
    const auto found = g_indexedBatchKeyPrefixes.find(mesh);
    if (found != g_indexedBatchKeyPrefixes.end()) {
        if (found->second != expected) {
            found->second = expected;
        }
        return found->second;
    }

    const auto inserted = g_indexedBatchKeyPrefixes.emplace(
        mesh,
        expected);
    return inserted.first->second;
}

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

const std::vector<int>& getCachedSubmeshNodeFallback(
    const game::runtime::render_model::MeshData& mesh) {
    static const std::vector<int> empty;

    auto& entry = g_submeshNodeFallbackCache[&mesh];
    const bool cacheValid =
        entry.assetCacheIdentitySnapshot ==
            mesh.assetCacheIdentity &&
        entry.submeshMeshIndexCount == mesh.submeshMeshIndex.size() &&
        entry.meshIndexToNodeCount == mesh.meshIndexToNode.size();
    if (cacheValid) {
        return entry.fallback;
    }

    entry = {};
    entry.assetCacheIdentitySnapshot =
        mesh.assetCacheIdentity;
    entry.submeshMeshIndexCount = mesh.submeshMeshIndex.size();
    entry.meshIndexToNodeCount = mesh.meshIndexToNode.size();
    if (mesh.submeshMeshIndex.empty()) {
        return entry.fallback;
    }

    entry.fallback.assign(mesh.submeshMeshIndex.size(), -1);
    for (std::size_t si = 0; si < mesh.submeshMeshIndex.size(); ++si) {
        const int meshIndex = mesh.submeshMeshIndex[si];
        if (meshIndex >= 0 &&
            static_cast<std::size_t>(meshIndex) < mesh.meshIndexToNode.size()) {
            entry.fallback[si] = mesh.meshIndexToNode[static_cast<std::size_t>(meshIndex)];
        }
    }
    return entry.fallback.empty() ? empty : entry.fallback;
}

float sampleMaterialCurve(
    const game::runtime::render_model::MaterialAnimationCurve& curve,
    float timeSec,
    float fallback,
    bool periodicOffset,
    float sourceFrameRate) {
    const auto& keys = curve.keys;
    if (keys.empty()) return fallback;
    if (keys.size() == 1u || timeSec <= keys.front().timeSec) {
        return keys.front().value;
    }
    const auto upper = std::upper_bound(
        keys.begin(),
        keys.end(),
        timeSec,
        [](float time,
           const game::runtime::render_model::MaterialAnimationKey& key) {
            return time < key.timeSec;
        });
    if (upper == keys.end()) return keys.back().value;
    const auto lower = upper - 1;
    const float span = upper->timeSec - lower->timeSec;
    if (span <= 1e-6f) return upper->value;
    const float alpha = std::clamp(
        (timeSec - lower->timeSec) / span,
        0.0f,
        1.0f);
    float delta = upper->value - lower->value;
    // Game Freak's repeating UV curves store their wrap as adjacent source
    // keys (for example 0 -> 1 or 1 -> 0.033333 over one 60 Hz frame).
    // Numerically interpolating that reset travels through the atlas instead
    // of across its periodic seam, producing one visibly corrupt fire frame.
    // Follow the equivalent shortest periodic displacement only for those
    // one-source-frame reset spans; longer 1 -> 0 spans are the authored
    // scrolling motion and must remain linear.
    const float sourceFrameSeconds =
        sourceFrameRate > 0.0f ? 1.0f / sourceFrameRate : 0.0f;
    const bool sourceFrameReset =
        periodicOffset &&
        sourceFrameSeconds > 0.0f &&
        span <= sourceFrameSeconds * 1.01f &&
        std::abs(delta) > 0.5f;
    if (sourceFrameReset) {
        delta -= std::round(delta);
    }
    return lower->value + delta * alpha;
}

void applyExactContinuousMaterialAnimation(
    const game::runtime::render_model::MeshData& mesh,
    std::size_t submeshIndex,
    float materialTimeSec,
    game::runtime::shared_world_batches::WorldIndexedBatch& batch) {
    glm::vec4 value{0.0f};
    if (game::runtime::shared_projected_unit_backend_mesh_prep::detail::
            sampleContinuousMaterialAnimation(
                mesh,
                submeshIndex,
                game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset,
                materialTimeSec,
                value)) {
        // Layer color alpha is not consumed by this shader family, so its
        // legacy transport slots can carry the exact base UV scale while
        // rect0.zw carries the exact base UV offset.
        batch.materialFlipbook0Fps = value.x;
        batch.materialFlipbook1Fps = value.y;
        batch.materialRect0W = value.z;
        batch.materialRect0H = value.w;
    }
    if (game::runtime::shared_projected_unit_backend_mesh_prep::detail::
            sampleContinuousMaterialAnimation(
                mesh,
                submeshIndex,
                game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset3,
                materialTimeSec,
                value)) {
        batch.materialRect1U = value.x;
        batch.materialRect1V = value.y;
        batch.materialRect1W = value.z;
        batch.materialRect1H = value.w;
    }
}

void applyIndexedBatchTemplateShallow(
    const game::runtime::shared_world_batches::WorldIndexedBatch& src,
    game::runtime::shared_world_batches::WorldIndexedBatch& dst,
    const game::runtime::render_model::MeshData& mesh,
    std::size_t submeshIndex,
    float materialTimeSec) {
    if (src.materialMode == game::runtime::render_model::
                                kNativeLayeredUnlitMaterialMode) {
        // Dynamic native materials need the current shared material clock.
        // Detach only these small batches from the immutable material template;
        // regular model materials retain the shallow shared path.
        dst = src;
        dst.sharedTemplate = nullptr;
        dst.materialTimeSec = materialTimeSec;
        if (src.materialFlags > 1.5f) {
            applyExactContinuousMaterialAnimation(
                mesh,
                submeshIndex,
                materialTimeSec,
                dst);
        }
        return;
    }
    dst.sharedTemplate = &src;
    dst.geometryCacheKey = src.geometryCacheKey;
    dst.materialAlphaOverride = false;

    dst.textureKey.clear();
    dst.textureCacheKey.clear();
    dst.ownedTextureRgba.clear();
    dst.textureRgba = nullptr;
    dst.textureWidth = 0;
    dst.textureHeight = 0;

    dst.normalTextureKey.clear();
    dst.normalTextureCacheKey.clear();
    dst.ownedNormalTextureRgba.clear();
    dst.normalTextureRgba = nullptr;
    dst.normalTextureWidth = 0;
    dst.normalTextureHeight = 0;

    dst.metallicRoughnessTextureKey.clear();
    dst.metallicRoughnessTextureCacheKey.clear();
    dst.ownedMetallicRoughnessTextureRgba.clear();
    dst.metallicRoughnessTextureRgba = nullptr;
    dst.metallicRoughnessTextureWidth = 0;
    dst.metallicRoughnessTextureHeight = 0;

    dst.occlusionTextureKey.clear();
    dst.occlusionTextureCacheKey.clear();
    dst.ownedOcclusionTextureRgba.clear();
    dst.occlusionTextureRgba = nullptr;
    dst.occlusionTextureWidth = 0;
    dst.occlusionTextureHeight = 0;

    dst.emissiveTextureKey.clear();
    dst.emissiveTextureCacheKey.clear();
    dst.ownedEmissiveTextureRgba.clear();
    dst.emissiveTextureRgba = nullptr;
    dst.emissiveTextureWidth = 0;
    dst.emissiveTextureHeight = 0;
}

const std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>* getIndexedBatchTemplates(
    const game::runtime::render_model::MeshData* mesh,
    const std::string& keyPrefix,
    bool characterInkingEnabled,
    int graphicsQuality,
    std::size_t batchCount) {
    if (!mesh || batchCount == 0u) return nullptr;

    for (auto& entry : g_indexedBatchTemplateCache) {
        if (entry.mesh != mesh) continue;
        if (entry.meshVertexCount != mesh->vertices.size()) continue;
        if (entry.meshIndexCount != mesh->indices.size()) continue;
        if (entry.characterInkingEnabled != characterInkingEnabled) continue;
        if (entry.graphicsQuality != graphicsQuality) continue;
        if (entry.keyPrefix != keyPrefix) continue;
        if (entry.batches.size() != batchCount) continue;
        return &entry.batches;
    }

    IndexedBatchTemplateCacheEntry entry{};
    entry.mesh = mesh;
    entry.meshVertexCount = mesh->vertices.size();
    entry.meshIndexCount = mesh->indices.size();
    entry.characterInkingEnabled = characterInkingEnabled;
    entry.graphicsQuality = graphicsQuality;
    entry.keyPrefix = keyPrefix;
    entry.batches.resize(batchCount);

    for (std::size_t si = 0; si < batchCount; ++si) {
        auto& batch = entry.batches[si];
        batch.geometryCacheKey = keyPrefix + "#submesh_geom:" + std::to_string(si);
        if (si < mesh->submeshBaseTextures.size()) {
            const auto& tex = mesh->submeshBaseTextures[si];
            if (tex.hasPixels()) {
                batch.textureKey = keyPrefix + "#submesh:" + std::to_string(si);
                batch.textureCacheKey = buildWorldTextureCacheKey(
                    batch.textureKey,
                    tex.width,
                    tex.height,
                    tex.wrapS,
                    tex.wrapT,
                    true);
                batch.textureRgba = tex.rgba.data();
                batch.textureWidth = tex.width;
                batch.textureHeight = tex.height;
                batch.textureWrapS = tex.wrapS;
                batch.textureWrapT = tex.wrapT;
                if (si < mesh->submeshNormalTextures.size()) {
                    const auto& normalTex = mesh->submeshNormalTextures[si];
                    if (normalTex.hasPixels()) {
                        batch.normalTextureKey =
                            keyPrefix + "#submesh_normal:" + std::to_string(si);
                        batch.normalTextureCacheKey = buildWorldTextureCacheKey(
                            batch.normalTextureKey,
                            normalTex.width,
                            normalTex.height,
                            normalTex.wrapS,
                            normalTex.wrapT,
                            false);
                        batch.normalTextureRgba = normalTex.rgba.data();
                        batch.normalTextureWidth = normalTex.width;
                        batch.normalTextureHeight = normalTex.height;
                        batch.normalTextureWrapS = normalTex.wrapS;
                        batch.normalTextureWrapT = normalTex.wrapT;
                    }
                }
                if (si < mesh->submeshMetallicRoughnessTextures.size()) {
                    const auto& metallicRoughnessTex = mesh->submeshMetallicRoughnessTextures[si];
                    if (metallicRoughnessTex.hasPixels()) {
                        batch.metallicRoughnessTextureKey =
                            keyPrefix + "#submesh_mr:" + std::to_string(si);
                        batch.metallicRoughnessTextureCacheKey = buildWorldTextureCacheKey(
                            batch.metallicRoughnessTextureKey,
                            metallicRoughnessTex.width,
                            metallicRoughnessTex.height,
                            metallicRoughnessTex.wrapS,
                            metallicRoughnessTex.wrapT,
                            false);
                        batch.metallicRoughnessTextureRgba = metallicRoughnessTex.rgba.data();
                        batch.metallicRoughnessTextureWidth = metallicRoughnessTex.width;
                        batch.metallicRoughnessTextureHeight = metallicRoughnessTex.height;
                        batch.metallicRoughnessTextureWrapS = metallicRoughnessTex.wrapS;
                        batch.metallicRoughnessTextureWrapT = metallicRoughnessTex.wrapT;
                    }
                }
                if (si < mesh->submeshOcclusionTextures.size()) {
                    const auto& occlusionTex = mesh->submeshOcclusionTextures[si];
                    if (occlusionTex.hasPixels()) {
                        batch.occlusionTextureKey =
                            keyPrefix + "#submesh_occ:" + std::to_string(si);
                        batch.occlusionTextureCacheKey = buildWorldTextureCacheKey(
                            batch.occlusionTextureKey,
                            occlusionTex.width,
                            occlusionTex.height,
                            occlusionTex.wrapS,
                            occlusionTex.wrapT,
                            false);
                        batch.occlusionTextureRgba = occlusionTex.rgba.data();
                        batch.occlusionTextureWidth = occlusionTex.width;
                        batch.occlusionTextureHeight = occlusionTex.height;
                        batch.occlusionTextureWrapS = occlusionTex.wrapS;
                        batch.occlusionTextureWrapT = occlusionTex.wrapT;
                    }
                }
                if (si < mesh->submeshEmissiveTextures.size()) {
                    const auto& emissiveTex = mesh->submeshEmissiveTextures[si];
                    if (emissiveTex.hasPixels()) {
                        batch.emissiveTextureKey =
                            keyPrefix + "#submesh_emissive:" + std::to_string(si);
                        batch.emissiveTextureCacheKey = buildWorldTextureCacheKey(
                            batch.emissiveTextureKey,
                            emissiveTex.width,
                            emissiveTex.height,
                            emissiveTex.wrapS,
                            emissiveTex.wrapT,
                            true);
                        batch.emissiveTextureRgba = emissiveTex.rgba.data();
                        batch.emissiveTextureWidth = emissiveTex.width;
                        batch.emissiveTextureHeight = emissiveTex.height;
                        batch.emissiveTextureWrapS = emissiveTex.wrapS;
                        batch.emissiveTextureWrapT = emissiveTex.wrapT;
                    }
                }
            }
        }
        if (!batch.textureRgba || batch.textureWidth <= 0 || batch.textureHeight <= 0) {
            batch.textureKey = "__fallback_white_1x1__";
            batch.textureCacheKey = buildWorldTextureCacheKey(
                batch.textureKey, 1, 1, 33071, 33071, true);
            batch.textureRgba = kFallbackWhiteRgba;
            batch.textureWidth = 1;
            batch.textureHeight = 1;
            batch.textureWrapS = 33071; // GL_CLAMP_TO_EDGE
            batch.textureWrapT = 33071; // GL_CLAMP_TO_EDGE
        }
        if (si < mesh->submeshAlphaMode.size()) {
            batch.alphaMode = mesh->submeshAlphaMode[si];
        }
        if (si < mesh->submeshAlphaCutoff.size()) {
            batch.alphaCutoff = mesh->submeshAlphaCutoff[si];
        }
        if (si < mesh->submeshNormalScale.size()) {
            batch.normalScale = std::max(0.0f, mesh->submeshNormalScale[si]);
        }
        if (si < mesh->submeshMetallicFactor.size()) {
            batch.metallicFactor = std::clamp(mesh->submeshMetallicFactor[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshRoughnessFactor.size()) {
            batch.roughnessFactor = std::clamp(mesh->submeshRoughnessFactor[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshOcclusionStrength.size()) {
            batch.occlusionStrength = std::clamp(mesh->submeshOcclusionStrength[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshEmissiveFactors.size()) {
            const glm::vec3& e = mesh->submeshEmissiveFactors[si];
            batch.emissiveFactorR = std::max(0.0f, e.r);
            batch.emissiveFactorG = std::max(0.0f, e.g);
            batch.emissiveFactorB = std::max(0.0f, e.b);
        }
        batch.materialMode =
            si < mesh->submeshMaterialModes.size()
                ? mesh->submeshMaterialModes[si]
                : 2u;
        batch.materialFlags =
            si < mesh->submeshMaterialFlags.size()
                ? mesh->submeshMaterialFlags[si]
                : 0.0f;
        if (si < mesh->submeshMaterialParams0.size()) {
            const glm::vec4& value = mesh->submeshMaterialParams0[si];
            batch.materialRect0U = value.x;
            batch.materialRect0V = value.y;
            batch.materialRect0W = value.z;
            batch.materialRect0H = value.w;
            if (batch.materialMode == game::runtime::render_model::
                                          kNativeEyeClearCoatMaterialMode) {
                batch.materialFlipbook1Frames = value.x;
            }
        }
        if (si < mesh->submeshMaterialParams1.size()) {
            const glm::vec4& value = mesh->submeshMaterialParams1[si];
            batch.materialRect1U = value.x;
            batch.materialRect1V = value.y;
            batch.materialRect1W = value.z;
            batch.materialRect1H = value.w;
        }
        if (batch.materialMode == game::runtime::render_model::
                                      kNativeLayeredUnlitMaterialMode) {
            if (si < mesh->submeshMaterialParams2.size()) {
                const glm::vec4& value = mesh->submeshMaterialParams2[si];
                batch.materialFlipbook0Cols = value.x;
                batch.materialFlipbook0Rows = value.y;
                batch.materialFlipbook0Frames = value.z;
                batch.materialFlipbook0Fps = value.w;
            }
            if (si < mesh->submeshMaterialParams3.size()) {
                const glm::vec4& value = mesh->submeshMaterialParams3[si];
                batch.materialFlipbook1Cols = value.x;
                batch.materialFlipbook1Rows = value.y;
                batch.materialFlipbook1Frames = value.z;
                batch.materialFlipbook1Fps = value.w;
            }
        }
        batch.characterInkingEnabled =
            batch.materialMode == game::runtime::render_model::
                                      kNativeLayeredUnlitMaterialMode ||
                batch.materialMode == game::runtime::render_model::
                                          kNativeEyeClearCoatMaterialMode
                ? 0u
                : (characterInkingEnabled ? 1u : 0u);
        game::runtime::shared_projected_unit_backend_mesh_support::
            applyGraphicsQualityToBatchTemplate(batch, graphicsQuality);
    }

    g_indexedBatchTemplateCache.push_back(std::move(entry));
    return &g_indexedBatchTemplateCache.back().batches;
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
} // namespace

namespace game::runtime::
    shared_projected_unit_backend_mesh_prep::detail {

bool sampleContinuousMaterialAnimation(
    const runtime::render_model::MeshData& mesh,
    std::size_t submeshIndex,
    runtime::render_model::MaterialAnimationParameter parameter,
    float materialTimeSec,
    glm::vec4& outValue) {
    const auto track = std::find_if(
        mesh.continuousMaterialAnimations.begin(),
        mesh.continuousMaterialAnimations.end(),
        [&](const auto& candidate) {
            return candidate.submeshIndex == submeshIndex &&
                candidate.parameter == parameter &&
                candidate.durationSec > 0.0f;
        });
    if (track == mesh.continuousMaterialAnimations.end()) {
        return false;
    }
    float sampleTime = std::max(materialTimeSec, 0.0f);
    if (track->loop) {
        sampleTime = std::fmod(sampleTime, track->durationSec);
        if (sampleTime < 0.0f) sampleTime += track->durationSec;
    } else {
        sampleTime = std::min(sampleTime, track->durationSec);
    }
    outValue = track->defaultValue;
    for (std::size_t component = 0u; component < 4u; ++component) {
        outValue[static_cast<glm::length_t>(component)] =
            sampleMaterialCurve(
                track->components[component],
                sampleTime,
                outValue[static_cast<glm::length_t>(component)],
                component >= 2u,
                track->sourceFrameRate);
    }
    return true;
}

} // namespace game::runtime::
  // shared_projected_unit_backend_mesh_prep::detail

namespace game::runtime::shared_projected_unit_backend_mesh_prep {

void PreparedState::reset() {
    mesh = nullptr;
    triangleCount = 0u;
    effectiveUnitTriangleBudget = 0u;
    modelIndexedBatchCount = 0u;

    useIndexedWorldModelPath = false;
    fullIndexedMeshPath = false;
    useFastTexturedFullMeshPath = false;
    usePositionOnlyVertexPath = false;
    downsampleModelTriangles = false;

    resolvedScaleCorrection = 1.0f;
    fastTexturedAlpha = 1.0f;
    fastTexturedTint = glm::vec3(1.0f);
    lightDir = glm::vec3(0.0f, 1.0f, 0.0f);
    fallbackBase = glm::vec3(1.0f);

    modelM = glm::mat4(1.0f);
    modelMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    indexedBatchSortDepth = 0.0f;

    modelDepthCountBefore = 0u;
    modelDepthWorldCountBefore = 0u;
    world3DTriangleCountBefore = 0u;

    scenePose = nullptr;
    ownedScenePose.hasScenePose = false;
    ownedScenePose.hasClipPose = false;
    ownedScenePose.nodeLocals.clear();
    ownedScenePose.nodeGlobals.clear();

    submeshNodeFallback = nullptr;
    modelIndexedBatchesPerSubmesh.clear();
    modelIndexedVertexRemap.clear();
}

namespace {

bool prepareProjectedUnitBackendMeshCommon(const Args& args,
                                           Result& out,
                                           PreparedState& prepared,
                                           bool materializeIndexedBatches) {
    const auto& unit = *args.unit;
    const auto* mesh = args.meshForUnit;
    const auto* scenePose = args.scenePose;
    bool scenePoseReady = args.scenePoseReady;
    const auto& tint = *args.tint;

    auto& modelDepthTris = *args.modelDepthTris;
    auto& modelDepthWorldTris = *args.modelDepthWorldTris;
    auto& world3DTriangles = *args.world3DTriangles;
    auto& remainingModelTrianglesBudget = *args.remainingModelTrianglesBudget;

    prepared.reset();
    prepared.mesh = mesh;

    const std::size_t triangleCount = mesh->indices.size() / 3u;
    prepared.triangleCount = triangleCount;
    if (triangleCount == 0u) {
        out.skipUnit = true;
        return false;
    }

    const std::size_t maxTrianglesPerUnit = args.backendModelTriangleLimit();
    const float detailScale = std::clamp(args.unitSize / 70.0f, 0.45f, 1.0f);
    const std::size_t minTrianglesPerUnit =
        std::min<std::size_t>(1800u, maxTrianglesPerUnit);
    const std::size_t scaledBudget = static_cast<std::size_t>(std::clamp(
        static_cast<double>(maxTrianglesPerUnit) * static_cast<double>(detailScale),
        static_cast<double>(minTrianglesPerUnit),
        static_cast<double>(maxTrianglesPerUnit)));
    const std::size_t unitTriangleBudget =
        std::min(triangleCount, std::max(minTrianglesPerUnit, scaledBudget));

    prepared.useIndexedWorldModelPath =
        args.supportsWorldTriangles3D && args.supportsWorldIndexedMeshes;
    prepared.fullIndexedMeshPath =
        prepared.useIndexedWorldModelPath && args.backendModelFullMeshEnabled();
    prepared.useFastTexturedFullMeshPath =
        args.supportsWorldTriangles3D && prepared.useIndexedWorldModelPath &&
        args.backendModelFastTexturedPathEnabled() && prepared.fullIndexedMeshPath;

    std::size_t effectiveUnitTriangleBudget = unitTriangleBudget;
    if (prepared.fullIndexedMeshPath) {
        effectiveUnitTriangleBudget = triangleCount;
    } else {
        if (remainingModelTrianglesBudget > 0u) {
            effectiveUnitTriangleBudget =
                std::min(effectiveUnitTriangleBudget, remainingModelTrianglesBudget);
        } else {
            effectiveUnitTriangleBudget = std::min<std::size_t>(triangleCount, 384u);
        }
        if (effectiveUnitTriangleBudget == 0u) {
            effectiveUnitTriangleBudget = std::min<std::size_t>(triangleCount, 384u);
        }
        if (remainingModelTrianglesBudget >= effectiveUnitTriangleBudget) {
            remainingModelTrianglesBudget -= effectiveUnitTriangleBudget;
        } else {
            remainingModelTrianglesBudget = 0u;
        }
    }
    prepared.effectiveUnitTriangleBudget = effectiveUnitTriangleBudget;
    prepared.downsampleModelTriangles = effectiveUnitTriangleBudget < triangleCount;

    const float resolvedScaleCorrection = std::max(0.05f, unit.modelScaleCorrection);
    prepared.resolvedScaleCorrection = resolvedScaleCorrection;

    const float modelScale = std::max(0.01f, mesh->modelScaleFactor) * resolvedScaleCorrection *
                             std::max(0.05f, unit.speciesScale) * args.renderVisualScale *
                             args.renderCaptureScale * args.attackPulse;
    glm::vec3 renderPos = args.proxyCenter;
    const float minAllowedModelY = args.boardSurfaceY + 0.0025f;
    const float approxModelMinY = renderPos.y + mesh->boundsMin.y * modelScale;
    if (std::isfinite(approxModelMinY) && approxModelMinY < minAllowedModelY) {
        renderPos.y += (minAllowedModelY - approxModelMinY);
    }

    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(modelScale));
    const glm::mat4 rotationX =
        glm::rotate(glm::mat4(1.0f), glm::radians(args.animPitch), glm::vec3(1, 0, 0));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(args.animYaw), glm::vec3(0, 1, 0));
    const glm::mat4 rotationZ =
        glm::rotate(glm::mat4(1.0f), glm::radians(args.animRoll), glm::vec3(0, 0, 1));
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);
    prepared.modelM = translation * rotationY * rotationX * rotationZ * scale;
    const float* modelM = glm::value_ptr(prepared.modelM);
    std::copy(modelM, modelM + 16, prepared.modelMatrix.begin());

    prepared.modelDepthCountBefore = modelDepthTris.size();
    prepared.modelDepthWorldCountBefore = modelDepthWorldTris.size();
    prepared.world3DTriangleCountBefore = world3DTriangles.size();

    prepared.submeshNodeFallback = &getCachedSubmeshNodeFallback(*mesh);

    if (prepared.useIndexedWorldModelPath) {
        const std::size_t batchCount =
            std::max<std::size_t>(1u, mesh->submeshBaseTextures.size());
        prepared.modelIndexedBatchCount = batchCount;
        prepared.indexedBatchSortDepth =
            glm::dot(args.cameraWorldPos - args.proxyCenter, args.cameraWorldPos - args.proxyCenter);

        if (materializeIndexedBatches) {
            const auto* templateBatches =
                getIndexedBatchTemplates(
                    mesh,
                    getIndexedBatchKeyPrefix(mesh),
                    args.characterInkingEnabled,
                    args.graphicsQuality,
                    batchCount);
            const bool hasTemplateBatches =
                templateBatches && templateBatches->size() == batchCount;
            prepared.modelIndexedBatchesPerSubmesh.resize(batchCount);
            if (hasTemplateBatches) {
                for (std::size_t si = 0; si < batchCount; ++si) {
                    applyIndexedBatchTemplateShallow(
                        (*templateBatches)[si],
                        prepared.modelIndexedBatchesPerSubmesh[si],
                        *mesh,
                        si,
                        args.materialTimeSec);
                }
            } else {
                for (auto& batch : prepared.modelIndexedBatchesPerSubmesh) {
                    batch.sharedTemplate = nullptr;
                }
            }
            if (prepared.fullIndexedMeshPath &&
                !prepared.useFastTexturedFullMeshPath &&
                !mesh->vertices.empty()) {
                prepared.modelIndexedVertexRemap.resize(batchCount);
                for (auto& remap : prepared.modelIndexedVertexRemap) {
                    if (remap.size() != mesh->vertices.size()) {
                        remap.resize(mesh->vertices.size(), -1);
                    }
                    std::fill(remap.begin(), remap.end(), -1);
                }
            } else {
                prepared.modelIndexedVertexRemap.clear();
            }

            for (std::size_t si = 0; si < prepared.modelIndexedBatchesPerSubmesh.size(); ++si) {
                auto& batch = prepared.modelIndexedBatchesPerSubmesh[si];
                batch.vertices.clear();
                batch.indices.clear();
                batch.sharedVertices = nullptr;
                batch.sharedVertexCount = 0u;
                batch.sharedIndices = nullptr;
                batch.sharedIndexCount = 0u;
                batch.gpuSkinning = 0u;
                batch.skinMatrixCount = 0u;
                batch.sharedSkinMatrices = nullptr;
                batch.skinMatrices.clear();
                if (!prepared.useFastTexturedFullMeshPath) {
                    batch.vertices.reserve((effectiveUnitTriangleBudget * 3u) / batchCount + 64u);
                    batch.indices.reserve((effectiveUnitTriangleBudget * 3u) / batchCount + 64u);
                }
                batch.sortDepth = prepared.indexedBatchSortDepth;
                batch.modelMatrix = prepared.modelMatrix;
                if (args.modelFadeAlpha < 0.999f) {
                    batch.materialAlphaOverride = true;
                    batch.alphaMode = 2u;
                    batch.blendMode = 0u;
                    batch.alphaCutoff = 0.0f;
                } else {
                    batch.materialAlphaOverride = false;
                }
            }
        }
    }

    if (!scenePoseReady) {
        game::runtime::shared_backend_pose::evaluateScenePose(
            *mesh,
            unit,
            prepared.ownedScenePose);
        scenePose = &prepared.ownedScenePose;
        scenePoseReady = true;
    }
    prepared.scenePose = scenePose;

    // Do not gate fast position-only path on authored base textures.
    // Missing texture payloads are already normalized to fallback white in batch prep,
    // and this keeps GPU clip skinning coverage high instead of silently falling back
    // to CPU vertex/normal/tangent work.
    prepared.usePositionOnlyVertexPath = prepared.useFastTexturedFullMeshPath;

    prepared.lightDir = glm::normalize(glm::vec3(0.45f, 0.90f, 0.35f));
    prepared.fallbackBase = glm::vec3(
        std::clamp(tint.r * 0.85f + 0.10f, 0.0f, 1.0f),
        std::clamp(tint.g * 0.85f + 0.10f, 0.0f, 1.0f),
        std::clamp(tint.b * 0.85f + 0.10f, 0.0f, 1.0f));
    prepared.fastTexturedAlpha = std::clamp(args.modelFadeAlpha, 0.0f, 1.0f);
    if (strictGltfParityEnabled()) {
        // Parity mode: keep authored material colors untouched by gameplay tint.
        prepared.fastTexturedTint = glm::vec3(1.0f);
    } else {
        prepared.fastTexturedTint = glm::mix(
            glm::vec3(1.0f),
            args.captureTintColor,
            std::clamp(args.captureVisualTintStrength, 0.0f, 1.0f));
    }

    return true;
}

} // namespace

bool prepareProjectedUnitBackendMesh(const Args& args, Result& out, PreparedState& prepared) {
    return prepareProjectedUnitBackendMeshCommon(args, out, prepared, true);
}

bool prepareProjectedUnitBackendMeshWorldScene(const Args& args,
                                               Result& out,
                                               PreparedState& prepared) {
    return prepareProjectedUnitBackendMeshCommon(args, out, prepared, false);
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_prep

