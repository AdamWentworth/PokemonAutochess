#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"

#include "engine/core/Environment.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>

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
        prepared.modelIndexedBatchesPerSubmesh.resize(batchCount);
        if (prepared.fullIndexedMeshPath && !mesh->vertices.empty()) {
            prepared.modelIndexedVertexRemap.resize(batchCount);
            for (auto& remap : prepared.modelIndexedVertexRemap) {
                remap.assign(mesh->vertices.size(), -1);
            }
        }

        std::string unitModelPath;
        if (const PokemonStats* stats = dataDb.pokemon.getStats(unit.name)) {
            if (!stats->model.empty()) {
                unitModelPath = "assets/models/" + stats->model;
            }
        }
        for (std::size_t si = 0; si < prepared.modelIndexedBatchesPerSubmesh.size(); ++si) {
            auto& batch = prepared.modelIndexedBatchesPerSubmesh[si];
            batch.vertices.reserve((effectiveUnitTriangleBudget * 3u) / batchCount + 64u);
            batch.indices.reserve((effectiveUnitTriangleBudget * 3u) / batchCount + 64u);
            batch.sortDepth =
                glm::dot(args.cameraWorldPos - args.proxyCenter, args.cameraWorldPos - args.proxyCenter);
            const float* modelM = glm::value_ptr(prepared.modelM);
            std::copy(modelM, modelM + 16, batch.modelMatrix.begin());
            if (si < mesh->submeshBaseTextures.size()) {
                const auto& tex = mesh->submeshBaseTextures[si];
                if (tex.hasPixels()) {
                    const std::string keyPrefix =
                        unitModelPath.empty() ? std::string("__runtime_model__") : unitModelPath;
                    batch.textureKey = keyPrefix + "#submesh:" + std::to_string(si);
                    batch.textureRgba = tex.rgba.data();
                    batch.textureWidth = tex.width;
                    batch.textureHeight = tex.height;
                    batch.textureWrapS = tex.wrapS;
                    batch.textureWrapT = tex.wrapT;
                    if (si < mesh->submeshNormalTextures.size()) {
                        const auto& normalTex = mesh->submeshNormalTextures[si];
                        if (normalTex.hasPixels()) {
                            batch.normalTextureKey = keyPrefix + "#submesh_normal:" + std::to_string(si);
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
                            batch.occlusionTextureKey = keyPrefix + "#submesh_occ:" + std::to_string(si);
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
                            batch.emissiveTextureKey = keyPrefix + "#submesh_emissive:" + std::to_string(si);
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
            batch.characterInkingEnabled = args.characterInkingEnabled ? 1u : 0u;
            // Material mode 2 routes model lighting to backend world shaders.
            batch.materialMode = 2u;
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

    bool allSubmeshesTextured = !mesh->submeshBaseTextures.empty();
    if (allSubmeshesTextured) {
        for (const auto& tex : mesh->submeshBaseTextures) {
            if (!tex.hasPixels()) {
                allSubmeshesTextured = false;
                break;
            }
        }
    }
    prepared.useFastTexturedFullMeshPath =
        args.supportsWorldTriangles3D && prepared.useIndexedWorldModelPath &&
        args.backendModelFastTexturedPathEnabled() && prepared.fullIndexedMeshPath;
    prepared.usePositionOnlyVertexPath =
        prepared.useFastTexturedFullMeshPath && allSubmeshesTextured;

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
