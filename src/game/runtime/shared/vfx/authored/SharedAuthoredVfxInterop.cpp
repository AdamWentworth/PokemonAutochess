#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"

namespace game::runtime::shared_authored_vfx_interop {

vfx::runtime::authored_batches::MeshData toReusableMeshData(
    const render_model::MeshData& mesh) {
    vfx::runtime::authored_batches::MeshData out;
    out.vertices.reserve(mesh.vertices.size());
    out.indices = mesh.indices;
    for (const auto& src : mesh.vertices) {
        vfx::runtime::authored_batches::MeshVertex dst;
        dst.position = src.position;
        dst.normal = src.normal;
        dst.tangent = src.tangent;
        dst.uv = src.uv;
        dst.color = src.color;
        dst.j0 = src.j0;
        dst.j1 = src.j1;
        dst.j2 = src.j2;
        dst.j3 = src.j3;
        dst.w0 = src.w0;
        dst.w1 = src.w1;
        dst.w2 = src.w2;
        dst.w3 = src.w3;
        out.vertices.push_back(dst);
    }
    return out;
}

shared_world_batches::WorldIndexedBatch toWorldIndexedBatch(
    const vfx::runtime::authored_batches::WorldIndexedBatch& src) {
    shared_world_batches::WorldIndexedBatch dst;
    dst.vertices = src.vertices;
    dst.indices = src.indices;
    dst.sharedVertices = src.sharedVertices;
    dst.sharedVertexCount = src.sharedVertexCount;
    dst.sharedIndices = src.sharedIndices;
    dst.sharedIndexCount = src.sharedIndexCount;
    dst.geometryCacheKey = src.geometryCacheKey;
    dst.instances = src.instances;
    dst.textureKey = src.textureKey;
    dst.textureCacheKey = src.textureCacheKey;
    dst.textureRgba = src.textureRgba;
    dst.textureWidth = src.textureWidth;
    dst.textureHeight = src.textureHeight;
    dst.textureWrapS = src.textureWrapS;
    dst.textureWrapT = src.textureWrapT;
    dst.alphaMode = src.alphaMode;
    dst.blendMode = src.blendMode;
    dst.alphaCutoff = src.alphaCutoff;
    dst.vertexColorMulR = src.vertexColorMulR;
    dst.vertexColorMulG = src.vertexColorMulG;
    dst.vertexColorMulB = src.vertexColorMulB;
    dst.vertexColorMulA = src.vertexColorMulA;
    dst.characterInkingEnabled = src.characterInkingEnabled;
    dst.sortDepth = src.sortDepth;
    dst.modelMatrix = src.modelMatrix;
    return dst;
}

void appendWorldIndexedBatches(
    const std::vector<vfx::runtime::authored_batches::WorldIndexedBatch>& src,
    std::vector<shared_world_batches::WorldIndexedBatch>& dst) {
    if (src.empty()) return;
    dst.reserve(dst.size() + src.size());
    for (const auto& batch : src) {
        dst.push_back(toWorldIndexedBatch(batch));
    }
}

} // namespace game::runtime::shared_authored_vfx_interop
