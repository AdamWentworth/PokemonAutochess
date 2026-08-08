#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/video/VideoPreferences.h"

#include <string_view>
#include <string>

bool test_shared_projected_unit_backend_mesh_support_contract(std::string& outFail) {
    namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
    using game::runtime::render_model::MeshData;

    if (support::selectUniformTriangleIndex(0u, 0u, 10u) != 0u ||
        support::selectUniformTriangleIndex(3u, 10u, 2u) != 1u) {
        outFail = "Projected mesh support should clamp uniform triangle sampling bounds.";
        return false;
    }

    {
        MeshData mesh;
        mesh.submeshMeshIndex = {2, -1, 0};
        mesh.meshIndexToNode = {7, 11, 19};
        const auto fallback = support::buildSubmeshNodeFallback(mesh);
        if (fallback.size() != 3u || fallback[0] != 19 || fallback[1] != -1 || fallback[2] != 7) {
            outFail = "Projected mesh support should derive submesh node fallback entries from mesh-to-node mappings.";
            return false;
        }
    }

    {
        const std::string unsplit =
            support::makeIndexedGeometryCacheKey("__mesh__", 4u, 4u, 8u);
        const std::string split =
            support::makeIndexedGeometryCacheKey("__mesh__", 4u, 11u, 8u);
        if (unsplit != "__mesh__#submesh_geom:4" ||
            split != "__mesh__#submesh_geom:4#split:11") {
            outFail = "Projected mesh support should format indexed geometry cache keys consistently.";
            return false;
        }
    }

    {
        game::runtime::shared_world_batches::WorldIndexedBatch batch;
        batch.geometryCacheKey = "__mesh__#submesh_geom:6#split:12";
        if (support::resolveBatchBaseSubmeshIndex(batch, 2u) != 6u) {
            outFail = "Projected mesh support should recover base submesh indices from geometry cache keys.";
            return false;
        }
    }

    {
        MeshData mesh;
        mesh.nodeSkin = {3, 3, -1};
        if (support::resolveDefaultSkinNodeIndex(&mesh) != 0) {
            outFail = "Projected mesh support should return the first node using a shared skin.";
            return false;
        }
        mesh.nodeSkin = {1, 2};
        if (support::resolveDefaultSkinNodeIndex(&mesh) != -1) {
            outFail = "Projected mesh support should reject meshes with multiple distinct node skins.";
            return false;
        }
    }

    {
        if (!support::backendUsesAuthoredTailFireMeshPlayback(nullptr) ||
            !support::backendUsesAuthoredTailFireMeshPlayback("opengl") ||
            !support::backendUsesAuthoredTailFireMeshPlayback("d3d12")) {
            outFail = "Projected mesh support should keep authored tail-fire mesh playback available on all backends.";
            return false;
        }
    }

    {
        if (!support::backendUsesGpuClipSkinningForUnit(nullptr, std::string_view("charmander")) ||
            !support::backendUsesGpuClipSkinningForUnit("opengl", std::string_view("charmander")) ||
            !support::backendUsesGpuClipSkinningForUnit("d3d12", std::string_view("charmander")) ||
            support::backendUsesGpuClipSkinningForUnit("d3d12", std::string_view("charmeleon")) ||
            !support::backendUsesGpuClipSkinningForUnit("d3d12", std::string_view("pikachu"))) {
            outFail = "Projected mesh support should keep native Charmander on GPU clip skinning and retain the D3D12 guard only for legacy tail-fire playback species.";
            return false;
        }
    }

    {
        using game::video::GraphicsQuality;
        if (support::textureDetailLodBiasForGraphicsQuality(static_cast<int>(GraphicsQuality::Low)) <=
                support::textureDetailLodBiasForGraphicsQuality(static_cast<int>(GraphicsQuality::Medium)) ||
            support::textureDetailLodBiasForGraphicsQuality(static_cast<int>(GraphicsQuality::Medium)) <=
                support::textureDetailLodBiasForGraphicsQuality(static_cast<int>(GraphicsQuality::High)) ||
            support::textureDetailLodBiasForGraphicsQuality(static_cast<int>(GraphicsQuality::High)) <=
                support::textureDetailLodBiasForGraphicsQuality(static_cast<int>(GraphicsQuality::Ultra))) {
            outFail = "Projected mesh support should make lower quality tiers progressively softer than Ultra.";
            return false;
        }
    }

    {
        using game::video::GraphicsQuality;
        static MeshData qualityCacheMesh;
        qualityCacheMesh.assetCacheIdentity =
            "__projected_material_quality_variant_test__";
        const auto* low = support::ensureFastTexturedMaterialTemplateCache(
            &qualityCacheMesh,
            1u,
            false,
            static_cast<int>(GraphicsQuality::Low));
        const auto* ultra = support::ensureFastTexturedMaterialTemplateCache(
            &qualityCacheMesh,
            1u,
            false,
            static_cast<int>(GraphicsQuality::Ultra));
        const auto* lowAgain =
            support::ensureFastTexturedMaterialTemplateCache(
                &qualityCacheMesh,
                1u,
                false,
                static_cast<int>(GraphicsQuality::Low));
        if (!low || !ultra || low == ultra || lowAgain != low ||
            low->graphicsQuality != static_cast<int>(GraphicsQuality::Low) ||
            ultra->graphicsQuality !=
                static_cast<int>(GraphicsQuality::Ultra)) {
            outFail =
                "Projected material templates should retain stable, distinct quality variants.";
            return false;
        }
    }

    {
        using game::video::GraphicsQuality;
        game::runtime::shared_world_batches::WorldIndexedBatch batch;
        batch.materialFlipbook1Frames = 1.0f;
        batch.normalTextureKey = "normal";
        batch.normalTextureCacheKey = "normal_cache";
        batch.normalTextureRgba = reinterpret_cast<const unsigned char*>(0x1);
        batch.normalTextureWidth = 64;
        batch.normalTextureHeight = 64;
        batch.metallicRoughnessTextureKey = "mr";
        batch.metallicRoughnessTextureCacheKey = "mr_cache";
        batch.metallicRoughnessTextureRgba = reinterpret_cast<const unsigned char*>(0x1);
        batch.metallicRoughnessTextureWidth = 64;
        batch.metallicRoughnessTextureHeight = 64;
        batch.occlusionTextureKey = "occ";
        batch.occlusionTextureCacheKey = "occ_cache";
        batch.occlusionTextureRgba = reinterpret_cast<const unsigned char*>(0x1);
        batch.occlusionTextureWidth = 64;
        batch.occlusionTextureHeight = 64;
        batch.emissiveTextureKey = "emi";
        batch.emissiveTextureCacheKey = "emi_cache";
        batch.emissiveTextureRgba = reinterpret_cast<const unsigned char*>(0x1);
        batch.emissiveTextureWidth = 64;
        batch.emissiveTextureHeight = 64;

        support::applyGraphicsQualityToBatchTemplate(
            batch,
            static_cast<int>(GraphicsQuality::Ultra));
        if (batch.textureDetailLodBias >= 0.0f ||
            batch.normalTextureRgba == nullptr ||
            batch.metallicRoughnessTextureRgba == nullptr ||
            batch.occlusionTextureRgba == nullptr ||
            batch.emissiveTextureRgba == nullptr) {
            outFail = "Projected mesh support should keep Ultra fully textured while biasing it toward sharper texture detail.";
            return false;
        }

        support::applyGraphicsQualityToBatchTemplate(
            batch,
            static_cast<int>(GraphicsQuality::Medium));
        if (batch.textureDetailLodBias <= 0.0f ||
            batch.normalTextureRgba == nullptr ||
            batch.metallicRoughnessTextureRgba == nullptr ||
            batch.occlusionTextureRgba == nullptr ||
            batch.emissiveTextureRgba == nullptr) {
            outFail = "Projected mesh support should make Medium softer without deleting authored material maps.";
            return false;
        }

        IRenderBackend::WorldSceneMaterial material;
        material.materialFlipbook1Frames = 1.0f;
        material.normalTextureKey = "normal";
        material.normalTextureCacheKey = "normal_cache";
        material.normalTextureRgba = reinterpret_cast<const unsigned char*>(0x1);
        material.normalTextureWidth = 64;
        material.normalTextureHeight = 64;
        material.metallicRoughnessTextureKey = "mr";
        material.metallicRoughnessTextureCacheKey = "mr_cache";
        material.metallicRoughnessTextureRgba = reinterpret_cast<const unsigned char*>(0x1);
        material.metallicRoughnessTextureWidth = 64;
        material.metallicRoughnessTextureHeight = 64;
        material.occlusionTextureKey = "occ";
        material.occlusionTextureCacheKey = "occ_cache";
        material.occlusionTextureRgba = reinterpret_cast<const unsigned char*>(0x1);
        material.occlusionTextureWidth = 64;
        material.occlusionTextureHeight = 64;
        material.emissiveTextureKey = "emi";
        material.emissiveTextureCacheKey = "emi_cache";
        material.emissiveTextureRgba = reinterpret_cast<const unsigned char*>(0x1);
        material.emissiveTextureWidth = 64;
        material.emissiveTextureHeight = 64;
        support::applyGraphicsQualityToWorldSceneMaterial(
            material,
            static_cast<int>(GraphicsQuality::Low));
        if (material.projectedShadowBias <= 0.0f ||
            material.normalTextureRgba == nullptr ||
            material.metallicRoughnessTextureRgba == nullptr ||
            material.occlusionTextureRgba == nullptr ||
            material.emissiveTextureRgba == nullptr) {
            outFail = "Projected mesh support should mirror texture-detail selection while preserving world-scene material maps.";
            return false;
        }

        game::runtime::shared_world_batches::WorldIndexedBatch nativeUnlitBatch;
        nativeUnlitBatch.materialMode =
            game::runtime::render_model::kNativeLayeredUnlitMaterialMode;
        nativeUnlitBatch.materialFlipbook1Frames = 0.28618f;
        nativeUnlitBatch.normalTextureKey = "displacement";
        nativeUnlitBatch.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        nativeUnlitBatch.metallicRoughnessTextureKey = "layer_mask";
        nativeUnlitBatch.metallicRoughnessTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        support::applyGraphicsQualityToBatchTemplate(
            nativeUnlitBatch,
            static_cast<int>(GraphicsQuality::Low));
        if (nativeUnlitBatch.materialFlipbook1Frames != 0.28618f ||
            nativeUnlitBatch.textureDetailLodBias <= 0.0f ||
            nativeUnlitBatch.normalTextureRgba == nullptr ||
            nativeUnlitBatch.metallicRoughnessTextureRgba == nullptr) {
            outFail = "Graphics quality must preserve native layered-Unlit colors and source maps.";
            return false;
        }

        IRenderBackend::WorldSceneMaterial nativeEyeMaterial;
        nativeEyeMaterial.materialMode =
            game::runtime::render_model::kNativeEyeClearCoatMaterialMode;
        nativeEyeMaterial.materialFlipbook1Frames = 0.137f;
        nativeEyeMaterial.normalTextureKey = "eye_normal";
        nativeEyeMaterial.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        support::applyGraphicsQualityToWorldSceneMaterial(
            nativeEyeMaterial,
            static_cast<int>(GraphicsQuality::Low));
        if (nativeEyeMaterial.materialFlipbook1Frames != 0.137f ||
            nativeEyeMaterial.projectedShadowBias <= 0.0f ||
            nativeEyeMaterial.normalTextureRgba == nullptr) {
            outFail = "Graphics quality must preserve native eye clear-coat parameters and maps.";
            return false;
        }
    }

    return true;
}

