#include "game/runtime/shared/SharedCaptureModelBridge.h"

#include "game/GameWorld.h"
#include "game/runtime/shared/SharedCaptureD3d12FastPath.h"
#include "game/runtime/shared/SharedCapturePreparedMeshCache.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace game::runtime::shared_capture_model_bridge {
namespace {

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

bool appendSharedCaptureAttemptModels(const Args& args) {
    using WorldIndexedBatch = shared_world_batches::WorldIndexedBatch;
    using BackendPoseEval = game::runtime::shared_backend_pose::PoseEval;

    if (!args.gameWorld) return false;
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return false;
    if (!args.sharedCaptureAttemptCache) return false;
    if (!args.worldIndexedBatches) return false;
    if (!args.ensureBackendMeshLoaded) return false;
    if (!args.ensureBackendTextureLoaded) return false;
    if (!args.evaluateScenePoseForClipTime) return false;

    const bool isD3d12Backend =
        args.renderer &&
        args.renderer->backendId() &&
        (toLowerCopy(args.renderer->backendId()) == "d3d12");

    bool d3d12CapturePrewarmRequested = false;
    if (isD3d12Backend) {
        d3d12CapturePrewarmRequested = (args.gameWorld->getSelectedItem() == "pokeball");
        if (!d3d12CapturePrewarmRequested) {
            const auto ownedItems = args.gameWorld->listItems();
            for (const auto& [itemId, count] : ownedItems) {
                if (count > 0 && itemId == "pokeball") {
                    d3d12CapturePrewarmRequested = true;
                    break;
                }
            }
        }
    }

    if (args.sharedCaptureAttemptCache->snaps.empty()) {
        (void)args.sharedCaptureAttemptCache->refresh(args.gameWorld);
    }
    const auto& captureSnaps = args.sharedCaptureAttemptCache->snaps;
    if (captureSnaps.empty() && !d3d12CapturePrewarmRequested) return false;

    runtime::backend_model::MeshData* mesh =
        args.ensureBackendMeshLoaded("assets/models/pokeball.glb");

    if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
        static bool sLoggedSharedPokeballModelFailure = false;
        if (!sLoggedSharedPokeballModelFailure) {
            std::cout
                << "[Render][CaptureShared] pokeball.glb unavailable for shared capture model path; "
                   "2D fallback is suppressed by policy.\n";
            sLoggedSharedPokeballModelFailure = true;
        }
        return false;
    }
    if (mesh->animations.empty()) {
        static bool sLoggedSharedPokeballAnimMissing = false;
        if (!sLoggedSharedPokeballAnimMissing) {
            std::cout << "[Render][CaptureShared] pokeball cache has no animations; "
                         "shared clip playback is disabled (clear/rebuild cache if pokeball.glb was updated).\n";
            sLoggedSharedPokeballAnimMissing = true;
        }
    }

    constexpr bool kCapturePokeballTreatAsUntextured = true;
    constexpr bool kCapturePokeballEnableNodeChunkPath = false;

    if (isD3d12Backend && args.renderer && args.hasWorldViewProj) {
        const auto d3d12FastResult =
            game::runtime::shared_capture_d3d12_fast::tryAppend(
                *args.renderer,
                args.hasWorldViewProj,
                args.worldViewProj,
                args.drawableW,
                args.drawableH,
                *mesh,
                captureSnaps,
                d3d12CapturePrewarmRequested,
                kCapturePokeballTreatAsUntextured,
                kCapturePokeballEnableNodeChunkPath,
                [&](int animIndex, float animTimeSec) {
                    return args.evaluateScenePoseForClipTime(*mesh, animIndex, animTimeSec);
                });
        if (d3d12FastResult.handled) return d3d12FastResult.appendedAny;
    }

    bool appendedAny = false;
    const auto captureMeshPrep =
        game::runtime::shared_capture_mesh_cache::preparePokeballCaptureMeshCache(
            *mesh,
            captureSnaps.empty(),
            d3d12CapturePrewarmRequested,
            kCapturePokeballTreatAsUntextured,
            kCapturePokeballEnableNodeChunkPath,
            args.renderer);
    if (captureMeshPrep.earlyReturnAfterPrewarm) return false;
    if (!captureMeshPrep.validForRender || !captureMeshPrep.cache) return false;
    auto& sCaptureMeshCache = *captureMeshPrep.cache;
    const int captureAnimIndex = captureMeshPrep.captureAnimIndex;
    const float captureAnimDurationSec = captureMeshPrep.captureAnimDurationSec;
    const bool captureMeshLikelySkinned = captureMeshPrep.captureMeshLikelySkinned;

