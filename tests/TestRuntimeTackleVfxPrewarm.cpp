#include "game/runtime/startup/RuntimeTackleVfxPrewarm.h"
#include "TestAuthoredVfxPrewarmHarness.h"

bool test_runtime_tackle_vfx_prewarm_contract(std::string& outFail) {
    test_authored_vfx_prewarm_harness::RecordingBackend backend;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> cache;
    game::runtime::render_model::MeshData mesh =
        test_authored_vfx_prewarm_harness::makeTriangleMesh();

    const auto stats = game::runtime::tackle_vfx_prewarm::prewarm(
        test_authored_vfx_prewarm_harness::makeArgs<game::runtime::tackle_vfx_prewarm::Args>(
            backend,
            cache,
            mesh));

    if (stats.drawPasses == 0u || stats.bakedTextures == 0u || stats.warmedBatches == 0u) {
        outFail = "RuntimeTackleVfxPrewarm should build tackle batches, bake pass textures, and prewarm renderer batches.";
        return false;
    }

    if (stats.warmedBatches != backend.prewarmedTextureKeys.size()) {
        outFail = "RuntimeTackleVfxPrewarm should prewarm one backend texture payload per generated tackle batch.";
        return false;
    }

    if (!test_authored_vfx_prewarm_harness::anyKeyHasPrefix(
            backend.prewarmedTextureKeys,
            "authored_vfx:tackle_eid_")) {
        outFail = "RuntimeTackleVfxPrewarm should prewarm tackle batch texture keys rather than only raw source textures.";
        return false;
    }

    if (backend.prewarmedTextureCacheKeys.size() != stats.warmedBatches ||
        !test_authored_vfx_prewarm_harness::anyKeyHasPrefix(
            backend.prewarmedTextureCacheKeys,
            "__authored_vfx_")) {
        outFail = "RuntimeTackleVfxPrewarm should prewarm stable tackle texture cache keys for backend reuse.";
        return false;
    }

    if (!test_authored_vfx_prewarm_harness::cacheContainsPrefix(
            cache,
            "__authored_vfx_baked:tackle_eid_")) {
        outFail = "RuntimeTackleVfxPrewarm should populate baked tackle texture entries in the shared backend texture cache.";
        return false;
    }

    const auto containsKey = [](const std::vector<std::string>& keys, const std::string& needle) {
        return std::find(keys.begin(), keys.end(), needle) != keys.end();
    };
    if (!containsKey(
            backend.prewarmedTextureCacheKeys,
            "__authored_vfx_baked:tackle_eid_1225_texture4158_impact:q:assets/textures/moves/tackle/Texture4158.png") ||
        !containsKey(
            backend.prewarmedTextureCacheKeys,
            "__authored_vfx_baked:tackle_eid_1234_texture4159_impact:q:assets/textures/moves/tackle/Texture4159.png") ||
        !containsKey(
            backend.prewarmedTextureCacheKeys,
            "__authored_vfx_baked:tackle_eid_1243_texture4160_impact:q:assets/textures/moves/tackle/Texture4160.png")) {
        outFail = "RuntimeTackleVfxPrewarm should prewarm the first-impact tackle texture cache keys observed in gameplay.";
        return false;
    }

    if (!containsKey(
            backend.prewarmedGeometryKeys,
            "__authored_vfx_geom_streak_quad_v2__:502")) {
        outFail = "RuntimeTackleVfxPrewarm should prewarm the first-impact tackle streak geometry key observed in gameplay.";
        return false;
    }

    return true;
}
