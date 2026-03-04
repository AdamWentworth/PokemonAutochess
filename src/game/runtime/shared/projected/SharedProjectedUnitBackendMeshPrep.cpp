#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"

#include "engine/core/Environment.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <deque>
#include <string>
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
    const game::runtime::backend_model::MeshData* mesh = nullptr;
    std::size_t meshVertexCount = 0u;
    std::size_t meshIndexCount = 0u;
    bool characterInkingEnabled = false;
    std::string keyPrefix;
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> batches;
};

thread_local std::deque<IndexedBatchTemplateCacheEntry> g_indexedBatchTemplateCache;

void applyIndexedBatchTemplateShallow(
    const game::runtime::shared_world_batches::WorldIndexedBatch& src,
    game::runtime::shared_world_batches::WorldIndexedBatch& dst) {
    dst.sharedTemplate = &src;

    dst.textureKey.clear();
    dst.ownedTextureRgba.clear();
    dst.textureRgba = src.textureRgba;
    dst.textureWidth = src.textureWidth;
    dst.textureHeight = src.textureHeight;
    dst.textureWrapS = src.textureWrapS;
    dst.textureWrapT = src.textureWrapT;

    dst.normalTextureKey.clear();
    dst.ownedNormalTextureRgba.clear();
    dst.normalTextureRgba = src.normalTextureRgba;
    dst.normalTextureWidth = src.normalTextureWidth;
    dst.normalTextureHeight = src.normalTextureHeight;
    dst.normalTextureWrapS = src.normalTextureWrapS;
    dst.normalTextureWrapT = src.normalTextureWrapT;

    dst.metallicRoughnessTextureKey.clear();
    dst.ownedMetallicRoughnessTextureRgba.clear();
    dst.metallicRoughnessTextureRgba = src.metallicRoughnessTextureRgba;
    dst.metallicRoughnessTextureWidth = src.metallicRoughnessTextureWidth;
    dst.metallicRoughnessTextureHeight = src.metallicRoughnessTextureHeight;
    dst.metallicRoughnessTextureWrapS = src.metallicRoughnessTextureWrapS;
    dst.metallicRoughnessTextureWrapT = src.metallicRoughnessTextureWrapT;

    dst.occlusionTextureKey.clear();
    dst.ownedOcclusionTextureRgba.clear();
    dst.occlusionTextureRgba = src.occlusionTextureRgba;
    dst.occlusionTextureWidth = src.occlusionTextureWidth;
    dst.occlusionTextureHeight = src.occlusionTextureHeight;
    dst.occlusionTextureWrapS = src.occlusionTextureWrapS;
    dst.occlusionTextureWrapT = src.occlusionTextureWrapT;

    dst.emissiveTextureKey.clear();
    dst.ownedEmissiveTextureRgba.clear();
    dst.emissiveTextureRgba = src.emissiveTextureRgba;
    dst.emissiveTextureWidth = src.emissiveTextureWidth;
    dst.emissiveTextureHeight = src.emissiveTextureHeight;
    dst.emissiveTextureWrapS = src.emissiveTextureWrapS;
    dst.emissiveTextureWrapT = src.emissiveTextureWrapT;

    dst.alphaMode = src.alphaMode;
    dst.blendMode = src.blendMode;
    dst.materialMode = src.materialMode;
    dst.alphaCutoff = src.alphaCutoff;
    dst.normalScale = src.normalScale;
    dst.metallicFactor = src.metallicFactor;
    dst.roughnessFactor = src.roughnessFactor;
    dst.occlusionStrength = src.occlusionStrength;
    dst.emissiveFactorR = src.emissiveFactorR;
    dst.emissiveFactorG = src.emissiveFactorG;
    dst.emissiveFactorB = src.emissiveFactorB;
    dst.characterInkingEnabled = src.characterInkingEnabled;
    dst.materialTimeSec = src.materialTimeSec;
    dst.materialFlags = src.materialFlags;
    dst.materialAtlasWidth = src.materialAtlasWidth;
    dst.materialAtlasHeight = src.materialAtlasHeight;
    dst.materialRect0U = src.materialRect0U;
    dst.materialRect0V = src.materialRect0V;
    dst.materialRect0W = src.materialRect0W;
    dst.materialRect0H = src.materialRect0H;
    dst.materialRect1U = src.materialRect1U;
    dst.materialRect1V = src.materialRect1V;
    dst.materialRect1W = src.materialRect1W;
    dst.materialRect1H = src.materialRect1H;
    dst.materialFlipbook0Cols = src.materialFlipbook0Cols;
    dst.materialFlipbook0Rows = src.materialFlipbook0Rows;
    dst.materialFlipbook0Frames = src.materialFlipbook0Frames;
    dst.materialFlipbook0Fps = src.materialFlipbook0Fps;
    dst.materialFlipbook1Cols = src.materialFlipbook1Cols;
    dst.materialFlipbook1Rows = src.materialFlipbook1Rows;
    dst.materialFlipbook1Frames = src.materialFlipbook1Frames;
    dst.materialFlipbook1Fps = src.materialFlipbook1Fps;
}

