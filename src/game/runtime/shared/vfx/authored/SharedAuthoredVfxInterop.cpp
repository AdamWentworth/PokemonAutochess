#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>

namespace game::runtime::shared_authored_vfx_interop {

namespace {

struct ReusableMeshCacheEntry {
    std::size_t vertexCount = 0u;
    std::size_t indexCount = 0u;
    vfx::runtime::authored_batches::MeshData mesh;
};

using AuthoredBatch = vfx::runtime::authored_batches::WorldIndexedBatch;

bool nearlyEqual(float lhs, float rhs) {
    return std::abs(lhs - rhs) <= 0.00001f;
}

bool sameMatrix(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs) {
    for (std::size_t i = 0u; i < lhs.size(); ++i) {
        if (!nearlyEqual(lhs[i], rhs[i])) return false;
    }
    return true;
}

bool hasSameGeometryPayload(const AuthoredBatch& lhs, const AuthoredBatch& rhs) {
    if (lhs.geometryCacheKey.empty() || lhs.geometryCacheKey != rhs.geometryCacheKey) {
        return false;
    }
    if (lhs.sharedVertices != rhs.sharedVertices ||
        lhs.sharedVertexCount != rhs.sharedVertexCount ||
        lhs.sharedIndices != rhs.sharedIndices ||
        lhs.sharedIndexCount != rhs.sharedIndexCount) {
        return false;
    }
    return lhs.vertices.size() == rhs.vertices.size() &&
           lhs.indices.size() == rhs.indices.size();
}

bool canMergeInstancedAdditiveBatch(const AuthoredBatch& base, const AuthoredBatch& candidate) {
    if (base.instances.empty() || candidate.instances.empty()) return false;
    if (base.alphaMode != 2u || candidate.alphaMode != 2u) return false;
    if (base.blendMode != 1u || candidate.blendMode != 1u) return false;
    if (base.dualSourceBlendEnabled != candidate.dualSourceBlendEnabled ||
        base.depthTestEnabled != candidate.depthTestEnabled ||
        base.characterInkingEnabled != candidate.characterInkingEnabled) {
        return false;
    }
    if (!hasSameGeometryPayload(base, candidate)) return false;
    if (base.textureCacheKey.empty() || base.textureCacheKey != candidate.textureCacheKey) {
        return false;
    }
    if (base.textureWidth != candidate.textureWidth ||
        base.textureHeight != candidate.textureHeight ||
        base.textureWrapS != candidate.textureWrapS ||
        base.textureWrapT != candidate.textureWrapT ||
        base.textureRgba != candidate.textureRgba) {
        return false;
    }
    if (!nearlyEqual(base.clipSpaceDepthBias, candidate.clipSpaceDepthBias) ||
        !nearlyEqual(base.alphaCutoff, candidate.alphaCutoff) ||
        !nearlyEqual(base.alphaWindowMin, candidate.alphaWindowMin) ||
        !nearlyEqual(base.alphaWindowMax, candidate.alphaWindowMax) ||
        !nearlyEqual(base.vertexColorMulR, candidate.vertexColorMulR) ||
        !nearlyEqual(base.vertexColorMulG, candidate.vertexColorMulG) ||
        !nearlyEqual(base.vertexColorMulB, candidate.vertexColorMulB) ||
        !nearlyEqual(base.vertexColorMulA, candidate.vertexColorMulA)) {
        return false;
    }
    return sameMatrix(base.modelMatrix, candidate.modelMatrix);
}

} // namespace

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

const vfx::runtime::authored_batches::MeshData& cachedReusableMeshData(
    const render_model::MeshData& mesh) {
    static thread_local std::unordered_map<const render_model::MeshData*, ReusableMeshCacheEntry> cache;

    auto& entry = cache[&mesh];
    if (entry.mesh.vertices.empty() ||
        entry.vertexCount != mesh.vertices.size() ||
        entry.indexCount != mesh.indices.size()) {
        entry.vertexCount = mesh.vertices.size();
        entry.indexCount = mesh.indices.size();
        entry.mesh = toReusableMeshData(mesh);
    }
    return entry.mesh;
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
    dst.dualSourceBlendEnabled = src.dualSourceBlendEnabled;
    dst.depthTestEnabled = src.depthTestEnabled;
    dst.clipSpaceDepthBias = src.clipSpaceDepthBias;
    dst.alphaCutoff = src.alphaCutoff;
    dst.alphaWindowMin = src.alphaWindowMin;
    dst.alphaWindowMax = src.alphaWindowMax;
    dst.vertexColorMulR = src.vertexColorMulR;
    dst.vertexColorMulG = src.vertexColorMulG;
    dst.vertexColorMulB = src.vertexColorMulB;
    dst.vertexColorMulA = src.vertexColorMulA;
    dst.characterInkingEnabled = src.characterInkingEnabled;
    dst.sortDepth = src.sortDepth;
    dst.modelMatrix = src.modelMatrix;
    return dst;
}

void mergeCompatibleInstancedAdditiveBatches(
    std::vector<vfx::runtime::authored_batches::WorldIndexedBatch>& batches) {
    if (batches.size() < 2u) return;

    std::vector<AuthoredBatch> merged;
    merged.reserve(batches.size());
    for (auto& batch : batches) {
        if (batch.instances.empty()) {
            merged.push_back(std::move(batch));
            continue;
        }

        auto targetIt = std::find_if(
            merged.begin(),
            merged.end(),
            [&](const AuthoredBatch& existing) {
                return canMergeInstancedAdditiveBatch(existing, batch);
            });
        if (targetIt == merged.end()) {
            merged.push_back(std::move(batch));
            continue;
        }

        targetIt->instances.reserve(targetIt->instances.size() + batch.instances.size());
        targetIt->instances.insert(
            targetIt->instances.end(),
            std::make_move_iterator(batch.instances.begin()),
            std::make_move_iterator(batch.instances.end()));
        targetIt->sortDepth = std::max(targetIt->sortDepth, batch.sortDepth);
    }

    batches = std::move(merged);
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
