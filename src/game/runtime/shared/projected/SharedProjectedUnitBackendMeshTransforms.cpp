#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"

#include "engine/core/Environment.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace {

glm::vec3 safeNormalizeVec3(const glm::vec3& v) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-12f) return glm::normalize(v);
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 safeNormalizeVec3(const glm::vec3& v, const glm::vec3& fallback) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-12f) return glm::normalize(v);
    return fallback;
}

bool backendClipSkinningEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_CLIP_SKINNING");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

struct NodeTransformCacheEntry {
    glm::mat4 worldM{1.0f};
    glm::mat3 worldNormalM{1.0f};
};

struct TransformScratch {
    std::vector<std::vector<glm::mat4>> skinMatricesByNode;
    std::vector<std::uint8_t> skinMatricesReady;
    std::vector<glm::mat4> nodeGlobalInverseCache;
    std::vector<std::uint8_t> nodeGlobalInverseReady;

    std::vector<NodeTransformCacheEntry> nodeTransformCache;
    std::vector<std::uint8_t> nodeTransformWorldReady;
    std::vector<std::uint8_t> nodeTransformNormalReady;

    std::vector<game::runtime::shared_projected_unit_backend_mesh_transforms::WorldVertexSample>
        worldVertexCache;
    std::vector<int> worldVertexCacheNode;
    std::vector<std::uint8_t> worldVertexCacheValid;

    std::vector<glm::vec3> worldVertexPosCache;
    std::vector<int> worldVertexPosCacheNode;
    std::vector<std::uint8_t> worldVertexPosCacheValid;
};

thread_local TransformScratch g_scratch;
constexpr std::size_t kMaxGpuSkinMatrices = 64u;

} // namespace