const std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>* getIndexedBatchTemplates(
    const game::runtime::backend_model::MeshData* mesh,
    const std::string& keyPrefix,
    bool characterInkingEnabled,
    std::size_t batchCount) {
    if (!mesh || batchCount == 0u) return nullptr;

    for (auto& entry : g_indexedBatchTemplateCache) {
        if (entry.mesh != mesh) continue;
        if (entry.meshVertexCount != mesh->vertices.size()) continue;
        if (entry.meshIndexCount != mesh->indices.size()) continue;
        if (entry.characterInkingEnabled != characterInkingEnabled) continue;
        if (entry.keyPrefix != keyPrefix) continue;
        if (entry.batches.size() != batchCount) continue;
        return &entry.batches;
    }

    IndexedBatchTemplateCacheEntry entry{};
    entry.mesh = mesh;
    entry.meshVertexCount = mesh->vertices.size();
    entry.meshIndexCount = mesh->indices.size();
    entry.characterInkingEnabled = characterInkingEnabled;
    entry.keyPrefix = keyPrefix;
    entry.batches.resize(batchCount);

    for (std::size_t si = 0; si < batchCount; ++si) {
        auto& batch = entry.batches[si];
        if (si < mesh->submeshBaseTextures.size()) {
            const auto& tex = mesh->submeshBaseTextures[si];
            if (tex.hasPixels()) {
                batch.textureKey = keyPrefix + "#submesh:" + std::to_string(si);
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
        batch.characterInkingEnabled = characterInkingEnabled ? 1u : 0u;
        // Material mode 2 routes model lighting to backend world shaders.
        batch.materialMode = 2u;
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

namespace game::runtime::shared_projected_unit_backend_mesh_prep {

bool prepareProjectedUnitBackendMesh(const Args& args, Result& out, PreparedState& prepared) {
    const auto& dataDb = *args.dataDb;
    const auto& unit = *args.unit;
    const auto* mesh = args.meshForUnit;
    auto scenePose = *args.scenePose;
    bool scenePoseReady = args.scenePoseReady;
    const auto& tint = *args.tint;

    auto& modelDepthTris = *args.modelDepthTris;
    auto& modelDepthWorldTris = *args.modelDepthWorldTris;
    auto& world3DTriangles = *args.world3DTriangles;
    auto& remainingModelTrianglesBudget = *args.remainingModelTrianglesBudget;

    prepared = PreparedState{};
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

    float resolvedScaleCorrection = std::max(0.05f, unit.modelScaleCorrection);
    if (!unit.model) {
        if (const PokemonStats* stats = dataDb.pokemon.getStats(unit.name)) {
            const std::string mode = toLowerCopy(stats->modelScaleMode);
            if (mode != "normalized") {
                const float importerScale = std::max(0.0f, mesh->modelScaleFactor);
                if (importerScale > 1e-6f) {
                    resolvedScaleCorrection = std::max(0.05f, 1.0f / importerScale);
                }
            }
        }
    }
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

    prepared.modelDepthCountBefore = modelDepthTris.size();
    prepared.modelDepthWorldCountBefore = modelDepthWorldTris.size();
    prepared.world3DTriangleCountBefore = world3DTriangles.size();

    prepared.submeshNodeFallback.clear();
    if (!mesh->submeshMeshIndex.empty()) {
        prepared.submeshNodeFallback.assign(mesh->submeshMeshIndex.size(), -1);
        for (std::size_t si = 0; si < mesh->submeshMeshIndex.size(); ++si) {
            const int meshIndex = mesh->submeshMeshIndex[si];
            if (meshIndex >= 0 &&
                static_cast<std::size_t>(meshIndex) < mesh->meshIndexToNode.size()) {
                prepared.submeshNodeFallback[si] =
                    mesh->meshIndexToNode[static_cast<std::size_t>(meshIndex)];
            }
        }
    }

    prepared.modelIndexedBatchesPerSubmesh.clear();
    prepared.modelIndexedVertexRemap.clear();
    if (prepared.useIndexedWorldModelPath) {
        const std::size_t batchCount =
            std::max<std::size_t>(1u, mesh->submeshBaseTextures.size());
        std::string unitModelPath;
        if (const PokemonStats* stats = dataDb.pokemon.getStats(unit.name)) {
            if (!stats->model.empty()) {
                unitModelPath = "assets/models/" + stats->model;
            }
        }
        const std::string keyPrefix =
            unitModelPath.empty() ? std::string("__runtime_model__") : unitModelPath;
        const auto* templateBatches =
            getIndexedBatchTemplates(mesh, keyPrefix, args.characterInkingEnabled, batchCount);
        const bool hasTemplateBatches =
            templateBatches && templateBatches->size() == batchCount;
        prepared.modelIndexedBatchesPerSubmesh.resize(batchCount);
        if (hasTemplateBatches) {
            for (std::size_t si = 0; si < batchCount; ++si) {
                applyIndexedBatchTemplateShallow(
                    (*templateBatches)[si], prepared.modelIndexedBatchesPerSubmesh[si]);
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
                remap.assign(mesh->vertices.size(), -1);
            }
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
            batch.sortDepth =
                glm::dot(args.cameraWorldPos - args.proxyCenter, args.cameraWorldPos - args.proxyCenter);
            const float* modelM = glm::value_ptr(prepared.modelM);
            std::copy(modelM, modelM + 16, batch.modelMatrix.begin());
            if (args.modelFadeAlpha < 0.999f) {
                batch.alphaMode = 2u;
                batch.blendMode = 0u;
                batch.alphaCutoff = 0.0f;
            }
        }
    }

    if (!scenePoseReady) {
        scenePose = game::runtime::shared_backend_pose::evaluateScenePose(*mesh, unit);
        scenePoseReady = true;
    }
    prepared.scenePose = std::move(scenePose);

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

} // namespace game::runtime::shared_projected_unit_backend_mesh_prep
