#include "game/runtime/shared/SharedWorldIndexedBatches.h"

#include <algorithm>

namespace game::runtime::shared_world_batches {

namespace {

IRenderBackend::WorldTextureData toWorldTextureData(const WorldIndexedBatch& batch) {
    const unsigned char* rgbaData = batch.textureRgba;
    if (!rgbaData && !batch.ownedTextureRgba.empty()) {
        rgbaData = batch.ownedTextureRgba.data();
    }

    IRenderBackend::WorldTextureData tex;
    tex.key = batch.textureKey.c_str();
    tex.rgba = rgbaData;
    tex.width = batch.textureWidth;
    tex.height = batch.textureHeight;
    tex.wrapS = batch.textureWrapS;
    tex.wrapT = batch.textureWrapT;
    tex.alphaMode = batch.alphaMode;
    tex.blendMode = batch.blendMode;
    tex.materialMode = batch.materialMode;
    tex.alphaCutoff = batch.alphaCutoff;
    tex.materialTimeSec = batch.materialTimeSec;
    tex.materialFlags = batch.materialFlags;
    tex.materialAtlasWidth = batch.materialAtlasWidth;
    tex.materialAtlasHeight = batch.materialAtlasHeight;
    tex.materialRect0U = batch.materialRect0U;
    tex.materialRect0V = batch.materialRect0V;
    tex.materialRect0W = batch.materialRect0W;
    tex.materialRect0H = batch.materialRect0H;
    tex.materialRect1U = batch.materialRect1U;
    tex.materialRect1V = batch.materialRect1V;
    tex.materialRect1W = batch.materialRect1W;
    tex.materialRect1H = batch.materialRect1H;
    tex.materialFlipbook0Cols = batch.materialFlipbook0Cols;
    tex.materialFlipbook0Rows = batch.materialFlipbook0Rows;
    tex.materialFlipbook0Frames = batch.materialFlipbook0Frames;
    tex.materialFlipbook0Fps = batch.materialFlipbook0Fps;
    tex.materialFlipbook1Cols = batch.materialFlipbook1Cols;
    tex.materialFlipbook1Rows = batch.materialFlipbook1Rows;
    tex.materialFlipbook1Frames = batch.materialFlipbook1Frames;
    tex.materialFlipbook1Fps = batch.materialFlipbook1Fps;
    return tex;
}

void drawOneBatch(IRenderBackend& renderer,
                  const WorldIndexedBatch& batch,
                  const float* viewProjectionMatrix4x4,
                  int surfaceWidth,
                  int surfaceHeight) {
    IRenderBackend::WorldTextureData tex = toWorldTextureData(batch);
    renderer.drawWorldIndexedMeshTextured(
        batch.vertices.data(),
        batch.vertices.size(),
        batch.indices.data(),
        batch.indices.size(),
        &tex,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

} // namespace

void submitWorldIndexedBatches(IRenderBackend& renderer,
                               const std::vector<WorldIndexedBatch>& batches,
                               const float* viewProjectionMatrix4x4,
                               int surfaceWidth,
                               int surfaceHeight) {
    if (batches.empty() || !viewProjectionMatrix4x4 || surfaceWidth <= 0 || surfaceHeight <= 0) return;

    for (const WorldIndexedBatch& batch : batches) {
        if (batch.vertices.empty() || batch.indices.empty()) continue;
        if (batch.alphaMode == 2u) continue;
        drawOneBatch(renderer, batch, viewProjectionMatrix4x4, surfaceWidth, surfaceHeight);
    }

    static thread_local std::vector<const WorldIndexedBatch*> blendBatches;
    blendBatches.clear();
    if (blendBatches.capacity() < batches.size()) {
        blendBatches.reserve(batches.size());
    }
    for (const WorldIndexedBatch& batch : batches) {
        if (batch.vertices.empty() || batch.indices.empty()) continue;
        if (batch.alphaMode != 2u) continue;
        blendBatches.push_back(&batch);
    }
    std::stable_sort(
        blendBatches.begin(),
        blendBatches.end(),
        [](const WorldIndexedBatch* lhs, const WorldIndexedBatch* rhs) {
            return lhs->sortDepth > rhs->sortDepth;
        });
    for (const WorldIndexedBatch* batch : blendBatches) {
        if (!batch) continue;
        drawOneBatch(renderer, *batch, viewProjectionMatrix4x4, surfaceWidth, surfaceHeight);
    }
}

} // namespace game::runtime::shared_world_batches