namespace game::runtime::shared_projected_unit_backend_mesh_transforms {

void Resolver::initialize(const shared_projected_unit_backend_mesh::Args& args,
                          const shared_projected_unit_backend_mesh_prep::PreparedState& prep) {
    renderArgs_ = &args;
    prep_ = &prep;
    mesh_ = prep.mesh;
    unit_ = args.unit;
    pose_ = args.pose;
    modelM_ = prep.modelM;
    worldCellSize_ = args.worldCellSize;
    hasClipPose_ = prep.scenePose.hasClipPose;
    usePositionOnlyVertexPath_ = prep.usePositionOnlyVertexPath;
    clipSkinningEnabled_ = backendClipSkinningEnabled() && args.enableClipSkinning;
    gpuClipSkinningRequested_ = args.enableGpuClipSkinning;

    nodeGlobals_ = prep.scenePose.hasScenePose ? &prep.scenePose.nodeGlobals : &mesh_->bindNodeGlobals;
    nodeCount_ = nodeGlobals_->size();

    if (g_scratch.skinMatricesByNode.size() < nodeCount_) g_scratch.skinMatricesByNode.resize(nodeCount_);
    for (std::size_t ni = 0; ni < nodeCount_; ++ni) g_scratch.skinMatricesByNode[ni].clear();

    if (g_scratch.skinMatricesReady.size() < nodeCount_) g_scratch.skinMatricesReady.resize(nodeCount_, 0u);
    std::fill(g_scratch.skinMatricesReady.begin(), g_scratch.skinMatricesReady.begin() + nodeCount_, 0u);

    if (g_scratch.nodeGlobalInverseCache.size() < nodeCount_) {
        g_scratch.nodeGlobalInverseCache.resize(nodeCount_, glm::mat4(1.0f));
    }
    if (g_scratch.nodeGlobalInverseReady.size() < nodeCount_) {
        g_scratch.nodeGlobalInverseReady.resize(nodeCount_, 0u);
    }
    std::fill(
        g_scratch.nodeGlobalInverseReady.begin(),
        g_scratch.nodeGlobalInverseReady.begin() + nodeCount_,
        0u);

    const std::size_t nodeCacheCount = nodeCount_ + 1u;
    if (g_scratch.nodeTransformCache.size() < nodeCacheCount) g_scratch.nodeTransformCache.resize(nodeCacheCount);
    if (g_scratch.nodeTransformWorldReady.size() < nodeCacheCount) {
        g_scratch.nodeTransformWorldReady.resize(nodeCacheCount, 0u);
    }
    if (g_scratch.nodeTransformNormalReady.size() < nodeCacheCount) {
        g_scratch.nodeTransformNormalReady.resize(nodeCacheCount, 0u);
    }
    std::fill(
        g_scratch.nodeTransformWorldReady.begin(),
        g_scratch.nodeTransformWorldReady.begin() + nodeCacheCount,
        0u);
    std::fill(
        g_scratch.nodeTransformNormalReady.begin(),
        g_scratch.nodeTransformNormalReady.begin() + nodeCacheCount,
        0u);

    const std::size_t meshVertexCount = mesh_ ? mesh_->vertices.size() : 0u;
    if (!usePositionOnlyVertexPath_) {
        g_scratch.worldVertexCache.resize(meshVertexCount);
        g_scratch.worldVertexCacheNode.assign(meshVertexCount, std::numeric_limits<int>::min());
        g_scratch.worldVertexCacheValid.assign(meshVertexCount, 0u);
    } else {
        g_scratch.worldVertexCache.clear();
        g_scratch.worldVertexCacheNode.clear();
        g_scratch.worldVertexCacheValid.clear();
    }
    g_scratch.worldVertexPosCache.resize(meshVertexCount);
    g_scratch.worldVertexPosCacheNode.assign(meshVertexCount, std::numeric_limits<int>::min());
    g_scratch.worldVertexPosCacheValid.assign(meshVertexCount, 0u);
}

std::size_t Resolver::nodeTransformIndexFor(int triNodeIndex) const {
    std::size_t cacheIndex = 0u;
    if (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeCount_) {
        cacheIndex = static_cast<std::size_t>(triNodeIndex) + 1u;
    }
    return cacheIndex;
}

const std::vector<glm::mat4>* Resolver::ensureSkinMatricesForNode(int nodeIndex) {
    if (!mesh_ || !nodeGlobals_) return nullptr;
    if (nodeIndex < 0 ||
        static_cast<std::size_t>(nodeIndex) >= mesh_->nodeSkin.size() ||
        static_cast<std::size_t>(nodeIndex) >= nodeGlobals_->size()) {
        return nullptr;
    }
    const std::size_t nodeIdx = static_cast<std::size_t>(nodeIndex);
    const int skinIndex = mesh_->nodeSkin[nodeIdx];
    if (skinIndex < 0 || static_cast<std::size_t>(skinIndex) >= mesh_->skins.size()) {
        return nullptr;
    }

    if (g_scratch.skinMatricesReady[nodeIdx] == 0u) {
        const auto& skin = mesh_->skins[static_cast<std::size_t>(skinIndex)];
        if (g_scratch.nodeGlobalInverseReady[nodeIdx] == 0u) {
            g_scratch.nodeGlobalInverseCache[nodeIdx] = glm::inverse((*nodeGlobals_)[nodeIdx]);
            g_scratch.nodeGlobalInverseReady[nodeIdx] = 1u;
        }
        const glm::mat4& invMeshGlobal = g_scratch.nodeGlobalInverseCache[nodeIdx];
        auto& mats = g_scratch.skinMatricesByNode[nodeIdx];
        mats.assign(skin.joints.size(), glm::mat4(1.0f));
        for (std::size_t j = 0; j < skin.joints.size(); ++j) {
            const int jointNode = skin.joints[j];
            if (jointNode < 0 || static_cast<std::size_t>(jointNode) >= nodeGlobals_->size()) {
                continue;
            }
            const glm::mat4 invBind =
                (j < skin.inverseBind.size()) ? skin.inverseBind[j] : glm::mat4(1.0f);
            mats[j] =
                invMeshGlobal *
                (*nodeGlobals_)[static_cast<std::size_t>(jointNode)] *
                invBind;
        }
        g_scratch.skinMatricesReady[nodeIdx] = 1u;
    }
    return &g_scratch.skinMatricesByNode[nodeIdx];
}

Resolver::SkinResult Resolver::skinVertexAtNode(
    int nodeIndex,
    const runtime::backend_model::MeshVertex& vtx,
    const glm::vec3& localPos,
    const glm::vec3& localNormal) {
    SkinResult outSkin{localPos, localNormal, false};
    if (hasClipPose_ && !clipSkinningEnabled_) {
        return outSkin;
    }
    const auto* matsPtr = ensureSkinMatricesForNode(nodeIndex);
    if (!matsPtr) return outSkin;
    const auto& mats = *matsPtr;

    const std::uint16_t joints[4] = {vtx.j0, vtx.j1, vtx.j2, vtx.j3};
    const float weights[4] = {vtx.w0, vtx.w1, vtx.w2, vtx.w3};
    const bool rigidSingleJoint =
        (weights[0] >= 0.999f) &&
        (weights[1] <= 0.00001f) &&
        (weights[2] <= 0.00001f) &&
        (weights[3] <= 0.00001f) &&
        (static_cast<std::size_t>(joints[0]) < mats.size());
    if (rigidSingleJoint) {
        const glm::mat4& m = mats[static_cast<std::size_t>(joints[0])];
        outSkin.pos = glm::vec3(m * glm::vec4(localPos, 1.0f));
        outSkin.normal = safeNormalizeVec3(glm::mat3(m) * localNormal);
        outSkin.applied = true;
        return outSkin;
    }

    glm::vec4 blendedPos(0.0f);
    glm::vec3 blendedNormal(0.0f);
    float totalWeight = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float w = weights[i];
        if (w <= 0.00001f) continue;
        const std::size_t joint = static_cast<std::size_t>(joints[i]);
        if (joint >= mats.size()) continue;
        blendedPos += (mats[joint] * glm::vec4(localPos, 1.0f)) * w;
        blendedNormal += (glm::mat3(mats[joint]) * localNormal) * w;
        totalWeight += w;
    }
    if (totalWeight <= 0.00001f) return outSkin;
    if (totalWeight < 0.999f) {
        const float remain = 1.0f - totalWeight;
        blendedPos += glm::vec4(localPos, 1.0f) * remain;
        blendedNormal += localNormal * remain;
    }
    outSkin.pos = glm::vec3(blendedPos);
    outSkin.normal = safeNormalizeVec3(blendedNormal);
    outSkin.applied = true;
    return outSkin;
}

