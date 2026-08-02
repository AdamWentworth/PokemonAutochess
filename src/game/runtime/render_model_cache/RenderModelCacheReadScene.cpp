#include "game/runtime/render_model_cache/RenderModelCacheReadScene.h"

#include <istream>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {
template <typename T>
bool readPod(std::istream& in, T& out) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&out), sizeof(T)));
}

bool readString(std::istream& in, std::string& out) {
    std::uint32_t n = 0;
    if (!readPod(in, n)) return false;
    out.clear();
    if (n == 0) return true;
    out.resize(n);
    return static_cast<bool>(in.read(out.data(), n));
}

glm::mat4 trsToMat4(const engine::render::model_types::NodeTRS& n) {
    if (n.hasMatrix) return n.matrix;
    const glm::mat4 t = glm::translate(glm::mat4(1.0f), n.t);
    const glm::mat4 r = glm::mat4_cast(glm::normalize(n.r));
    const glm::mat4 s = glm::scale(glm::mat4(1.0f), n.s);
    return t * r * s;
}

void buildNodeParentTable(const std::vector<std::vector<int>>& nodeChildren,
                          std::vector<int>& outParent) {
    outParent.assign(nodeChildren.size(), -1);
    for (std::size_t p = 0; p < nodeChildren.size(); ++p) {
        for (int c : nodeChildren[p]) {
            if (c < 0 || static_cast<std::size_t>(c) >= outParent.size()) continue;
            if (outParent[static_cast<std::size_t>(c)] < 0) {
                outParent[static_cast<std::size_t>(c)] = static_cast<int>(p);
            }
        }
    }
}

void buildNodeGlobals(const std::vector<engine::render::model_types::NodeTRS>& nodesDefault,
                      const std::vector<std::vector<int>>& nodeChildren,
                      const std::vector<int>& sceneRoots,
                      std::vector<glm::mat4>& outGlobals) {
    outGlobals.assign(nodesDefault.size(), glm::mat4(1.0f));
    if (nodesDefault.empty()) return;

    std::vector<int> parent;
    buildNodeParentTable(nodeChildren, parent);

    const auto dfs = [&](const auto& self, int node, const glm::mat4& parentM) -> void {
        if (node < 0 || static_cast<std::size_t>(node) >= nodesDefault.size()) return;
        const glm::mat4 global = parentM * trsToMat4(nodesDefault[static_cast<std::size_t>(node)]);
        outGlobals[static_cast<std::size_t>(node)] = global;
        if (static_cast<std::size_t>(node) >= nodeChildren.size()) return;
        for (int child : nodeChildren[static_cast<std::size_t>(node)]) {
            self(self, child, global);
        }
    };

    if (!sceneRoots.empty()) {
        for (int root : sceneRoots) {
            dfs(dfs, root, glm::mat4(1.0f));
        }
        return;
    }

    bool drewAny = false;
    for (std::size_t i = 0; i < parent.size(); ++i) {
        if (parent[i] >= 0) continue;
        dfs(dfs, static_cast<int>(i), glm::mat4(1.0f));
        drewAny = true;
    }
    if (!drewAny) {
        dfs(dfs, 0, glm::mat4(1.0f));
    }
}

