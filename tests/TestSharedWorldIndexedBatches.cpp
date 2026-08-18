#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/projected/core/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleSubmit.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

struct RecordingBackend final : public IRenderBackend {
    struct DrawCall {
        std::string key;
        std::uint8_t alphaMode = 0u;
        bool rgbaNonNull = false;
        int width = 0;
        int height = 0;
        float clipSpaceDepthBias = 0.0f;
        std::size_t vertexCount = 0u;
        std::size_t indexCount = 0u;
        std::size_t instanceCount = 0u;
        bool instanced = false;
    };

    std::vector<DrawCall> calls;
    WorldIndexedSubmissionStats submissionStats{};
    bool sawSubmissionStats = false;

    const char* backendId() const override { return "test"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}
    void recordWorldIndexedSubmissionStats(const WorldIndexedSubmissionStats& stats) override {
        submissionStats.opaqueDraws += stats.opaqueDraws;
        submissionStats.blendDraws += stats.blendDraws;
        submissionStats.cachedDraws += stats.cachedDraws;
        submissionStats.dynamicDraws += stats.dynamicDraws;
        submissionStats.instancedDraws += stats.instancedDraws;
        submissionStats.outlineBatches += stats.outlineBatches;
        submissionStats.geometrySwitches += stats.geometrySwitches;
        submissionStats.materialSwitches += stats.materialSwitches;
        submissionStats.textureSwitches += stats.textureSwitches;
        sawSubmissionStats = true;
    }

    void drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint32_t* indices,
                                      std::size_t indexCount,
                                      const WorldTextureData* texture,
                                      const float*,
                                      int,
                                      int) override {
        (void)vertices;
        (void)indices;
        DrawCall call;
        if (texture && texture->key) call.key = texture->key;
        if (texture) {
            call.alphaMode = texture->alphaMode;
            call.rgbaNonNull = (texture->rgba != nullptr);
            call.width = texture->width;
            call.height = texture->height;
            call.clipSpaceDepthBias = texture->clipSpaceDepthBias;
        }
        call.vertexCount = vertexCount;
        call.indexCount = indexCount;
        calls.push_back(std::move(call));
    }

    bool supportsWorldIndexedMeshInstancing() const override { return true; }

    void drawWorldIndexedMeshTexturedCachedInstanced(const char* geometryKey,
                                                     const WorldMeshVertex* vertices,
                                                     std::size_t vertexCount,
                                                     const std::uint32_t* indices,
                                                     std::size_t indexCount,
                                                     const WorldTextureData* texture,
                                                     const WorldMeshInstance* instances,
                                                     std::size_t instanceCount,
                                                     const float* viewProjectionMatrix4x4,
                                                     int surfaceWidth,
                                                     int surfaceHeight) override {
        (void)geometryKey;
        (void)vertices;
        (void)indices;
        (void)instances;
        (void)viewProjectionMatrix4x4;
        (void)surfaceWidth;
        (void)surfaceHeight;
        DrawCall call;
        if (texture && texture->key) call.key = texture->key;
        if (texture) {
            call.alphaMode = texture->alphaMode;
            call.rgbaNonNull = (texture->rgba != nullptr);
            call.width = texture->width;
            call.height = texture->height;
            call.clipSpaceDepthBias = texture->clipSpaceDepthBias;
        }
        call.vertexCount = vertexCount;
        call.indexCount = indexCount;
        call.instanceCount = instanceCount;
        call.instanced = true;
        calls.push_back(std::move(call));
    }
};

game::runtime::shared_world_batches::WorldIndexedBatch makeBatch(const std::string& key,
                                                                 std::uint8_t alphaMode,
                                                                 float sortDepth,
                                                                 bool useOwnedTexture) {
    game::runtime::shared_world_batches::WorldIndexedBatch batch;
    batch.textureKey = key;
    batch.alphaMode = alphaMode;
    batch.sortDepth = sortDepth;
    batch.textureWidth = 1;
    batch.textureHeight = 1;
    if (useOwnedTexture) {
        batch.ownedTextureRgba = {255, 255, 255, 255};
    } else {
        static const unsigned char kTex[4] = {255, 0, 0, 255};
        batch.textureRgba = kTex;
    }
    batch.vertices.resize(3);
    batch.indices = {0u, 1u, 2u};
    return batch;
}

} // namespace

