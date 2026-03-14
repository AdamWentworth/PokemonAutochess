#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"

#include "engine/render/Model.h"
#include "game/runtime/render_prep/UnitVisuals.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

namespace game::runtime::shared_projected_unit_backend_mesh {

std::size_t prewarmProjectedUnitBackendMeshGeometryCache(
    IRenderBackend& renderer,
    const runtime::render_model::MeshData& mesh) {
    if (!renderer.supportsWorldIndexedMeshes()) return 0u;
    if (mesh.vertices.empty() || mesh.indices.size() < 3u) return 0u;

    const std::size_t baseBatchCount =
        std::max<std::size_t>(1u, mesh.submeshBaseTextures.size());
    const std::vector<int> submeshNodeFallback = support::buildSubmeshNodeFallback(mesh);
    const support::FastTexturedMeshTemplateCache* fastCache =
        support::ensureFastTexturedMeshTemplateCache(&mesh, submeshNodeFallback, baseBatchCount);
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
    const bool strictGltfParity = support::strictGltfParityEnabled();

    using SharedTailFireAnchor = game::runtime::shared_tail_fire_fallback::Anchor;

    bool drewModelMesh = false;
    if (meshForUnit) {
        std::uint32_t sharedRigidBatches = 0u;
        std::uint32_t gpuClipSkinBatches = 0u;
        std::uint32_t gpuClipPaletteBatches = 0u;
        std::uint32_t cpuRewriteBatches = 0u;
        std::uint32_t indexedBatchesQueued = 0u;
        using Clock = std::chrono::high_resolution_clock;
        const auto prepStart = Clock::now();
        auto& prep = support::preparedMeshState();
        if (!shared_projected_unit_backend_mesh_prep::prepareProjectedUnitBackendMesh(args, out, prep)) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            return out;
        }

        const runtime::render_model::MeshData* mesh = prep.mesh;
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
            (prep.scenePose && prep.scenePose->hasScenePose) ? prep.scenePose->nodeGlobals
                                                             : mesh->bindNodeGlobals;
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
        const support::FastTexturedMeshTemplateCache* fastCachePtr = nullptr;
        std::vector<std::uint8_t> batchUsesGpuClipPalette;
        if (useFastTexturedFullMeshPath && !modelIndexedBatchesPerSubmesh.empty()) {
            fastCachePtr = support::ensureFastTexturedMeshTemplateCache(
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
            batchUsesGpuClipPalette.assign(modelIndexedBatchesPerSubmesh.size(), 0u);

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

            auto& gpuSkinBatchStates = support::gpuSkinBatchStateEntries();
            gpuSkinBatchStates.clear();
            gpuSkinBatchStates.reserve(fastCache.batches.size());
            support::GpuSkinBatchStateEntry* lastGpuSkinBatchState = nullptr;
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
                        const std::size_t paletteCount = hasJointPalette
                            ? std::min(srcBatch.gpuJointPalette.size(), support::kMaxGpuSkinMatrices)
                            : 0u;
                        const auto matchesBatchStateKey =
                            [&](const support::UnitSkinMatrixKey& key) {
                                if (key.unitId != unit.id ||
                                    key.skinKey != skinCacheKey ||
                                    key.paletteSize != static_cast<std::uint32_t>(paletteCount)) {
                                    return false;
                                }
                                if (paletteCount == 0u) return true;
                                return std::memcmp(
                                           key.palette.data(),
                                           srcBatch.gpuJointPalette.data(),
                                           paletteCount * sizeof(std::uint16_t)) == 0;
                            };

                        support::GpuSkinBatchStateEntry* matchedEntry = nullptr;
                        if (lastGpuSkinBatchState &&
                            matchesBatchStateKey(lastGpuSkinBatchState->key)) {
                            matchedEntry = lastGpuSkinBatchState;
                        } else {
                            auto stateIt = std::find_if(
                                gpuSkinBatchStates.begin(),
                                gpuSkinBatchStates.end(),
                                [&](const support::GpuSkinBatchStateEntry& entry) {
                                    return matchesBatchStateKey(entry.key);
                                });
                            if (stateIt != gpuSkinBatchStates.end()) {
                                matchedEntry = &(*stateIt);
                            }
                        }
                        if (!matchedEntry) {
                            support::UnitSkinMatrixKey batchStateKey{};
                            batchStateKey.unitId = unit.id;
                            batchStateKey.skinKey = skinCacheKey;
                            batchStateKey.paletteSize =
                                static_cast<std::uint32_t>(paletteCount);
                            if (paletteCount > 0u) {
                                std::memcpy(batchStateKey.palette.data(),
                                            srcBatch.gpuJointPalette.data(),
                                            paletteCount * sizeof(std::uint16_t));
                            }
                            support::GpuSkinBatchStateEntry newEntry{};
                            newEntry.key = batchStateKey;
                            auto& sharedSkinMatrices = support::unitSkinMatrices()[batchStateKey];
                            if (transforms.configureGpuClipSkinningBatch(
                                    resolvedTriNodeIndex,
                                    hasJointPalette ? &srcBatch.gpuJointPalette : nullptr,
                                    newEntry.state.modelMatrix,
                                    sharedSkinMatrices,
                                    newEntry.state.skinMatrixCount)) {
                                newEntry.state.valid = true;
                                newEntry.state.sharedSkinMatrices =
                                    sharedSkinMatrices.empty() ? nullptr : sharedSkinMatrices.data();
                            }
                            gpuSkinBatchStates.emplace_back(std::move(newEntry));
                            matchedEntry = &gpuSkinBatchStates.back();
                        }
                        lastGpuSkinBatchState = matchedEntry;
                        if (matchedEntry->state.valid) {
                            dstBatch.gpuSkinning = 1u;
                            dstBatch.modelMatrix = matchedEntry->state.modelMatrix;
                            dstBatch.skinMatrixCount = matchedEntry->state.skinMatrixCount;
                            dstBatch.sharedSkinMatrices = matchedEntry->state.sharedSkinMatrices;
                            dstBatch.skinMatrices.clear();
                            if (hasJointPalette && bi < batchUsesGpuClipPalette.size()) {
                                batchUsesGpuClipPalette[bi] = 1u;
                            }
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
                    const bool canUseSharedNodeTransform =
                        srcBatch.gpuJointPalette.empty() &&
                        !srcBatch.gpuTemplateVertices.empty() &&
                        !srcBatch.indices.empty();
                    if (canUseSharedNodeTransform) {
                        // Non-skinned submeshes already have stable local-space template
                        // vertices. Let the backend apply the node/model transform instead
                        // of rewriting every vertex on the CPU.
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
                            const auto surface = transforms.resolveModelVertexSurface(
                                resolvedTriNodeIndex,
                                srcIndex,
                                srcVertex,
                                needsLitNormals,
                                needsTangents);
                            outVertex.x = surface.pos.x;
                            outVertex.y = surface.pos.y;
                            outVertex.z = surface.pos.z;
                            if (needsLitNormals) {
                                outVertex.nx = surface.normal.x;
                                outVertex.ny = surface.normal.y;
                                outVertex.nz = surface.normal.z;
                            }
                            if (needsTangents) {
                                outVertex.tx = surface.tangent.x;
                                outVertex.ty = surface.tangent.y;
                                outVertex.tz = surface.tangent.z;
                                outVertex.tw = surface.tangent.w;
                            }
                            dstBatch.vertices[vi] = outVertex;
                        }
                    }
                }
            }
            // Keep the indexed fast path inline here. Splitting it into another
            // translation unit caused a measurable Debug perf regression.
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
                batchUsesGpuClipPalette.clear();
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

        if (unit.alive &&
            !unit.fainting &&
            game::runtime::shared_tail_fire_mesh_playback::isTailFireMeshPlaybackSpecies(
                unit.name)) {
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
                if (support::tailFireDebugShouldLogAnchor(unit.id)) {
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
                if (support::tailFireDebugShouldLogAnchor(unit.id)) {
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

            auto& triNodeIndexByTriangle = support::triNodeIndexByTriangleScratch();
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
            for (std::size_t sampleIdx = 0; sampleIdx < effectiveUnitTriangleBudget;
                 ++sampleIdx) {
                std::size_t triIdx = sampleIdx;
                if (downsampleModelTriangles) {
                    triIdx = support::selectUniformTriangleIndex(
                        sampleIdx,
                        effectiveUnitTriangleBudget,
                        triangleCount);
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
                                                  const runtime::render_model::MeshVertex& srcVertex)
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
                        const auto surface = useGpuSkinning
                            ? shared_projected_unit_backend_mesh_transforms::ModelVertexSurfaceSample{}
                            : transforms.resolveModelVertexSurface(
                                  triNodeIndex,
                                  src,
                                  srcVertex,
                                  needsLitNormalsForSubmesh,
                                  needsTangentsForSubmesh);
                        const glm::vec3 pos = useGpuSkinning
                            ? transforms.resolveGpuSkinningInputPos(src, srcVertex)
                            : surface.pos;
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
                                outVertex.nx = surface.normal.x;
                                outVertex.ny = surface.normal.y;
                                outVertex.nz = surface.normal.z;
                            }
                            if (needsTangentsForSubmesh) {
                                outVertex.tx = surface.tangent.x;
                                outVertex.ty = surface.tangent.y;
                                outVertex.tz = surface.tangent.z;
                                outVertex.tw = surface.tangent.w;
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
                    const auto surface = useGpuSkinning
                        ? shared_projected_unit_backend_mesh_transforms::ModelVertexSurfaceSample{}
                        : transforms.resolveModelVertexSurface(
                              triNodeIndex,
                              src,
                              srcVertex,
                              needsLitNormalsForSubmesh,
                              needsTangentsForSubmesh);
                    const glm::vec3 pos = useGpuSkinning
                        ? transforms.resolveGpuSkinningInputPos(src, srcVertex)
                        : surface.pos;
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
                            outVertex.nx = surface.normal.x;
                            outVertex.ny = surface.normal.y;
                            outVertex.nz = surface.normal.z;
                        }
                        if (needsTangentsForSubmesh) {
                            outVertex.tx = surface.tangent.x;
                            outVertex.ty = surface.tangent.y;
                            outVertex.tz = surface.tangent.z;
                            outVertex.tw = surface.tangent.w;
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
                const auto s0 = transforms.resolveModelVertexSurface(
                    triNodeIndex,
                    i0,
                    v0,
                    needsLitNormalsForSubmesh,
                    needsTangentsForSubmesh);
                const auto s1 = transforms.resolveModelVertexSurface(
                    triNodeIndex,
                    i1,
                    v1,
                    needsLitNormalsForSubmesh,
                    needsTangentsForSubmesh);
                const auto s2 = transforms.resolveModelVertexSurface(
                    triNodeIndex,
                    i2,
                    v2,
                    needsLitNormalsForSubmesh,
                    needsTangentsForSubmesh);
                a = s0.pos;
                b = s1.pos;
                c = s2.pos;
                if (needsLitNormalsForSubmesh) {
                    n0 = s0.normal;
                    n1 = s1.normal;
                    n2 = s2.normal;
                } else {
                    n0 = v0.normal;
                    n1 = v1.normal;
                    n2 = v2.normal;
                }
                if (needsTangentsForSubmesh) {
                    t0 = s0.tangent;
                    t1 = s1.tangent;
                    t2 = s2.tangent;
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
                                         const runtime::render_model::MeshVertex& v) {
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
            (void)support::applyTailFireMeshFlipbookOverride(args, *mesh, modelIndexedBatchesPerSubmesh);
            for (std::size_t bi = 0; bi < modelIndexedBatchesPerSubmesh.size(); ++bi) {
                auto& batch = modelIndexedBatchesPerSubmesh[bi];
                if (!batch.hasGeometry()) continue;
                if (batch.gpuSkinning != 0u) {
                    ++gpuClipSkinBatches;
                    if (bi < batchUsesGpuClipPalette.size() &&
                        batchUsesGpuClipPalette[bi] != 0u) {
                        ++gpuClipPaletteBatches;
                    }
                } else if (batch.sharedVertices != nullptr &&
                           batch.sharedVertexCount > 0u &&
                           batch.sharedIndices != nullptr &&
                           batch.sharedIndexCount > 0u) {
                    ++sharedRigidBatches;
                } else {
                    ++cpuRewriteBatches;
                }
                worldIndexedBatches.push_back(std::move(batch));
                queuedIndexedBatch = true;
                ++indexedBatchesQueued;
            }
        }

        drewModelMesh = runtime::render_prep_units::didAccumulateModelGeometry(
            modelDepthCountBefore,
            modelDepthTris.size(),
            modelDepthWorldCountBefore,
            modelDepthWorldTris.size()) ||
            (world3DTriangles.size() > world3DTriangleCountBefore) ||
            queuedIndexedBatch;
        if (args.perfBreakdown) {
            args.perfBreakdown->geometryMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - geometryStart).count();
            args.perfBreakdown->sharedRigidBatches += sharedRigidBatches;
            args.perfBreakdown->gpuClipSkinBatches += gpuClipSkinBatches;
            args.perfBreakdown->gpuClipPaletteBatches += gpuClipPaletteBatches;
            args.perfBreakdown->cpuRewriteBatches += cpuRewriteBatches;
            args.perfBreakdown->indexedBatchesQueued += indexedBatchesQueued;
        }
    }
    out.drewModelMesh = drewModelMesh;
    return out;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh




