#include "game/runtime/startup/RuntimeScratchVfxPrewarm.h"
#include "TestAuthoredVfxPrewarmHarness.h"

bool test_runtime_scratch_vfx_prewarm_contract(std::string& outFail) {
    test_authored_vfx_prewarm_harness::RecordingBackend backend;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> cache;
    game::runtime::render_model::MeshData mesh =
        test_authored_vfx_prewarm_harness::makeTriangleMesh();

    const auto stats = game::runtime::scratch_vfx_prewarm::prewarm(
        test_authored_vfx_prewarm_harness::makeArgs<game::runtime::scratch_vfx_prewarm::Args>(
            backend,
            cache,
            mesh));

    if (stats.drawPasses == 0u || stats.bakedTextures == 0u || stats.warmedBatches == 0u) {
        outFail = "RuntimeScratchVfxPrewarm should build scratch batches, bake pass textures, and prewarm renderer batches.";
        return false;
    }

    if (stats.warmedBatches != backend.prewarmedTextureKeys.size()) {
        outFail = "RuntimeScratchVfxPrewarm should prewarm one backend texture payload per generated scratch batch.";
        return false;
    }

    if (stats.warmedBatches <= 10u) {
        outFail = "RuntimeScratchVfxPrewarm should cover the real multi-frame Scratch timeline, not only one synthetic all-groups snapshot.";
        return false;
    }

    if (backend.prewarmedGeometryKeys.empty() || backend.prewarmedInstanceCounts.empty()) {
        outFail = "RuntimeScratchVfxPrewarm should prewarm cached geometry and instanced batch capacity for first-use Scratch draws.";
        return false;
    }

    if (!test_authored_vfx_prewarm_harness::anyKeyHasPrefix(
            backend.prewarmedTextureKeys,
            "authored_vfx:scratch_eid_")) {
        outFail = "RuntimeScratchVfxPrewarm should prewarm scratch batch texture keys rather than only raw source textures.";
        return false;
    }

    if (backend.prewarmedTextureCacheKeys.size() != stats.warmedBatches ||
        !test_authored_vfx_prewarm_harness::anyKeyHasPrefix(
            backend.prewarmedTextureCacheKeys,
            "__authored_vfx_")) {
        outFail = "RuntimeScratchVfxPrewarm should prewarm stable scratch texture cache keys for backend reuse.";
        return false;
    }

    if (cache.find("__authored_vfx_baked:scratch_eid_1330_texture7566_glow:q:assets/textures/moves/scratch/Texture7566.png") == cache.end()) {
        outFail = "RuntimeScratchVfxPrewarm should populate the scratch red-glow baked texture entry.";
        return false;
    }
    if (cache.find("__authored_vfx_baked:scratch_eid_1382_texture7567_burst:q:assets/textures/moves/scratch/Texture7567.png") == cache.end()) {
        outFail = "RuntimeScratchVfxPrewarm should populate the scratch burst baked texture entry.";
        return false;
    }
    if (!test_authored_vfx_prewarm_harness::cacheContainsPrefix(
            cache,
            "__authored_vfx_baked:scratch_eid_")) {
        outFail = "RuntimeScratchVfxPrewarm should populate baked scratch texture entries in the shared backend texture cache.";
        return false;
    }

    const std::string marksBakedKey =
        "__authored_vfx_baked:scratch_texture7568_marks:m:assets/textures/moves/scratch/Texture7568.png";
    if (cache.find(marksBakedKey) == cache.end()) {
        outFail = "RuntimeScratchVfxPrewarm should share the identical Texture7568 claw-mark bake across all scratch mark passes.";
        return false;
    }

    if (!test_authored_vfx_prewarm_harness::anyKeyHasPrefix(
            backend.prewarmedTextureCacheKeys,
            marksBakedKey)) {
        outFail = "RuntimeScratchVfxPrewarm should prewarm the shared Texture7568 claw-mark texture cache key.";
        return false;
    }

    bool sawMergedClawInstanceBatch = false;
    for (std::size_t instanceCount : backend.prewarmedInstanceCounts) {
        if (instanceCount > 32u) {
            sawMergedClawInstanceBatch = true;
            break;
        }
    }
    if (!sawMergedClawInstanceBatch) {
        outFail = "RuntimeScratchVfxPrewarm should prewarm the merged claw-mark instance batches used by runtime Scratch submission.";
        return false;
    }

    return true;
}
