#include <string>

#include "game/runtime/shared/scene/SharedWorldScene.h"

bool test_shared_world_scene_contract(std::string& outFail) {
    using game::runtime::shared_world_batches::WorldIndexedBatch;
    using game::runtime::shared_world_scene::PipelineVariant;
    using game::runtime::shared_world_scene::WorldSceneRegistry;

    WorldSceneRegistry registry;
    IRenderBackend::WorldSceneFrame frame;

    static const IRenderBackend::WorldMeshVertex kVertices[3] = {
        {.x = 0.0f, .y = 0.0f, .z = 0.0f},
        {.x = 1.0f, .y = 0.0f, .z = 0.0f},
        {.x = 0.0f, .y = 1.0f, .z = 0.0f},
    };
    static const std::uint32_t kIndices[3] = {0u, 1u, 2u};
    static const unsigned char kTexture[4] = {255u, 255u, 255u, 255u};

    WorldIndexedBatch materialTemplate;
    materialTemplate.textureKey = "test_diffuse";
    materialTemplate.textureCacheKey = "test_cache";
    materialTemplate.textureRgba = kTexture;
    materialTemplate.textureWidth = 1;
    materialTemplate.textureHeight = 1;
    materialTemplate.materialMode = 2u;

    const auto geometryHandle =
        game::runtime::shared_world_scene::ensureRigidGeometry(
            registry,
            kVertices,
            "geom:test",
            kVertices,
            3u,
            kIndices,
            3u);
    const auto geometryHandle2 =
        game::runtime::shared_world_scene::ensureRigidGeometry(
            registry,
            kVertices,
            "geom:test",
            kVertices,
            3u,
            kIndices,
            3u);
    if (geometryHandle != geometryHandle2 || registry.geometries.size() != 1u) {
        outFail = "SharedWorldScene should reuse persistent geometry handles by identity.";
        return false;
    }

    const auto materialHandle =
        game::runtime::shared_world_scene::ensureMaterialFromBatchTemplate(
            registry,
            &materialTemplate,
            materialTemplate);
    const auto materialHandle2 =
        game::runtime::shared_world_scene::ensureMaterialFromBatchTemplate(
            registry,
            &materialTemplate,
            materialTemplate);
    if (materialHandle != materialHandle2 || registry.materials.size() != 1u) {
        outFail = "SharedWorldScene should reuse persistent material handles by identity.";
        return false;
    }

    IRenderBackend::WorldSceneMaterial directMaterialTemplate;
    directMaterialTemplate.textureKey = "direct_diffuse";
    directMaterialTemplate.textureCacheKey = "direct_cache";
    directMaterialTemplate.textureRgba = kTexture;
    directMaterialTemplate.textureWidth = 1;
    directMaterialTemplate.textureHeight = 1;
    directMaterialTemplate.materialMode = 2u;
    const auto directMaterialHandle =
        game::runtime::shared_world_scene::ensureMaterial(
            registry,
            &directMaterialTemplate,
            directMaterialTemplate);
    const auto directMaterialHandle2 =
        game::runtime::shared_world_scene::ensureMaterial(
            registry,
            &directMaterialTemplate,
            directMaterialTemplate);
    if (directMaterialHandle != directMaterialHandle2 || registry.materials.size() != 2u) {
        outFail = "SharedWorldScene should reuse direct scene-material handles by identity.";
        return false;
    }

    const auto objectHandle = game::runtime::shared_world_scene::ensureRenderObject(
        registry,
        geometryHandle,
        materialHandle,
        PipelineVariant::OpaqueLit,
        7u);
    const auto objectHandle2 = game::runtime::shared_world_scene::ensureRenderObject(
        registry,
        geometryHandle,
        materialHandle,
        PipelineVariant::OpaqueLit,
        7u);
    if (objectHandle != objectHandle2 || registry.renderObjects.size() != 1u) {
        outFail = "SharedWorldScene should reuse render-object handles for the same draw class.";
        return false;
    }
    const auto skinnedObjectHandle = game::runtime::shared_world_scene::ensureRenderObject(
        registry,
        geometryHandle,
        materialHandle,
        PipelineVariant::OpaqueLit,
        7u,
        true);
    if (skinnedObjectHandle == objectHandle || registry.renderObjects.size() != 2u) {
        outFail = "SharedWorldScene should keep skinned and rigid draw classes separate.";
        return false;
    }

    IRenderBackend::WorldSceneRenderInstanceHandle a{};
    a.id = 1u;
    IRenderBackend::WorldSceneRenderInstanceHandle b{};
    b.id = 2u;
    game::runtime::shared_world_scene::appendRigidInstance(
        frame,
        objectHandle,
        a,
        {},
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        3.0f);
    game::runtime::shared_world_scene::appendRigidInstance(
        frame,
        objectHandle,
        b,
        {},
        0.5f,
        0.6f,
        0.7f,
        0.8f,
        5.0f);

    if (frame.drawClasses.size() != 1u || frame.drawClasses.front().instances.size() != 2u) {
        outFail = "SharedWorldScene should group rigid instances by render-object draw class.";
        return false;
    }

    IRenderBackend::WorldSceneRenderInstanceHandle c{};
    c.id = 3u;
    static const float kSkinMatrices[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    game::runtime::shared_world_scene::appendSkinnedInstance(
        frame,
        skinnedObjectHandle,
        c,
        {},
        1.0f,
        0.9f,
        0.8f,
        0.7f,
        7.0f,
        0u,
        1u,
        kSkinMatrices);
    if (frame.drawClasses.size() != 2u ||
        frame.drawClasses.back().instances.size() != 1u ||
        frame.drawClasses.back().visibleSkeletons != 1u ||
        frame.visibleSkeletons != 1u ||
        frame.paletteUploadBytes != sizeof(kSkinMatrices)) {
        outFail = "SharedWorldScene should track skinned-instance counters on the fast scene frame.";
        return false;
    }

    frame.clear();
    game::runtime::shared_world_scene::appendRigidInstance(
        frame,
        objectHandle,
        a,
        {},
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        2.0f);
    if (frame.drawClasses.size() != 1u || frame.drawClasses.front().instances.size() != 1u) {
        outFail = "SharedWorldScene should rebuild draw-class lookup cleanly after frame reset.";
        return false;
    }

    game::runtime::shared_world_scene::resetWorldSceneRegistry(registry);
    if (!registry.geometries.empty() ||
        !registry.materials.empty() ||
        !registry.renderObjects.empty() ||
        registry.generation == 0u) {
        outFail = "SharedWorldScene should clear persistent state and preserve a valid generation on reset.";
        return false;
    }

    return true;
}