bool readSceneData(std::istream& in,
                   std::uint32_t nodeCount,
                   std::uint32_t skinCount,
                   std::uint32_t animCount,
                   std::vector<engine::render::model_types::NodeTRS>& outNodesDefault,
                   std::vector<std::string>& outNodeNames,
                   std::vector<std::vector<int>>& outNodeChildren,
                   std::vector<int>& outNodeParent,
                   std::vector<int>& outNodeMesh,
                   std::vector<int>& outNodeSkin,
                   std::vector<int>& outSceneRoots,
                   std::vector<engine::render::model_types::SkinData>& outSkins,
                   std::vector<engine::render::model_types::AnimationClip>& outAnimations) {
    outNodesDefault.assign(nodeCount, engine::render::model_types::NodeTRS{});
    outNodeNames.assign(nodeCount, std::string{});
    outNodeChildren.assign(nodeCount, {});
    outNodeMesh.assign(nodeCount, -1);
    outNodeSkin.assign(nodeCount, -1);

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        auto& n = outNodesDefault[static_cast<std::size_t>(i)];
        std::uint8_t hasMatrix = 0u;
        if (!readPod(in, n.t) ||
            !readPod(in, n.r) ||
            !readPod(in, n.s) ||
            !readPod(in, hasMatrix) ||
            !readPod(in, n.matrix)) {
            return false;
        }
        n.r = glm::normalize(n.r);
        n.hasMatrix = (hasMatrix != 0u);
    }

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        if (!readString(in, outNodeNames[static_cast<std::size_t>(i)])) return false;
    }

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::uint32_t childCount = 0u;
        if (!readPod(in, childCount)) return false;
        auto& children = outNodeChildren[static_cast<std::size_t>(i)];
        children.assign(childCount, -1);
        for (std::uint32_t k = 0; k < childCount; ++k) {
            std::int32_t v = -1;
            if (!readPod(in, v)) return false;
            children[static_cast<std::size_t>(k)] = static_cast<int>(v);
        }
    }

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::int32_t v = -1;
        if (!readPod(in, v)) return false;
        outNodeMesh[static_cast<std::size_t>(i)] = static_cast<int>(v);
    }
    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::int32_t v = -1;
        if (!readPod(in, v)) return false;
        outNodeSkin[static_cast<std::size_t>(i)] = static_cast<int>(v);
    }

    std::uint32_t rootCount = 0u;
    if (!readPod(in, rootCount)) return false;
    outSceneRoots.assign(rootCount, -1);
    for (std::uint32_t i = 0; i < rootCount; ++i) {
        std::int32_t v = -1;
        if (!readPod(in, v)) return false;
        outSceneRoots[static_cast<std::size_t>(i)] = static_cast<int>(v);
    }

    outSkins.assign(skinCount, engine::render::model_types::SkinData{});
    for (std::uint32_t si = 0; si < skinCount; ++si) {
        std::uint32_t jointCount = 0u;
        if (!readPod(in, jointCount)) return false;
        auto& skin = outSkins[static_cast<std::size_t>(si)];
        skin.joints.assign(jointCount, -1);
        skin.inverseBind.assign(jointCount, glm::mat4(1.0f));
        for (std::uint32_t j = 0; j < jointCount; ++j) {
            std::int32_t v = -1;
            if (!readPod(in, v)) return false;
            skin.joints[static_cast<std::size_t>(j)] = static_cast<int>(v);
        }
        for (std::uint32_t j = 0; j < jointCount; ++j) {
            if (!readPod(in, skin.inverseBind[static_cast<std::size_t>(j)])) return false;
        }
    }

    outAnimations.assign(animCount, engine::render::model_types::AnimationClip{});
    for (std::uint32_t ai = 0; ai < animCount; ++ai) {
        auto& clip = outAnimations[static_cast<std::size_t>(ai)];
        if (!readString(in, clip.name) || !readPod(in, clip.durationSec)) return false;

        std::uint32_t samplerCount = 0u;
        if (!readPod(in, samplerCount)) return false;
        clip.samplers.assign(samplerCount, engine::render::model_types::AnimationSampler{});
        for (std::uint32_t s = 0; s < samplerCount; ++s) {
            auto& samp = clip.samplers[static_cast<std::size_t>(s)];
            std::uint8_t isVec4 = 0u;
            if (!readString(in, samp.interpolation) || !readPod(in, isVec4)) return false;
            samp.isVec4 = (isVec4 != 0u);

            std::uint32_t inputCount = 0u;
            if (!readPod(in, inputCount)) return false;
            samp.inputs.assign(inputCount, 0.0f);
            for (std::uint32_t i = 0; i < inputCount; ++i) {
                if (!readPod(in, samp.inputs[static_cast<std::size_t>(i)])) return false;
            }

            std::uint32_t outputCount = 0u;
            if (!readPod(in, outputCount)) return false;
            samp.outputs.assign(outputCount, glm::vec4(0.0f));
            for (std::uint32_t i = 0; i < outputCount; ++i) {
                if (!readPod(in, samp.outputs[static_cast<std::size_t>(i)])) return false;
            }
        }

        std::uint32_t channelCount = 0u;
        if (!readPod(in, channelCount)) return false;
        clip.channels.assign(channelCount, engine::render::model_types::AnimationChannel{});
        for (std::uint32_t c = 0; c < channelCount; ++c) {
            std::int32_t samplerIndex = -1;
            std::int32_t targetNode = -1;
            std::uint8_t path = 0u;
            if (!readPod(in, samplerIndex) || !readPod(in, targetNode) || !readPod(in, path)) {
                return false;
            }
            auto& ch = clip.channels[static_cast<std::size_t>(c)];
            ch.samplerIndex = static_cast<int>(samplerIndex);
            ch.targetNode = static_cast<int>(targetNode);
            ch.path = (path == 1u) ? engine::render::model_types::ChannelPath::Rotation
                    : (path == 2u) ? engine::render::model_types::ChannelPath::Scale
                                   : engine::render::model_types::ChannelPath::Translation;
        }
    }

    buildNodeParentTable(outNodeChildren, outNodeParent);
    return true;
}

} // namespace

namespace game::runtime::render_model::detail {

bool readSceneFromValidatedCacheStream(std::istream& in,
                                       const CacheHeader& hdr,
                                       MeshData& out,
                                       std::string* outError) {
    constexpr std::uint32_t kMaxNodes = 4096u;
    constexpr std::uint32_t kMaxSkins = 512u;
    constexpr std::uint32_t kMaxAnimations = 512u;
    if (hdr.nodeCount > kMaxNodes || hdr.skinCount > kMaxSkins || hdr.animCount > kMaxAnimations) {
        if (outError) *outError = "cache scene metadata exceeds safety limits";
        return false;
    }

    if (!readSceneData(in,
                       hdr.nodeCount,
                       hdr.skinCount,
                       hdr.animCount,
                       out.nodesDefault,
                       out.nodeNames,
                       out.nodeChildren,
                       out.nodeParent,
                       out.nodeMesh,
                       out.nodeSkin,
                       out.sceneRoots,
                       out.skins,
                       out.animations)) {
        if (outError) *outError = "failed to read cache scene/animation sections";
        return false;
    }

    buildNodeGlobals(out.nodesDefault, out.nodeChildren, out.sceneRoots, out.bindNodeGlobals);
    return true;
}

} // namespace game::runtime::render_model::detail