    for (const auto& snap : captureSnaps) {
        if (snap.timeLeftSec <= 0.0f) continue;

        const float baseScale =
            std::max(0.01f, mesh->modelScaleFactor) * std::max(0.02f, snap.ballScale);
        const glm::vec3 renderPos = snap.ballPos;
        const glm::mat4 modelM =
            runtime::shared_capture::buildBallModelMatrix(snap, baseScale);

        BackendPoseEval capturePoseEval;
        bool hasCaptureClipPose = false;
        std::vector<glm::mat4> captureNodeDelta;
        if (captureAnimIndex >= 0 && captureAnimDurationSec > 0.0f && snap.phase == 1) {
            const float clipAnimTimeSec =
                runtime::shared_capture::ballClipTimeSec(snap, captureAnimDurationSec);
            capturePoseEval = args.evaluateScenePoseForClipTime(*mesh, captureAnimIndex, clipAnimTimeSec);
            hasCaptureClipPose =
                capturePoseEval.hasScenePose &&
                !capturePoseEval.nodeGlobals.empty() &&
                !mesh->bindNodeGlobals.empty();
            if (hasCaptureClipPose) {
                const std::size_t nodeCount = std::min(
                    capturePoseEval.nodeGlobals.size(),
                    sCaptureMeshCache.bindNodeGlobalInv.size());
                captureNodeDelta.assign(nodeCount, glm::mat4(1.0f));
                for (std::size_t ni = 0; ni < nodeCount; ++ni) {
                    captureNodeDelta[ni] =
                        capturePoseEval.nodeGlobals[ni] * sCaptureMeshCache.bindNodeGlobalInv[ni];
                }
            }
        }

        const std::size_t batchCount = sCaptureMeshCache.submeshes.size();
        const bool useDirectD3d12CaptureDraw =
            args.renderer &&
            args.renderer->backendId() &&
            args.hasWorldViewProj &&
            (toLowerCopy(args.renderer->backendId()) == "d3d12");

        if (useDirectD3d12CaptureDraw) {
            const glm::mat4 viewProjM = glm::make_mat4(args.worldViewProj);
            if (!hasCaptureClipPose &&
                !sCaptureMeshCache.rigidCombinedVertices.empty() &&
                sCaptureMeshCache.rigidCombinedIndices.size() >= 3u) {
                const glm::mat4 rigidMvp = viewProjM * modelM;
                args.renderer->drawWorldIndexedMeshCached(
                    "assets/models/pokeball.glb#geomcombined",
                    sCaptureMeshCache.rigidCombinedVertices.data(),
                    sCaptureMeshCache.rigidCombinedVertices.size(),
                    sCaptureMeshCache.rigidCombinedIndices.data(),
                    sCaptureMeshCache.rigidCombinedIndices.size(),
                    glm::value_ptr(rigidMvp),
                    args.drawableW,
                    args.drawableH);
                appendedAny = true;
                continue;
            }
            std::vector<glm::mat4> nodeModelMatrices;
            if (hasCaptureClipPose && !captureNodeDelta.empty()) {
                nodeModelMatrices.resize(captureNodeDelta.size(), modelM);
                for (std::size_t ni = 0; ni < captureNodeDelta.size(); ++ni) {
                    nodeModelMatrices[ni] = modelM * captureNodeDelta[ni];
                }
            }

            for (int alphaPass = 0; alphaPass < 1; ++alphaPass) {
                const bool drawBlendPass = (alphaPass == 1);
                for (std::size_t si = 0; si < batchCount; ++si) {
                    auto& prepared = sCaptureMeshCache.submeshes[si];
                    if (prepared.vertices.empty() || prepared.indices.empty()) continue;
                    const bool isBlendSubmesh = (prepared.alphaMode == 2u);
                    if (isBlendSubmesh != drawBlendPass) continue;

                    IRenderBackend::WorldTextureData tex{};
                    std::string worldTexKey;
                    std::string worldGeomKey =
                        "assets/models/pokeball.glb#geomsubmesh:" + std::to_string(si);
                    bool hasRealTexture = false;
                    if (!kCapturePokeballTreatAsUntextured && si < mesh->submeshBaseTextures.size()) {
                        const auto& srcTex = mesh->submeshBaseTextures[si];
                        if (srcTex.hasPixels()) {
                            hasRealTexture = true;
                            worldTexKey = "assets/models/pokeball.glb#submesh:" + std::to_string(si);
                            tex.rgba = srcTex.rgba.data();
                            tex.width = srcTex.width;
                            tex.height = srcTex.height;
                            tex.wrapS = srcTex.wrapS;
                            tex.wrapT = srcTex.wrapT;
                            tex.key = worldTexKey.c_str();
                        }
                    }
                    tex.alphaMode = prepared.alphaMode;
                    tex.alphaCutoff = prepared.alphaCutoff;

                    if (prepared.scratchVertices.size() != prepared.vertices.size()) {
                        prepared.scratchVertices.resize(prepared.vertices.size());
                        for (std::size_t vi = 0; vi < prepared.vertices.size(); ++vi) {
                            const auto& src = prepared.vertices[vi];
                            auto& dst = prepared.scratchVertices[vi];
                            dst.x = src.bindPos.x;
                            dst.y = src.bindPos.y;
                            dst.z = src.bindPos.z;
                            dst.u = src.u;
                            dst.v = src.v;
                            dst.r = src.r;
                            dst.g = src.g;
                            dst.b = src.b;
                            dst.a = src.a;
                        }
                        prepared.scratchAtBindPose = true;
                    }
                    const bool canUseRigidSubmeshDirectDraw = !hasCaptureClipPose;
                    const bool canUseChunkDirectDraw =
                        kCapturePokeballEnableNodeChunkPath &&
                        !hasCaptureClipPose &&
                        !hasRealTexture &&
                        !captureMeshLikelySkinned &&
                        prepared.alphaMode != 2u &&
                        !prepared.nodeChunks.empty();
                    if (canUseChunkDirectDraw) {
                        for (std::size_t chunkIdx = 0; chunkIdx < prepared.nodeChunks.size(); ++chunkIdx) {
                            const auto& nodeChunk = prepared.nodeChunks[chunkIdx];
                            if (nodeChunk.compactVertices.empty() ||
                                nodeChunk.compactIndices.size() < 3u) {
                                continue;
                            }
                            glm::mat4 chunkModel = modelM;
                            if (!nodeModelMatrices.empty() &&
                                nodeChunk.nodeIndex >= 0 &&
                                static_cast<std::size_t>(nodeChunk.nodeIndex) < nodeModelMatrices.size()) {
                                chunkModel =
                                    nodeModelMatrices[static_cast<std::size_t>(nodeChunk.nodeIndex)];
                            }
                            const glm::mat4 chunkMvp = viewProjM * chunkModel;
                            if (hasRealTexture) {
                                args.renderer->drawWorldIndexedMeshTextured(
                                    nodeChunk.compactVertices.data(),
                                    nodeChunk.compactVertices.size(),
                                    nodeChunk.compactIndices.data(),
                                    nodeChunk.compactIndices.size(),
                                    &tex,
                                    glm::value_ptr(chunkMvp),
                                    args.drawableW,
                                    args.drawableH);
                            } else {
                                const std::string worldChunkGeomKey =
                                    "assets/models/pokeball.glb#geomsubmesh:" + std::to_string(si) +
                                    ":node:" + std::to_string(nodeChunk.nodeIndex) +
                                    ":chunk:" + std::to_string(chunkIdx);
                                args.renderer->drawWorldIndexedMeshCached(
                                    worldChunkGeomKey.c_str(),
                                    nodeChunk.compactVertices.data(),
                                    nodeChunk.compactVertices.size(),
                                    nodeChunk.compactIndices.data(),
                                    nodeChunk.compactIndices.size(),
                                    glm::value_ptr(chunkMvp),
                                    args.drawableW,
                                    args.drawableH);
                            }
                            appendedAny = true;
                        }
                    } else if (canUseRigidSubmeshDirectDraw) {
                        if (!prepared.scratchAtBindPose) {
                            for (std::size_t vi = 0; vi < prepared.vertices.size(); ++vi) {
                                const auto& src = prepared.vertices[vi];
                                auto& dst = prepared.scratchVertices[vi];
                                dst.x = src.bindPos.x;
                                dst.y = src.bindPos.y;
                                dst.z = src.bindPos.z;
                            }
                            prepared.scratchAtBindPose = true;
                        }
                        const glm::mat4 rigidMvp = viewProjM * modelM;
                        if (hasRealTexture) {
                            args.renderer->drawWorldIndexedMeshTextured(
                                prepared.scratchVertices.data(),
                                prepared.scratchVertices.size(),
                                prepared.indices.data(),
                                prepared.indices.size(),
                                &tex,
                                glm::value_ptr(rigidMvp),
                                args.drawableW,
                                args.drawableH);
                        } else {
                            args.renderer->drawWorldIndexedMeshCached(
                                worldGeomKey.c_str(),
                                prepared.scratchVertices.data(),
                                prepared.scratchVertices.size(),
                                prepared.indices.data(),
                                prepared.indices.size(),
                                glm::value_ptr(rigidMvp),
                                args.drawableW,
                                args.drawableH);
                        }
                        appendedAny = true;
                    } else {
                        for (std::size_t vi = 0; vi < prepared.vertices.size(); ++vi) {
                            const auto& src = prepared.vertices[vi];
                            glm::vec3 posedBindPos = src.bindPos;
                            if (hasCaptureClipPose &&
                                src.nodeIndex >= 0 &&
                                static_cast<std::size_t>(src.nodeIndex) < captureNodeDelta.size()) {
                                posedBindPos = glm::vec3(
                                    captureNodeDelta[static_cast<std::size_t>(src.nodeIndex)] *
                                    glm::vec4(src.bindPos, 1.0f));
                            }
                            const glm::vec3 pos = glm::vec3(modelM * glm::vec4(posedBindPos, 1.0f));
                            auto& dst = prepared.scratchVertices[vi];
                            dst.x = pos.x;
                            dst.y = pos.y;
                            dst.z = pos.z;
                        }
                        prepared.scratchAtBindPose = false;
                        if (hasRealTexture) {
                            args.renderer->drawWorldIndexedMeshTextured(
                                prepared.scratchVertices.data(),
                                prepared.scratchVertices.size(),
                                prepared.indices.data(),
                                prepared.indices.size(),
                                &tex,
                                args.worldViewProj,
                                args.drawableW,
                                args.drawableH);
                        } else {
                            args.renderer->drawWorldIndexedMesh(
                                prepared.scratchVertices.data(),
                                prepared.scratchVertices.size(),
                                prepared.indices.data(),
                                prepared.indices.size(),
                                args.worldViewProj,
                                args.drawableW,
                                args.drawableH);
                        }
                        appendedAny = true;
                    }
                }
            }
        } else {
            std::vector<WorldIndexedBatch> captureBatches(batchCount);
            for (std::size_t si = 0; si < batchCount; ++si) {
                auto& batch = captureBatches[si];
                const auto& prepared = sCaptureMeshCache.submeshes[si];
                batch.vertices.reserve(prepared.vertices.size());
                batch.indices.reserve(prepared.indices.size());
                batch.sortDepth = glm::dot(args.cameraWorldPos - renderPos, args.cameraWorldPos - renderPos);
                if (!kCapturePokeballTreatAsUntextured &&
                    si < mesh->submeshBaseTextures.size()) {
                    const auto& tex = mesh->submeshBaseTextures[si];
                    if (tex.hasPixels()) {
                        batch.textureKey = "assets/models/pokeball.glb#submesh:" + std::to_string(si);
                        batch.textureRgba = tex.rgba.data();
                        batch.textureWidth = tex.width;
                        batch.textureHeight = tex.height;
                        batch.textureWrapS = tex.wrapS;
                        batch.textureWrapT = tex.wrapT;
                    }
                }
                if ((!batch.textureRgba || batch.textureWidth <= 0 || batch.textureHeight <= 0)) {
                    if (SharedBackendTextureCacheEntry* white = args.ensureBackendTextureLoaded("")) {
                        batch.textureKey =
                            "assets/models/pokeball.glb#submesh:" + std::to_string(si) + ":white";
                        batch.textureRgba = white->rgba.data();
                        batch.textureWidth = white->width;
                        batch.textureHeight = white->height;
                        batch.textureWrapS = 33071;
                        batch.textureWrapT = 33071;
                    }
                }
                batch.alphaMode = prepared.alphaMode;
                batch.alphaCutoff = prepared.alphaCutoff;
            }
            for (std::size_t si = 0; si < batchCount; ++si) {
                auto& batch = captureBatches[si];
                const auto& prepared = sCaptureMeshCache.submeshes[si];
                if (prepared.vertices.empty() || prepared.indices.empty()) continue;
                if (!batch.textureRgba || batch.textureWidth <= 0 || batch.textureHeight <= 0) {
                    continue;
                }
                batch.indices = prepared.indices;
                batch.vertices.resize(prepared.vertices.size());
                for (std::size_t vi = 0; vi < prepared.vertices.size(); ++vi) {
                    const auto& src = prepared.vertices[vi];
                    glm::vec3 posedBindPos = src.bindPos;
                    if (hasCaptureClipPose &&
                        src.nodeIndex >= 0 &&
                        static_cast<std::size_t>(src.nodeIndex) < captureNodeDelta.size()) {
                        posedBindPos = glm::vec3(
                            captureNodeDelta[static_cast<std::size_t>(src.nodeIndex)] *
                            glm::vec4(src.bindPos, 1.0f));
                    }
                    const glm::vec3 pos = glm::vec3(modelM * glm::vec4(posedBindPos, 1.0f));
                    auto& dst = batch.vertices[vi];
                    dst.x = pos.x;
                    dst.y = pos.y;
                    dst.z = pos.z;
                    dst.u = src.u;
                    dst.v = src.v;
                    dst.r = src.r;
                    dst.g = src.g;
                    dst.b = src.b;
                    dst.a = src.a;
                }
            }

            for (auto& batch : captureBatches) {
                if (batch.vertices.empty() || batch.indices.empty()) continue;
                args.worldIndexedBatches->push_back(std::move(batch));
                appendedAny = true;
            }
        }
    }

    return appendedAny;
}

} // namespace game::runtime::shared_capture_model_bridge
