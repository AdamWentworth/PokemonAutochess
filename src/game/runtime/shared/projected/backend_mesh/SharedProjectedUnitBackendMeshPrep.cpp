#include "engine/models/ModelAttachments.h"
#include "pokemon/models/MaterialCompatibility.h"
#include "game/render/materials/character/MaterialModes.h"
#include "engine/models/ModelTracks.h"
using engine::models::sampleMaterialCurve;
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"

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



using pokemon::models::materials::getIndexedBatchTemplates;
using pokemon::models::materials::applyIndexedBatchTemplateShallow;

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





} // namespace game::runtime::
  // shared_projected_unit_backend_mesh_prep::detail

namespace game::runtime::shared_projected_unit_backend_mesh_prep {

glm::mat4 supportedModelMatrix(const runtime::render_model::MeshData& mesh,
    glm::vec3 position, float modelScale, glm::vec3 rotationDegrees,
    float boardSurfaceY) {
    const float minAllowedY = boardSurfaceY + 0.0025f;
    const float minY = position.y + runtime::render_model::modelSupportContactY(mesh) * modelScale;
    if (std::isfinite(minY) && minY < minAllowedY)
        position.y += minAllowedY - minY;
    const auto scale = glm::scale(glm::mat4(1.0f), glm::vec3(modelScale));
    const auto x = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.x), glm::vec3(1, 0, 0));
    const auto y = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.y), glm::vec3(0, 1, 0));
    const auto z = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.z), glm::vec3(0, 0, 1));
    return glm::translate(glm::mat4(1.0f), position) * y * x * z * scale;
}

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
    submeshVisibilityAlpha.clear();
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
    prepared.submeshVisibilityAlpha.resize(
        mesh->submeshMeshIndex.size(),
        1.0f);
    for (std::size_t submeshIndex = 0u;
         submeshIndex < prepared.submeshVisibilityAlpha.size();
         ++submeshIndex) {
        prepared.submeshVisibilityAlpha[submeshIndex] =
            detail::sampleMeshVisibilityAlpha(
                *mesh,
                args.materialAnimationIndex,
                submeshIndex,
                args.materialAnimationTimeSec,
                args.materialTimeSec);
    }

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
    const bool hasFrontFacingOnlyMaterial = std::any_of(
        mesh->submeshMaterialFlags.begin(),
        mesh->submeshMaterialFlags.end(),
        [](float value) {
            const std::uint32_t flags = static_cast<std::uint32_t>(
                std::max(0l, std::lround(value)));
            return (flags &
                    game::runtime::render_model::
                        kNativeFrontFacingOnlyMaterialFlagBit) != 0u;
        });
    prepared.useFastTexturedFullMeshPath =
        args.supportsWorldTriangles3D && prepared.useIndexedWorldModelPath &&
        args.backendModelFastTexturedPathEnabled() && prepared.fullIndexedMeshPath &&
        !hasFrontFacingOnlyMaterial;

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
    prepared.modelM = supportedModelMatrix(*mesh, args.proxyCenter, modelScale,
        {args.animPitch, args.animYaw, args.animRoll}, args.boardSurfaceY);
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
                        args.materialTimeSec,
                        args.materialAnimationIndex,
                        args.materialAnimationTimeSec);
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

    if (materializeIndexedBatches && scenePose) {
        const float concealTongue =
            game::runtime::shared_backend_pose::isTongueSurfaceConcealed(
                *mesh,
                *scenePose)
                ? 1.0f
                : 0.0f;
        for (auto& batch : prepared.modelIndexedBatchesPerSubmesh) {
            if (batch.materialMode == game::runtime::render_model::
                                          kNativeFacialOverlayMaterialMode &&
                batch.materialFlags > 3.5f && batch.materialFlags < 4.5f) {
                // Mode 31 / subtype 4 owns params0.w. All three backends carry
                // it to the Gastly fragment path as the dynamic surface guard.
                batch.materialRect0H = concealTongue;
            }
        }
    }

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
    if (strictGltfParityEnabled() && !args.presentationTintEnabled) {
        // Preserve authored colors except for an explicit temporary presentation effect.
        prepared.fastTexturedTint = glm::vec3(1.0f);
    } else {
        prepared.fastTexturedTint = glm::mix(
            glm::vec3(1.0f),
            args.captureTintColor,
            std::clamp(args.captureVisualTintStrength, 0.0f, 1.0f));
    }

    if (args.supportsWorldIndexedMeshes && scenePose) {
        engine::models::appendAnimatedBillboards(*mesh, scenePose->nodeGlobals,
            prepared.modelM, args.materialAnimationIndex, args.materialAnimationTimeSec,
            args.cameraWorldPos, args.cameraWorldUp, args.cameraWorldFacing,
            args.modelFadeAlpha, *args.worldIndexedBatches);
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
