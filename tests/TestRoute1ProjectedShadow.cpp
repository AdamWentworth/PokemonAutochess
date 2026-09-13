#include "game/runtime/shared/scene/Route1ProjectedShadow.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <string>

bool test_route1_projected_shadow_cache_contract(std::string& outFail) {
    using namespace game::runtime;
    using namespace engine::render::backend;
    using I = IRenderBackend;

    // Two environments can use the same board registration, as the South
    // Entrance and its flat experiment do. Only their casters differ.
    published_environment_scene::PreparedScene scene;
    I::WorldMeshVertex vertices[] = {
        {.x = -500.0f, .y = 100.0f, .z = -500.0f},
        {.x = 500.0f, .y = 100.0f, .z = -500.0f},
        {.x = 0.0f, .y = 100.0f, .z = 500.0f}};
    const std::uint32_t indices[] = {0u, 1u, 2u};
    I::WorldSceneMaterial material;
    material.projectedShadowEnabled = 1u;
    material.sourceEnabledSwitchMask = WorldSceneSourceMaterialSwitchCastShadow;
    material.sourceMaterialFamily = WorldSceneSourceMaterialFamily::Ground;
    const auto geometry = shared_world_scene::ensureRigidGeometry(
        scene.registry, vertices, "shadow-fixture", vertices, 3u, indices, 3u);
    const auto surface = shared_world_scene::ensureMaterial(
        scene.registry, &material, material);
    const auto object = shared_world_scene::ensureRenderObject(
        scene.registry, geometry, surface,
        shared_world_scene::PipelineVariant::OpaqueLit, 0u);
    shared_world_scene::appendRigidInstance(
        scene.shadowFrame, object, {1u},
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
        1.0f, 1.0f, 1.0f, 1.0f, 0.0f);

    route1_projected_shadow::Atlas atlas;
    const std::vector<published_environment_scene::PreparedScene*> scenes{&scene};
    const std::array<float, 3> center{};
    if (!atlas.build(scenes, center, 64, 64, &outFail)) return false;
    if (atlas.stats().writtenPixelCount == 0u) {
        outFail = "Shadow cache fixture did not rasterize its caster.";
        return false;
    }
    const auto originalPixels = atlas.rgba();
    const auto originalKey = atlas.textureKey();
    const auto verifyAttachment = [&]() {
        atlas.attach(scenes);
        const auto& attached = scene.registry.materials.front();
        return attached.projectedShadowTextureKey == atlas.textureKey() &&
            attached.projectedShadowTextureCacheKey == atlas.textureKey() &&
            attached.projectedShadowTextureRgba == atlas.rgba().data() &&
            attached.projectedShadowMatrix == atlas.projection();
    };
    if (!verifyAttachment()) {
        outFail = "Shadow receiver did not receive the current atlas binding.";
        return false;
    }

    // Removing ground/grass casters must not retrieve the previous GPU image.
    if (!atlas.build(scenes, center, 64, 64,
            {.includeGroundCasters = false}, &outFail)) return false;
    const auto emptyKey = atlas.textureKey();
    if (atlas.stats().writtenPixelCount != 0u || atlas.rgba() == originalPixels ||
        emptyKey == originalKey || !verifyAttachment()) {
        outFail = "Changed casters at the same board center reused the old shadow texture.";
        return false;
    }

    // Returning to a scene must reuse its original image, not allocate a new
    // texture on every visit. Rebuilding the same Atlas also must clear depth.
    if (!atlas.build(scenes, center, 64, 64, &outFail)) return false;
    if (atlas.rgba() != originalPixels || atlas.textureKey() != originalKey ||
        !verifyAttachment()) {
        outFail = "Returning to an unchanged scene did not restore its shadow identity.";
        return false;
    }

    // Editing geometry in place needs a new image even without changing scene ID.
    for (auto& vertex : vertices) vertex.x += 400.0f;
    if (!atlas.build(scenes, center, 64, 64, &outFail)) return false;
    if (atlas.rgba() == originalPixels || atlas.textureKey() == originalKey ||
        atlas.textureKey() == emptyKey || !verifyAttachment()) {
        outFail = "Moving shadow geometry reused an obsolete atlas binding.";
        return false;
    }

    route1_projected_shadow::Atlas independentAtlas;
    if (!independentAtlas.build(scenes, center, 64, 64, &outFail)) return false;
    if (independentAtlas.rgba() != atlas.rgba() ||
        independentAtlas.textureKey() != atlas.textureKey()) {
        outFail = "Identical shadow content must share an identity across scene instances.";
        return false;
    }

    // Empty images with the same byte count but different shapes are distinct.
    if (!atlas.build(scenes, center, 32, 64,
            {.includeGroundCasters = false}, &outFail) ||
        !independentAtlas.build(scenes, center, 64, 32,
            {.includeGroundCasters = false}, &outFail)) return false;
    if (atlas.rgba() != independentAtlas.rgba() ||
        atlas.textureKey() == independentAtlas.textureKey()) {
        outFail = "Shadow texture identity must include image dimensions.";
        return false;
    }
    return true;
}
