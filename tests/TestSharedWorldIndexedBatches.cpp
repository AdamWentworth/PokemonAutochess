#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.h"
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
        std::size_t vertexCount = 0u;
        std::size_t indexCount = 0u;
    };

    std::vector<DrawCall> calls;

    const char* backendId() const override { return "test"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}

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
        }
        call.vertexCount = vertexCount;
        call.indexCount = indexCount;
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

    const auto runCase = [&](bool textured, const std::string& keySuffix) -> bool {
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
        submitter.pushTriangle(
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
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
            false);

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

    return true;
}

