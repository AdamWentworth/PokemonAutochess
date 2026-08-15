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
        static MeshData nativeIkMaterialMesh;
        nativeIkMaterialMesh = {};
        nativeIkMaterialMesh.assetCacheIdentity =
            "__native_ik_character_ao_strength_test__";
        nativeIkMaterialMesh.submeshMaterialModes = {
            game::runtime::render_model::kNativeIkCharacterMaterialMode};
        nativeIkMaterialMesh.submeshOcclusionStrength = {1.7f};
        const auto* nativeIk =
            support::ensureFastTexturedMaterialTemplateCache(
                &nativeIkMaterialMesh,
                1u,
                false,
                static_cast<int>(game::video::GraphicsQuality::Ultra));
        if (!nativeIk || nativeIk->materials.size() != 1u ||
            nativeIk->materials[0].occlusionStrength != 1.7f) {
            outFail =
                "Projected material templates must preserve native IkCharacter OcclusionStrength values above one.";
            return false;
        }

        static MeshData genericMaterialMesh;
        genericMaterialMesh = {};
        genericMaterialMesh.assetCacheIdentity =
            "__generic_ao_strength_clamp_test__";
        genericMaterialMesh.submeshMaterialModes = {2u};
        genericMaterialMesh.submeshOcclusionStrength = {1.7f};
        const auto* generic =
            support::ensureFastTexturedMaterialTemplateCache(
                &genericMaterialMesh,
                1u,
                false,
                static_cast<int>(game::video::GraphicsQuality::Ultra));
        if (!generic || generic->materials.size() != 1u ||
            generic->materials[0].occlusionStrength != 1.0f) {
            outFail =
                "Projected material templates must retain the generic PBR OcclusionStrength clamp.";
            return false;
        }
    }

    {
        static MeshData nativeSssMaterialMesh;
        nativeSssMaterialMesh = {};
        nativeSssMaterialMesh.assetCacheIdentity =
            "__native_sss_linear_mask_test__";
        nativeSssMaterialMesh.submeshMaterialModes = {
            game::runtime::render_model::kNativeSssMaterialMode};
        nativeSssMaterialMesh.submeshMaterialFlags = {
            game::runtime::render_model::kNativeSssSurfaceDefault};
        game::runtime::render_model::CachedTextureRgba mask;
        mask.width = 1;
        mask.height = 1;
        mask.rgba = {128u, 128u, 128u, 255u};
        nativeSssMaterialMesh.submeshEmissiveTextures = {mask};
        const auto* nativeSss =
            support::ensureFastTexturedMaterialTemplateCache(
                &nativeSssMaterialMesh,
                1u,
                false,
                static_cast<int>(game::video::GraphicsQuality::Ultra));
        if (!nativeSss || nativeSss->materials.size() != 1u ||
            nativeSss->materials[0].emissiveTextureSrgb != 0u ||
            nativeSss->materials[0].materialFlags !=
                game::runtime::render_model::kNativeSssSurfaceDefault) {
            outFail =
                "Projected native SSS templates must upload the scalar mask as linear data and retain the neutral surface qualifier.";
            return false;
        }
    }


    {
        static MeshData nativeFresnelMaterialMesh;
        nativeFresnelMaterialMesh = {};
        nativeFresnelMaterialMesh.assetCacheIdentity =
            "__native_fresnel_linear_layer_test__";
        nativeFresnelMaterialMesh.submeshMaterialModes = {
            game::runtime::render_model::
                kNativeFresnelEffectMaterialMode};
        game::runtime::render_model::CachedTextureRgba layer;
        layer.width = 1;
        layer.height = 1;
        layer.rgba = {128u, 64u, 32u, 255u};
        nativeFresnelMaterialMesh.submeshEmissiveTextures = {layer};
        const auto* nativeFresnel =
            support::ensureFastTexturedMaterialTemplateCache(
                &nativeFresnelMaterialMesh,
                1u,
                false,
                static_cast<int>(game::video::GraphicsQuality::Ultra));
        if (!nativeFresnel || nativeFresnel->materials.size() != 1u ||
            nativeFresnel->materials[0].emissiveTextureSrgb != 0u) {
            outFail =
                "Projected native FresnelEffect templates must upload BaseColorMap1 as linear source data.";
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
        if (batch.materialFlipbook1Frames >= 0.0f ||
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
        if (batch.materialFlipbook1Frames <= 0.0f ||
            batch.normalTextureRgba != nullptr ||
            batch.metallicRoughnessTextureRgba == nullptr ||
            batch.occlusionTextureRgba != nullptr ||
            batch.emissiveTextureRgba != nullptr) {
            outFail = "Projected mesh support should keep Medium softer and drop normal/occlusion/emissive maps while preserving base PBR response.";
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
        if (material.materialFlipbook1Frames <= 0.0f ||
            material.normalTextureRgba != nullptr ||
            material.metallicRoughnessTextureRgba != nullptr ||
            material.occlusionTextureRgba != nullptr ||
            material.emissiveTextureRgba != nullptr) {
            outFail = "Projected mesh support should mirror texture-detail and map reductions on world-scene materials.";
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
            nativeUnlitBatch.normalTextureRgba == nullptr ||
            nativeUnlitBatch.metallicRoughnessTextureRgba == nullptr) {
            outFail = "Graphics quality must preserve native layered-Unlit colors and source maps.";
            return false;
        }

        IRenderBackend::WorldSceneMaterial nativeEyeMaterial;
        nativeEyeMaterial.materialMode =
            game::runtime::render_model::kNativeEyeClearCoatMaterialMode;
        nativeEyeMaterial.materialFlipbook1Frames = 0.137f;
        nativeEyeMaterial.materialRect0U = 0.27f;
        nativeEyeMaterial.materialRect0V = 0.51f;
        nativeEyeMaterial.materialRect0W = 0.65f;
        nativeEyeMaterial.materialRect0H = 1.0f;
        nativeEyeMaterial.materialRect1U = 0.10f;
        nativeEyeMaterial.materialRect1V = 0.20f;
        nativeEyeMaterial.materialRect1W = 0.30f;
        nativeEyeMaterial.materialRect1H = 0.35f;
        nativeEyeMaterial.materialFlipbook0Cols = 0.80f;
        nativeEyeMaterial.materialFlipbook0Rows = 0.70f;
        nativeEyeMaterial.materialFlipbook0Frames = 0.60f;
        nativeEyeMaterial.normalTextureKey = "eye_normal";
        nativeEyeMaterial.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        support::applyGraphicsQualityToWorldSceneMaterial(
            nativeEyeMaterial,
            static_cast<int>(GraphicsQuality::Low));
        if (nativeEyeMaterial.materialFlipbook1Frames != 0.90f ||
            nativeEyeMaterial.materialRect0U != 0.27f ||
            nativeEyeMaterial.materialRect0V != 0.51f ||
            nativeEyeMaterial.materialRect0W != 0.65f ||
            nativeEyeMaterial.materialRect0H != 1.0f ||
            nativeEyeMaterial.materialRect1U != 0.10f ||
            nativeEyeMaterial.materialRect1V != 0.20f ||
            nativeEyeMaterial.materialRect1W != 0.30f ||
            nativeEyeMaterial.materialRect1H != 0.35f ||
            nativeEyeMaterial.materialFlipbook0Cols != 0.80f ||
            nativeEyeMaterial.materialFlipbook0Rows != 0.70f ||
            nativeEyeMaterial.materialFlipbook0Frames != 0.60f ||
            nativeEyeMaterial.normalTextureRgba == nullptr) {
            outFail = "Graphics quality must bias native EyeClearCoat sampling without changing its exact source parameters or maps.";
            return false;
        }

        game::runtime::shared_world_batches::WorldIndexedBatch
            nativeAnimatedEyeBatch;
        nativeAnimatedEyeBatch.materialMode =
            game::runtime::render_model::kNativeAnimatedEyeMaterialMode;
        nativeAnimatedEyeBatch.materialFlipbook1Frames = 0.0f;
        nativeAnimatedEyeBatch.emissiveTextureKey = "eye_atlas";
        nativeAnimatedEyeBatch.emissiveTextureCacheKey =
            "eye_atlas_cache";
        nativeAnimatedEyeBatch.emissiveTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        nativeAnimatedEyeBatch.emissiveTextureWidth = 128;
        nativeAnimatedEyeBatch.emissiveTextureHeight = 256;
        nativeAnimatedEyeBatch.emissiveFactorR = 1.0f;
        nativeAnimatedEyeBatch.emissiveFactorG = 1.0f;
        nativeAnimatedEyeBatch.emissiveFactorB = 1.0f;
        support::applyGraphicsQualityToBatchTemplate(
            nativeAnimatedEyeBatch,
            static_cast<int>(GraphicsQuality::Low));
        if (nativeAnimatedEyeBatch.emissiveTextureRgba == nullptr ||
            nativeAnimatedEyeBatch.emissiveTextureKey != "eye_atlas" ||
            nativeAnimatedEyeBatch.emissiveFactorR != 1.0f ||
            nativeAnimatedEyeBatch.materialFlipbook1Frames != 0.0f) {
            outFail =
                "Graphics quality must preserve animated native eye atlases at every tier.";
            return false;
        }

        game::runtime::shared_world_batches::WorldIndexedBatch
            nativeAnimatedClearCoatEyeBatch;
        nativeAnimatedClearCoatEyeBatch.materialMode =
            game::runtime::render_model::kNativeAnimatedEyeClearCoatMaterialMode;
        nativeAnimatedClearCoatEyeBatch.materialRect0U = 0.19f;
        nativeAnimatedClearCoatEyeBatch.materialRect1H = 0.42f;
        nativeAnimatedClearCoatEyeBatch.materialFlipbook0Frames = 0.73f;
        nativeAnimatedClearCoatEyeBatch.materialFlipbook1Frames = 0.19f;
        nativeAnimatedClearCoatEyeBatch.emissiveTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        support::applyGraphicsQualityToBatchTemplate(
            nativeAnimatedClearCoatEyeBatch,
            static_cast<int>(GraphicsQuality::Low));
        if (nativeAnimatedClearCoatEyeBatch.materialFlipbook1Frames != 0.90f ||
            nativeAnimatedClearCoatEyeBatch.materialRect0U != 0.19f ||
            nativeAnimatedClearCoatEyeBatch.materialRect1H != 0.42f ||
            nativeAnimatedClearCoatEyeBatch.materialFlipbook0Frames != 0.73f ||
            nativeAnimatedClearCoatEyeBatch.emissiveTextureRgba == nullptr) {
            outFail =
                "Graphics quality must preserve animated EyeClearCoat atlases and packed source parameters while applying its LOD bias.";
            return false;
        }

        game::runtime::shared_world_batches::WorldIndexedBatch gastlyBody;
        gastlyBody.materialMode =
            game::runtime::render_model::kNativeFacialOverlayMaterialMode;
        gastlyBody.materialFlags = 4.0f;
        gastlyBody.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        gastlyBody.metallicRoughnessTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        gastlyBody.occlusionTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        gastlyBody.emissiveTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        support::applyGraphicsQualityToBatchTemplate(
            gastlyBody,
            static_cast<int>(GraphicsQuality::Low));
        if (gastlyBody.normalTextureRgba != nullptr ||
            gastlyBody.metallicRoughnessTextureRgba == nullptr ||
            gastlyBody.occlusionTextureRgba != nullptr ||
            gastlyBody.emissiveTextureRgba != nullptr) {
            outFail =
                "Low quality must retain Gastly's source shadow-color payload while dropping optional IkCharacter detail maps.";
            return false;
        }

        IRenderBackend::WorldSceneMaterial gastlySceneBody;
        gastlySceneBody.materialMode =
            game::runtime::render_model::kNativeFacialOverlayMaterialMode;
        gastlySceneBody.materialFlags = 4.0f;
        gastlySceneBody.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        gastlySceneBody.metallicRoughnessTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        gastlySceneBody.occlusionTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        gastlySceneBody.emissiveTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        support::applyGraphicsQualityToWorldSceneMaterial(
            gastlySceneBody,
            static_cast<int>(GraphicsQuality::Low));
        if (gastlySceneBody.normalTextureRgba != nullptr ||
            gastlySceneBody.metallicRoughnessTextureRgba == nullptr ||
            gastlySceneBody.occlusionTextureRgba != nullptr ||
            gastlySceneBody.emissiveTextureRgba != nullptr) {
            outFail =
                "World-scene Low quality must preserve Gastly's source shadow-color payload.";
            return false;
        }

        game::runtime::shared_world_batches::WorldIndexedBatch
            nativeIkCharacterBody;
        nativeIkCharacterBody.materialMode =
            game::runtime::render_model::kNativeIkCharacterMaterialMode;
        nativeIkCharacterBody.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        nativeIkCharacterBody.metallicRoughnessTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        nativeIkCharacterBody.occlusionTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        nativeIkCharacterBody.emissiveTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        nativeIkCharacterBody.materialRect0W =
            game::runtime::render_model::
                kNativeIkCharacterSurfaceFeather;
        nativeIkCharacterBody.materialRect1U = 0.82f;
        nativeIkCharacterBody.materialRect1V = -0.35f;
        nativeIkCharacterBody.materialFlipbook0Cols = 0.12f;
        nativeIkCharacterBody.materialFlipbook0Rows = 0.21f;
        nativeIkCharacterBody.materialFlipbook1Cols = 0.31f;
        nativeIkCharacterBody.materialFlipbook1Rows = 0.83f;
        support::applyGraphicsQualityToBatchTemplate(
            nativeIkCharacterBody,
            static_cast<int>(GraphicsQuality::Low));
        if (nativeIkCharacterBody.materialFlipbook1Frames <= 0.0f ||
            nativeIkCharacterBody.normalTextureRgba != nullptr ||
            nativeIkCharacterBody.metallicRoughnessTextureRgba == nullptr ||
            nativeIkCharacterBody.occlusionTextureRgba == nullptr ||
            nativeIkCharacterBody.emissiveTextureRgba != nullptr ||
            nativeIkCharacterBody.materialRect1U != 0.82f ||
            nativeIkCharacterBody.materialRect1V != -0.35f ||
            nativeIkCharacterBody.materialFlipbook0Cols != 0.12f ||
            nativeIkCharacterBody.materialFlipbook0Rows != 0.21f ||
            nativeIkCharacterBody.materialFlipbook1Cols != 0.31f ||
            nativeIkCharacterBody.materialFlipbook1Rows != 0.83f ||
            nativeIkCharacterBody.materialRect0W !=
                game::runtime::render_model::
                    kNativeIkCharacterSurfaceFeather) {
            outFail =
                "Low quality must retain native IkCharacter shadow and surface controls while dropping optional normal/rim maps.";
            return false;
        }

        game::runtime::shared_world_batches::WorldIndexedBatch svEeveeFur;
        svEeveeFur.materialMode =
            game::runtime::render_model::kNativeSssMaterialMode;
        svEeveeFur.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svEeveeFur.metallicRoughnessTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svEeveeFur.occlusionTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svEeveeFur.emissiveTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        support::applyGraphicsQualityToBatchTemplate(
            svEeveeFur,
            static_cast<int>(GraphicsQuality::Low));
        if (svEeveeFur.materialFlipbook1Frames != 0.90f ||
            svEeveeFur.normalTextureRgba != nullptr ||
            svEeveeFur.metallicRoughnessTextureRgba == nullptr ||
            svEeveeFur.occlusionTextureRgba != nullptr ||
            svEeveeFur.emissiveTextureRgba != nullptr) {
            outFail =
                "Low quality must retain SV Eevee's fur atlas while reducing normal, AO, and SSS detail.";
            return false;
        }

        IRenderBackend::WorldSceneMaterial svEeveeFurUltra;
        svEeveeFurUltra.materialMode =
            game::runtime::render_model::kNativeSssMaterialMode;
        svEeveeFurUltra.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svEeveeFurUltra.metallicRoughnessTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svEeveeFurUltra.occlusionTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svEeveeFurUltra.emissiveTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        support::applyGraphicsQualityToWorldSceneMaterial(
            svEeveeFurUltra,
            static_cast<int>(GraphicsQuality::Ultra));
        if (svEeveeFurUltra.materialFlipbook1Frames != -0.40f ||
            svEeveeFurUltra.normalTextureRgba == nullptr ||
            svEeveeFurUltra.metallicRoughnessTextureRgba == nullptr ||
            svEeveeFurUltra.occlusionTextureRgba == nullptr ||
            svEeveeFurUltra.emissiveTextureRgba == nullptr) {
            outFail =
                "Ultra quality must retain SV Eevee's complete native fur/SSS material.";
            return false;
        }

        game::runtime::shared_world_batches::WorldIndexedBatch svFresnel;
        svFresnel.materialMode = game::runtime::render_model::
            kNativeFresnelEffectMaterialMode;
        svFresnel.normalTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svFresnel.metallicRoughnessTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svFresnel.occlusionTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svFresnel.emissiveTextureRgba =
            reinterpret_cast<const unsigned char*>(0x1);
        svFresnel.materialRect0U = 0.76f;
        svFresnel.materialFlipbook0Cols = 0.8f;
        svFresnel.materialFlipbook1Rows = 0.5f;
        support::applyGraphicsQualityToBatchTemplate(
            svFresnel,
            static_cast<int>(GraphicsQuality::Low));
        if (svFresnel.materialFlipbook1Frames != 0.90f ||
            svFresnel.normalTextureRgba == nullptr ||
            svFresnel.metallicRoughnessTextureRgba == nullptr ||
            svFresnel.occlusionTextureRgba == nullptr ||
            svFresnel.emissiveTextureRgba == nullptr ||
            svFresnel.materialRect0U != 0.76f ||
            svFresnel.materialFlipbook0Cols != 0.8f ||
            svFresnel.materialFlipbook1Rows != 0.5f) {
            outFail =
                "Low quality must retain FresnelEffect's two color layers and source controls while applying only its texture-detail LOD bias.";
            return false;
        }
    }

    return true;
}