glm::vec3 Resolver::skinPositionAtNode(int nodeIndex,
                                       const runtime::backend_model::MeshVertex& vtx,
                                       const glm::vec3& localPos) {
    glm::vec3 outPos = localPos;
    if (hasClipPose_ && !clipSkinningEnabled_) {
        return outPos;
    }
    const auto* matsPtr = ensureSkinMatricesForNode(nodeIndex);
    if (!matsPtr) return outPos;
    const auto& mats = *matsPtr;

    const std::uint16_t joints[4] = {vtx.j0, vtx.j1, vtx.j2, vtx.j3};
    const float weights[4] = {vtx.w0, vtx.w1, vtx.w2, vtx.w3};
    const bool rigidSingleJoint =
        (weights[0] >= 0.999f) &&
        (weights[1] <= 0.00001f) &&
        (weights[2] <= 0.00001f) &&
        (weights[3] <= 0.00001f) &&
        (static_cast<std::size_t>(joints[0]) < mats.size());
    if (rigidSingleJoint) {
        outPos = glm::vec3(
            mats[static_cast<std::size_t>(joints[0])] *
            glm::vec4(localPos, 1.0f));
        return outPos;
    }

    glm::vec4 blendedPos(0.0f);
    float totalWeight = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float w = weights[i];
        if (w <= 0.00001f) continue;
        const std::size_t joint = static_cast<std::size_t>(joints[i]);
        if (joint >= mats.size()) continue;
        blendedPos += (mats[joint] * glm::vec4(localPos, 1.0f)) * w;
        totalWeight += w;
    }
    if (totalWeight <= 0.00001f) return outPos;
    if (totalWeight < 0.999f) {
        const float remain = 1.0f - totalWeight;
        blendedPos += glm::vec4(localPos, 1.0f) * remain;
    }
    return glm::vec3(blendedPos);
}