bool test_shared_world_indexed_batches_contract(std::string& outFail) {
    using game::runtime::shared_world_batches::WorldIndexedBatch;
    using game::runtime::shared_world_batches::prewarmWorldIndexedSubmissionWorkingSet;
    using game::runtime::shared_world_batches::submitWorldIndexedBatches;

    RecordingBackend backend;
    const float viewProj[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    std::vector<WorldIndexedBatch> batches;
    batches.push_back(makeBatch("opaque_a", 0u, 0.0f, false));
    batches.push_back(makeBatch("blend_far_a", 2u, 9.0f, false));
    batches.push_back(makeBatch("mask_b", 1u, 0.0f, true));
    batches.push_back(makeBatch("blend_far_b", 2u, 9.0f, false));
    batches.push_back(makeBatch("blend_near", 2u, 3.0f, false));
    batches.push_back(WorldIndexedBatch{}); // empty batch should be skipped

    prewarmWorldIndexedSubmissionWorkingSet(backend, batches);

    if (!expect(backend.calls.empty() && !backend.sawSubmissionStats,
                "prewarmWorldIndexedSubmissionWorkingSet should only prime indexed submission working storage, not emit draws or stats.",
                outFail)) {
        return false;
    }

    submitWorldIndexedBatches(backend, batches, viewProj, 1280, 720);

    if (!expect(backend.calls.size() == 5u,
                "submitWorldIndexedBatches should skip empty batches and draw the remaining textured batches.",
                outFail)) {
        return false;
    }

    const std::vector<std::string> expectedOrder = {
        "opaque_a",
        "mask_b",
        "blend_far_a",
        "blend_far_b",
        "blend_near"
    };
    for (std::size_t i = 0; i < expectedOrder.size(); ++i) {
        if (!expect(backend.calls[i].key == expectedOrder[i],
                    "submitWorldIndexedBatches draw ordering regression (opaque/mask insertion order + stable blend depth sort).",
                    outFail)) {
            return false;
        }
    }

    if (!expect(backend.calls[1].rgbaNonNull && backend.calls[1].width == 1 && backend.calls[1].height == 1,
                "submitWorldIndexedBatches should pass ownedTextureRgba data to the backend when textureRgba is null.",
                outFail)) {
        return false;
    }

    if (!expect(backend.sawSubmissionStats &&
                    backend.submissionStats.opaqueDraws == 2u &&
                    backend.submissionStats.blendDraws == 3u &&
                    backend.submissionStats.dynamicDraws == 5u &&
                    backend.submissionStats.cachedDraws == 0u &&
                    backend.submissionStats.textureSwitches == 5u,
                "submitWorldIndexedBatches should publish indexed submit-path counters for the emitted draw order.",
                outFail)) {
        return false;
    }

    backend.calls.clear();
    backend.submissionStats = {};
    backend.sawSubmissionStats = false;
    WorldIndexedBatch authoredZ =
        makeBatch("authored_z", 0u, 0.0f, false);
    WorldIndexedBatch authoredA =
        makeBatch("authored_a", 0u, 0.0f, false);
    authoredZ.preserveSubmissionOrder = true;
    authoredA.preserveSubmissionOrder = true;
    submitWorldIndexedBatches(
        backend,
        {authoredZ, authoredA},
        viewProj,
        1280,
        720);
    if (!expect(
            backend.calls.size() == 2u &&
                backend.calls[0].key == "authored_z" &&
                backend.calls[1].key == "authored_a",
            "Authored opaque batches must bypass renderer-state sorting and "
            "retain source polygon-group order.",
            outFail)) {
        return false;
    }

    backend.calls.clear();
    backend.submissionStats = {};
    backend.sawSubmissionStats = false;
    WorldIndexedBatch sharedBlendTemplate =
        makeBatch("shared_template_blend", 2u, 7.0f, false);
    sharedBlendTemplate.clipSpaceDepthBias = 0.02f;
    WorldIndexedBatch sharedBlendWrapper;
    sharedBlendWrapper.sharedTemplate = &sharedBlendTemplate;
    sharedBlendWrapper.sharedVertices =
        sharedBlendTemplate.vertices.data();
    sharedBlendWrapper.sharedVertexCount =
        sharedBlendTemplate.vertices.size();
    sharedBlendWrapper.sharedIndices =
        sharedBlendTemplate.indices.data();
    sharedBlendWrapper.sharedIndexCount =
        sharedBlendTemplate.indices.size();
    sharedBlendWrapper.instances.resize(1u);
    submitWorldIndexedBatches(
        backend,
        {sharedBlendWrapper},
        viewProj,
        1280,
        720);
    if (!expect(
            backend.calls.size() == 1u &&
                backend.calls.front().alphaMode == 2u &&
                std::abs(
                    backend.calls.front().clipSpaceDepthBias -
                    0.02f) < 0.000001f &&
                !backend.calls.front().instanced &&
                backend.submissionStats.opaqueDraws == 0u &&
                backend.submissionStats.blendDraws == 1u &&
                backend.submissionStats.instancedDraws == 0u,
            "Shared-template alpha/depth policy must classify the wrapper batch "
            "into the resolved blend pass and submit its one instance "
            "without the instanced vertex path.",
            outFail)) {
        return false;
    }

    backend.calls.clear();
    backend.submissionStats = {};
    backend.sawSubmissionStats = false;
    WorldIndexedBatch instancedBatch = makeBatch("instanced_growl", 2u, 5.0f, false);
    instancedBatch.geometryCacheKey = "__growl_geom_line_v1__:test";
    instancedBatch.vertices.clear();
    instancedBatch.indices.clear();
    static const IRenderBackend::WorldMeshVertex kSharedVertices[3] = {};
    static const std::uint32_t kSharedIndices[3] = {0u, 1u, 2u};
    instancedBatch.sharedVertices = kSharedVertices;
    instancedBatch.sharedVertexCount = 3u;
    instancedBatch.sharedIndices = kSharedIndices;
    instancedBatch.sharedIndexCount = 3u;
    instancedBatch.instances.resize(2u);
    submitWorldIndexedBatches(backend, {instancedBatch}, viewProj, 1280, 720);

    if (!expect(backend.calls.size() == 1u &&
                    backend.calls.front().instanced &&
                    backend.calls.front().instanceCount == 2u,
                "submitWorldIndexedBatches should route batches with per-instance payloads through the instanced cached-mesh backend path.",
                outFail)) {
        return false;
    }

    if (!expect(backend.sawSubmissionStats &&
                    backend.submissionStats.cachedDraws == 1u &&
                    backend.submissionStats.instancedDraws == 1u,
                "submitWorldIndexedBatches should mark cached instanced submissions in the shared indexed counters.",
                outFail)) {
        return false;
    }

    backend.calls.clear();
    backend.submissionStats = {};
    backend.sawSubmissionStats = false;
    game::runtime::shared_world_batches::WorldIndexedBatch sharedTemplate = makeBatch(
        "auto_instanced_model",
        0u,
        0.0f,
        false);
    sharedTemplate.geometryCacheKey = "__auto_instance_geom__:test";
    static const IRenderBackend::WorldMeshVertex kModelVertices[3] = {};
    static const std::uint32_t kModelIndices[3] = {0u, 1u, 2u};
    sharedTemplate.sharedVertices = kModelVertices;
    sharedTemplate.sharedVertexCount = 3u;
    sharedTemplate.sharedIndices = kModelIndices;
    sharedTemplate.sharedIndexCount = 3u;
    sharedTemplate.vertices.clear();
    sharedTemplate.indices.clear();

    WorldIndexedBatch autoInstanceA;
    autoInstanceA.sharedTemplate = &sharedTemplate;
    autoInstanceA.geometryCacheKey = sharedTemplate.geometryCacheKey;
    autoInstanceA.sharedVertices = sharedTemplate.sharedVertices;
    autoInstanceA.sharedVertexCount = sharedTemplate.sharedVertexCount;
    autoInstanceA.sharedIndices = sharedTemplate.sharedIndices;
    autoInstanceA.sharedIndexCount = sharedTemplate.sharedIndexCount;
    autoInstanceA.modelMatrix[12] = 1.0f;
    autoInstanceA.vertexColorMulR = 0.8f;

    WorldIndexedBatch autoInstanceB = autoInstanceA;
    autoInstanceB.modelMatrix[12] = 2.0f;
    autoInstanceB.vertexColorMulR = 0.6f;

    submitWorldIndexedBatches(
        backend,
        {autoInstanceA, autoInstanceB},
        viewProj,
        1280,
        720);

    if (!expect(backend.calls.size() == 1u &&
                    backend.calls.front().instanced &&
                    backend.calls.front().instanceCount == 2u,
                "submitWorldIndexedBatches should auto-merge compatible opaque cached batches into a single instanced draw.",
                outFail)) {
        return false;
    }

    backend.calls.clear();
    backend.submissionStats = {};
    backend.sawSubmissionStats = false;

    WorldIndexedBatch localAutoInstanceA = makeBatch(
        "local_auto_instanced_model",
        0u,
        0.0f,
        false);
    localAutoInstanceA.geometryCacheKey = "__local_auto_instance_geom__:test";
    localAutoInstanceA.sharedVertices = kModelVertices;
    localAutoInstanceA.sharedVertexCount = 3u;
    localAutoInstanceA.sharedIndices = kModelIndices;
    localAutoInstanceA.sharedIndexCount = 3u;
    localAutoInstanceA.vertices.clear();
    localAutoInstanceA.indices.clear();
    localAutoInstanceA.modelMatrix[12] = 3.0f;
    localAutoInstanceA.vertexColorMulG = 0.7f;

    WorldIndexedBatch localAutoInstanceB = localAutoInstanceA;
    localAutoInstanceB.modelMatrix[12] = 4.0f;
    localAutoInstanceB.vertexColorMulG = 0.5f;

    submitWorldIndexedBatches(
        backend,
        {localAutoInstanceA, localAutoInstanceB},
        viewProj,
        1280,
        720);

    if (!expect(backend.calls.size() == 1u &&
                    backend.calls.front().instanced &&
                    backend.calls.front().instanceCount == 2u &&
                    backend.calls.front().key == "local_auto_instanced_model",
                "submitWorldIndexedBatches should auto-merge compatible opaque cached batches by resolved local material state, not only by shared template pointer.",
                outFail)) {
        return false;
    }

    if (!expect(backend.sawSubmissionStats &&
                    backend.submissionStats.cachedDraws == 1u &&
                    backend.submissionStats.instancedDraws == 1u,
                "submitWorldIndexedBatches should preserve cached/instanced submission counters for resolved-state auto-instancing.",
                outFail)) {
        return false;
    }

    return true;
}


bool test_projected_triangle_submit_clears_geometry_cache_key(std::string& outFail) {
    using game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder;
    using game::runtime::shared_projected_scene::DepthTri;
    using game::runtime::shared_projected_unit_backend_mesh_submit::TriangleSubmitter;
    using game::runtime::shared_world_batches::WorldIndexedBatch;

    std::vector<IRenderBackend::DebugTriangle> debugTriangles;
    std::vector<IRenderBackend::WorldTriangle> worldTriangles3D;
    std::vector<IRenderBackend::DebugLine> debugLines;
    const glm::mat4 identity(1.0f);
    const glm::vec4 viewport(0.0f, 0.0f, 1280.0f, 720.0f);
    ProjectedDebugVfxBuilder projectedDebug(
        /*supportsWorldTriangles3D=*/true,
        identity,
        identity,
        720,
        viewport,
        debugTriangles,
        worldTriangles3D,
        debugLines);

    const auto runCase = [&](bool textured,
                             const std::string& keySuffix,
                             bool doubleSided = false,
                             bool forceFrontFacing = false,
                             bool reverseWinding = false,
                             bool expectSubmission = true) -> bool {
        std::vector<WorldIndexedBatch> batches(1);
        WorldIndexedBatch& batch = batches[0];
        batch.geometryCacheKey = "cached_geom_" + keySuffix;
        static const IRenderBackend::WorldMeshVertex kSharedVertices[3] = {};
        static const std::uint32_t kSharedIndices[3] = {0u, 1u, 2u};
        batch.sharedVertices = kSharedVertices;
        batch.sharedVertexCount = 3u;
        batch.sharedIndices = kSharedIndices;
        batch.sharedIndexCount = 3u;
        if (textured) {
            static const unsigned char kTex[4] = {255u, 255u, 255u, 255u};
            batch.textureRgba = kTex;
            batch.textureWidth = 1;
            batch.textureHeight = 1;
        }

        std::vector<std::vector<int>> remap(1);
        std::vector<DepthTri> modelDepthTris;
        TriangleSubmitter submitter;
        submitter.initialize(TriangleSubmitter::Args{
            .supportsWorldTriangles3D = true,
            .useIndexedWorldModelPath = true,
            .fullIndexedMeshPath = true,
            .fastTexturedPathEnabled = false,
            .backfaceCullingEnabled = false,
            .cameraWorldPos = glm::vec3(0.0f, 5.0f, 8.0f),
            .lightDir = glm::vec3(0.0f, 1.0f, 0.0f),
            .projectedDebug = &projectedDebug,
            .modelIndexedBatchesPerSubmesh = &batches,
            .modelIndexedVertexRemap = &remap,
            .modelDepthTris = &modelDepthTris,
            .world3DTriangles = &worldTriangles3D});
        const glm::vec3 b = reverseWinding
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 c = reverseWinding
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        submitter.pushTriangle(
            glm::vec3(0.0f, 0.0f, 0.0f),
            b,
            c,
            0u,
            1u,
            2u,
            glm::vec2(0.0f, 0.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            glm::vec3(1.0f),
            glm::vec3(1.0f),
            glm::vec3(1.0f),
            0u,
            1.0f,
            doubleSided,
            forceFrontFacing);

        if (!expectSubmission) {
            return batch.vertices.empty() && batch.indices.empty();
        }

        return batch.geometryCacheKey.empty() &&
               batch.sharedVertices == nullptr &&
               batch.sharedVertexCount == 0u &&
               batch.sharedIndices == nullptr &&
               batch.sharedIndexCount == 0u &&
               !batch.vertices.empty() &&
               !batch.indices.empty();
    };

    if (!expect(
            runCase(/*textured=*/true, "textured"),
            "triangle submit should clear cached geometry on textured indexed batches before mutating vertices.",
            outFail)) {
        return false;
    }

    if (!expect(
            runCase(/*textured=*/false, "flat"),
            "triangle submit should clear cached geometry on untextured indexed batches before mutating vertices.",
            outFail)) {
        return false;
    }

    if (!expect(
            runCase(
                /*textured=*/true,
                "front_only_double_sided",
                /*doubleSided=*/true,
                /*forceFrontFacing=*/true,
                /*reverseWinding=*/true,
                /*expectSubmission=*/false),
            "a front-facing-only facial shell should reject a rear-facing triangle even when its source mesh is generally double-sided.",
            outFail)) {
        return false;
    }

    return true;
}

