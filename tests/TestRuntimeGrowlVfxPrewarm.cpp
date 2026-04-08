#include "game/runtime/startup/RuntimeGrowlVfxPrewarm.h"
#include "TestAuthoredVfxPrewarmHarness.h"

bool test_runtime_growl_vfx_prewarm_contract(std::string& outFail) {
    test_authored_vfx_prewarm_harness::RecordingBackend backend;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> cache;
    game::runtime::render_model::MeshData mesh =
        test_authored_vfx_prewarm_harness::makeTriangleMesh();

    const auto stats = game::runtime::growl_vfx_prewarm::prewarm(
        test_authored_vfx_prewarm_harness::makeArgs<game::runtime::growl_vfx_prewarm::Args>(
            backend,
            cache,
            mesh));

    if (stats.drawPasses == 0u || stats.bakedTextures == 0u || stats.warmedBatches == 0u) {
        outFail = "RuntimeGrowlVfxPrewarm should build growl batches, bake pass textures, and prewarm renderer batches.";
        return false;
    }

    if (stats.warmedBatches != backend.prewarmedTextureKeys.size()) {
        outFail = "RuntimeGrowlVfxPrewarm should prewarm one backend texture payload per generated growl batch.";
        return false;
    }

    if (backend.prewarmedTextureKeys.empty()) {
        outFail = "RuntimeGrowlVfxPrewarm should prewarm at least one growl texture payload key.";
        return false;
    }

    if (backend.prewarmedTextureCacheKeys.size() != stats.warmedBatches ||
        !test_authored_vfx_prewarm_harness::anyKeyHasPrefix(
            backend.prewarmedTextureCacheKeys,
            "__authored_vfx_")) {
        outFail = "RuntimeGrowlVfxPrewarm should prewarm stable growl texture cache keys for backend reuse.";
        return false;
    }

    if (cache.find("__authored_vfx_baked:growl_eid_1076:m:assets/textures/moves/growl/Texture3918.png") == cache.end()) {
        outFail = "RuntimeGrowlVfxPrewarm should populate baked growl texture entries in the shared backend texture cache.";
        return false;
    }
    if (cache.find("__authored_vfx_baked:growl_eid_1255:q:assets/textures/moves/growl/Texture3924.png") == cache.end()) {
        outFail = "RuntimeGrowlVfxPrewarm should populate quarter-shaded growl mesh texture entries in the shared backend texture cache.";
        return false;
    }
    if (cache.find("__authored_vfx_baked:growl_eid_1275:q:assets/textures/moves/growl/Texture3924.png") == cache.end()) {
        outFail = "RuntimeGrowlVfxPrewarm should populate the second sparkle growl mesh texture entry in the shared backend texture cache.";
        return false;
    }
    if (cache.find("__authored_vfx_baked:growl_eid_1284:q:assets/textures/moves/growl/Texture3930.png") == cache.end()) {
        outFail = "RuntimeGrowlVfxPrewarm should populate the glow billboard growl texture entry in the shared backend texture cache.";
        return false;
    }

    return true;
}