const glm::mat4& Resolver::worldMatrixForNode(int triNodeIndex) {
    const std::size_t cacheIndex = nodeTransformIndexFor(triNodeIndex);
    if (g_scratch.nodeTransformWorldReady[cacheIndex] != 0u) {
        return g_scratch.nodeTransformCache[cacheIndex].worldM;
    }

    const glm::mat4 nodeGlobal =
        (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
            ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
            : glm::mat4(1.0f);
    auto& entry = g_scratch.nodeTransformCache[cacheIndex];
    entry.worldM = modelM_ * nodeGlobal;
    g_scratch.nodeTransformWorldReady[cacheIndex] = 1u;
    return entry.worldM;
}

const glm::mat3& Resolver::worldNormalMatrixForNode(int triNodeIndex) {
    const std::size_t cacheIndex = nodeTransformIndexFor(triNodeIndex);
    if (g_scratch.nodeTransformNormalReady[cacheIndex] != 0u) {
        return g_scratch.nodeTransformCache[cacheIndex].worldNormalM;
    }
    if (g_scratch.nodeTransformWorldReady[cacheIndex] == 0u) {
        (void)worldMatrixForNode(triNodeIndex);
    }
    auto& entry = g_scratch.nodeTransformCache[cacheIndex];
    entry.worldNormalM = glm::transpose(glm::inverse(glm::mat3(entry.worldM)));
    g_scratch.nodeTransformNormalReady[cacheIndex] = 1u;
    return entry.worldNormalM;
}

WorldVertexSample Resolver::resolveWorldVertex(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const runtime::backend_model::MeshVertex& vtx) {
    if (vertexIndex < g_scratch.worldVertexCache.size() &&
        g_scratch.worldVertexCacheValid[vertexIndex] != 0u &&
        g_scratch.worldVertexCacheNode[vertexIndex] == triNodeIndex) {
        return g_scratch.worldVertexCache[vertexIndex];
    }

    glm::vec3 local = vtx.position;
    if (!hasClipPose_) {
        local = runtime::backend_anim::deformLocalVertex(
            *unit_,
            *pose_,
            local,
            mesh_->boundsMin,
            mesh_->boundsMax,
            worldCellSize_);
    }
    const auto sk = skinVertexAtNode(triNodeIndex, vtx, local, vtx.normal);
    const glm::mat4& worldM = worldMatrixForNode(triNodeIndex);
    const glm::mat3& worldNormalM = worldNormalMatrixForNode(triNodeIndex);

    WorldVertexSample out;
    out.pos = glm::vec3(worldM * glm::vec4(sk.pos, 1.0f));
    out.normal = safeNormalizeVec3(worldNormalM * sk.normal);

    if (vertexIndex < g_scratch.worldVertexCache.size()) {
        g_scratch.worldVertexCache[vertexIndex] = out;
        g_scratch.worldVertexCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.worldVertexCacheValid[vertexIndex] = 1u;
    }
    return out;
}

glm::vec3 Resolver::resolveWorldVertexPos(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const runtime::backend_model::MeshVertex& vtx) {
    if (vertexIndex < g_scratch.worldVertexPosCache.size() &&
        g_scratch.worldVertexPosCacheValid[vertexIndex] != 0u &&
        g_scratch.worldVertexPosCacheNode[vertexIndex] == triNodeIndex) {
        return g_scratch.worldVertexPosCache[vertexIndex];
    }

    glm::vec3 local = vtx.position;
    if (!hasClipPose_) {
        local = runtime::backend_anim::deformLocalVertex(
            *unit_,
            *pose_,
            local,
            mesh_->boundsMin,
            mesh_->boundsMax,
            worldCellSize_);
    }
    const glm::vec3 skinnedPos = skinPositionAtNode(triNodeIndex, vtx, local);
    const glm::mat4 nodeGlobal =
        (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
            ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
            : glm::mat4(1.0f);
    const glm::vec3 outPos = glm::vec3(nodeGlobal * glm::vec4(skinnedPos, 1.0f));

    if (vertexIndex < g_scratch.worldVertexPosCache.size()) {
        g_scratch.worldVertexPosCache[vertexIndex] = outPos;
        g_scratch.worldVertexPosCacheNode[vertexIndex] = triNodeIndex;
        g_scratch.worldVertexPosCacheValid[vertexIndex] = 1u;
    }
    return outPos;
}

glm::vec3 Resolver::resolveModelVertexNormal(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const runtime::backend_model::MeshVertex& vtx) {
    // Reuse world-vertex cache when available and convert back into model space normal
    // is not valid in general (requires inverse model), so compute directly.
    (void)vertexIndex;

    glm::vec3 local = vtx.position;
    if (!hasClipPose_) {
        local = runtime::backend_anim::deformLocalVertex(
            *unit_,
            *pose_,
            local,
            mesh_->boundsMin,
            mesh_->boundsMax,
            worldCellSize_);
    }
    const auto sk = skinVertexAtNode(triNodeIndex, vtx, local, vtx.normal);
    const glm::mat4 nodeGlobal =
        (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
            ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
            : glm::mat4(1.0f);
    const glm::mat3 nodeNormalM = glm::transpose(glm::inverse(glm::mat3(nodeGlobal)));
    return safeNormalizeVec3(nodeNormalM * sk.normal);
}

glm::vec4 Resolver::resolveModelVertexTangent(
    int triNodeIndex,
    std::uint32_t vertexIndex,
    const runtime::backend_model::MeshVertex& vtx) {
    (void)vertexIndex;

    glm::vec3 local = vtx.position;
    if (!hasClipPose_) {
        local = runtime::backend_anim::deformLocalVertex(
            *unit_,
            *pose_,
            local,
            mesh_->boundsMin,
            mesh_->boundsMax,
            worldCellSize_);
    }

    glm::vec3 localTangent(vtx.tangent.x, vtx.tangent.y, vtx.tangent.z);
    if (glm::dot(localTangent, localTangent) <= 1e-10f) {
        glm::vec3 n = safeNormalizeVec3(vtx.normal, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 helper =
            (std::fabs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        localTangent = safeNormalizeVec3(glm::cross(helper, n), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    const auto sk = skinVertexAtNode(triNodeIndex, vtx, local, localTangent);
    const glm::mat4 nodeGlobal =
        (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
            ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
            : glm::mat4(1.0f);
    const glm::mat3 nodeNormalM = glm::transpose(glm::inverse(glm::mat3(nodeGlobal)));
    const glm::vec3 tangent = safeNormalizeVec3(nodeNormalM * sk.normal, glm::vec3(1.0f, 0.0f, 0.0f));
    const float sign = (vtx.tangent.w < 0.0f) ? -1.0f : 1.0f;
    return glm::vec4(tangent, sign);
}

glm::vec3 Resolver::resolveGpuSkinningInputPos(
    std::uint32_t vertexIndex,
    const runtime::backend_model::MeshVertex& vtx) {
    if (vertexIndex < g_scratch.worldVertexPosCache.size() &&
        g_scratch.worldVertexPosCacheValid[vertexIndex] != 0u &&
        g_scratch.worldVertexPosCacheNode[vertexIndex] == std::numeric_limits<int>::min()) {
        return g_scratch.worldVertexPosCache[vertexIndex];
    }

    // GPU skinning path expects local (pre-skin) positions.
    const glm::vec3 outPos = vtx.position;

    if (vertexIndex < g_scratch.worldVertexPosCache.size()) {
        g_scratch.worldVertexPosCache[vertexIndex] = outPos;
        g_scratch.worldVertexPosCacheNode[vertexIndex] = std::numeric_limits<int>::min();
        g_scratch.worldVertexPosCacheValid[vertexIndex] = 1u;
    }
    return outPos;
}

bool Resolver::configureGpuClipSkinningBatch(
    int triNodeIndex,
    std::array<float, 16>& inOutModelMatrix,
    std::vector<float>& outSkinMatrices,
    std::uint32_t& outSkinMatrixCount) {
    outSkinMatrices.clear();
    outSkinMatrixCount = 0u;
    if (!gpuClipSkinningRequested_ || !clipSkinningEnabled_ || !hasClipPose_ ||
        !usePositionOnlyVertexPath_) {
        return false;
    }

    const auto* mats = ensureSkinMatricesForNode(triNodeIndex);
    if (!mats || mats->empty() || mats->size() > kMaxGpuSkinMatrices) {
        return false;
    }

    const glm::mat4 nodeGlobal =
        (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals_->size())
            ? (*nodeGlobals_)[static_cast<std::size_t>(triNodeIndex)]
            : glm::mat4(1.0f);
    const glm::mat4 combinedModel = modelM_ * nodeGlobal;
    const float* modelData = glm::value_ptr(combinedModel);
    std::copy(modelData, modelData + 16, inOutModelMatrix.begin());

    outSkinMatrices.resize(mats->size() * 16u);
    for (std::size_t i = 0; i < mats->size(); ++i) {
        const float* src = glm::value_ptr((*mats)[i]);
        std::copy(src, src + 16, outSkinMatrices.data() + (i * 16u));
    }
    outSkinMatrixCount = static_cast<std::uint32_t>(mats->size());
    return true;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_transforms
