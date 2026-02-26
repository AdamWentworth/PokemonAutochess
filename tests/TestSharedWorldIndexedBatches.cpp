#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/IRenderBackend.h"
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

