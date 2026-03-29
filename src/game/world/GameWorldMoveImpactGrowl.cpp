#include "game/world/GameWorld.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/Model.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/world/MoveImpactMath.h"

namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

glm::vec3 safeForwardXZ(const glm::vec3& v) {
    glm::vec3 f(v.x, 0.0f, v.z);
    const float len = glm::length(f);
    if (len <= 0.0001f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return f / len;
}

glm::mat4 buildModelInstanceTransform(const PokemonInstance& instance,
                                      float backendModelScaleFactor = 1.0f) {
    const float scaleFactor =
        computeModelWorldScaleForMoveImpact(instance, backendModelScaleFactor);

    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
    const glm::mat4 rotationX =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    const glm::mat4 rotationZ =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
    const glm::vec3 renderPos = instance.position + glm::vec3(0.0f, instance.visualYOffset, 0.0f);
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);

    return translation * rotationY * rotationX * rotationZ * scale;
}

std::string resolveBackendModelPath(const PokemonInstance& unit, const GameDataDb* data) {
    if (!unit.backendModelPath.empty()) return unit.backendModelPath;
    if (!unit.animIndexCacheSourceModelPath.empty()) return unit.animIndexCacheSourceModelPath;
    if (!unit.backendAnimDurationsSourceModelPath.empty()) {
        return unit.backendAnimDurationsSourceModelPath;
    }
    if (!data) return {};

    const PokemonStats* stats = data->pokemon.getStats(unit.name);
    if (!stats || stats->model.empty()) return {};
    return "assets/models/" + stats->model;
}

struct BackendGrowlMeshCacheEntry {
    bool attemptedLoad = false;
    game::runtime::render_model::MeshData mesh;
    bool cachedGrowlNodeIndicesReady = false;
    std::vector<int> cachedGrowlNodeIndices;
};

BackendGrowlMeshCacheEntry* tryLoadBackendGrowlMesh(const std::string& modelPath) {
    if (modelPath.empty()) return nullptr;

    static std::unordered_map<std::string, BackendGrowlMeshCacheEntry> cache;
    auto& entry = cache[modelPath];
    if (!entry.attemptedLoad) {
        entry.attemptedLoad = true;
        std::string err;
        if (!game::runtime::render_model::loadMeshFromCache(modelPath, entry.mesh, &err)) {
            entry.mesh = game::runtime::render_model::MeshData{};
        }
    }

    if (entry.mesh.nodesDefault.empty()) return nullptr;
    return &entry;
}

float scoreGrowlAnchorCandidate(const glm::vec3& worldPos,
                                const glm::vec3& expectedWorld,
                                std::size_t candidateRank,
                                float animBias) {
    const glm::vec2 planarDelta(worldPos.x - expectedWorld.x, worldPos.z - expectedWorld.z);
    const float planar = glm::length(planarDelta);
    const float vertical = std::abs(worldPos.y - expectedWorld.y);
    return planar * 2.0f + vertical + static_cast<float>(candidateRank) * 0.0005f + animBias;
}

template <size_t N>
std::vector<int> resolveBackendGrowlNodeIndices(const std::vector<std::string>& nodeNames,
                                                const std::array<const char*, N>& candidates) {
    std::vector<int> result;
    if (nodeNames.empty()) return result;

    std::array<std::string, N> loweredCandidates{};
    for (std::size_t i = 0; i < N; ++i) {
        loweredCandidates[i] = candidates[i] ? lowerCopy(candidates[i]) : std::string{};
    }

    auto appendIfMissing = [&](int nodeIndex) {
        if (nodeIndex < 0) return;
        if (std::find(result.begin(), result.end(), nodeIndex) == result.end()) {
            result.push_back(nodeIndex);
        }
    };

    for (const auto& candidate : loweredCandidates) {
        if (candidate.empty()) continue;
        for (std::size_t ni = 0; ni < nodeNames.size(); ++ni) {
            const std::string nodeLower = lowerCopy(nodeNames[ni]);
            if (nodeLower == candidate) {
                appendIfMissing(static_cast<int>(ni));
            }
        }
    }

    for (const auto& candidate : loweredCandidates) {
        if (candidate.empty()) continue;
        for (std::size_t ni = 0; ni < nodeNames.size(); ++ni) {
            const std::string nodeLower = lowerCopy(nodeNames[ni]);
            if (nodeLower.find(candidate) != std::string::npos) {
                appendIfMissing(static_cast<int>(ni));
            }
        }
    }

    return result;
}

template <size_t N>
bool tryResolveAnimatedNodeWorld(const PokemonInstance& unit,
                                 const std::array<const char*, N>& nodeNames,
                                 const glm::vec3& expectedWorldPos,
                                 glm::vec3& outWorldPos) {
    if (!unit.model) return false;

    const glm::mat4 instanceM = buildModelInstanceTransform(unit);
    const int activeAnim = (unit.activeAnimIndex >= 0) ? unit.activeAnimIndex : unit.animIdleIndex;
    const int idleAnim = unit.animIdleIndex;
    float bestScore = std::numeric_limits<float>::max();
    bool found = false;

    auto tryAnim = [&](int animIndex, float animBias) {
        if (animIndex < 0) return false;
        for (std::size_t i = 0; i < N; ++i) {
            const char* nodeName = nodeNames[i];
            if (!nodeName || !nodeName[0]) continue;
            glm::mat4 nodeGlobal(1.0f);
            if (!unit.model->getNodeGlobalTransformByName(unit.animTimeSec, animIndex, nodeName, nodeGlobal)) {
                continue;
            }
            const glm::mat4 nodeWorld = instanceM * nodeGlobal;
            const glm::vec3 worldPos(nodeWorld[3]);
            const float score =
                scoreGrowlAnchorCandidate(worldPos, expectedWorldPos, i, animBias);
            if (score < bestScore) {
                bestScore = score;
                outWorldPos = worldPos;
                found = true;
            }
        }
        return found;
    };

    (void)tryAnim(activeAnim, 0.0f);
    if (idleAnim != activeAnim) {
        (void)tryAnim(idleAnim, 0.01f);
    }
    return found;
}

template <size_t N>
bool tryResolveBackendAnimatedNodeWorld(const PokemonInstance& unit,
                                        const GameDataDb* data,
                                        const std::array<const char*, N>& nodeNames,
                                        const glm::vec3& expectedWorldPos,
                                        glm::vec3& outWorldPos) {
    const std::string modelPath = resolveBackendModelPath(unit, data);
    BackendGrowlMeshCacheEntry* entry = tryLoadBackendGrowlMesh(modelPath);
    if (!entry) return false;

    if (!entry->cachedGrowlNodeIndicesReady) {
        entry->cachedGrowlNodeIndices = resolveBackendGrowlNodeIndices(entry->mesh.nodeNames, nodeNames);
        entry->cachedGrowlNodeIndicesReady = true;
    }

    if (entry->cachedGrowlNodeIndices.empty()) {
        return false;
    }

    const glm::mat4 instanceM =
        buildModelInstanceTransform(unit, std::max(0.01f, entry->mesh.modelScaleFactor));
    const int activeAnim = (unit.activeAnimIndex >= 0) ? unit.activeAnimIndex : unit.animIdleIndex;
    const int idleAnim = unit.animIdleIndex;
    float bestScore = std::numeric_limits<float>::max();
    bool found = false;

    auto tryPose = [&](int animIndex, float animBias) {
        if (animIndex < 0) return false;
        const bool loopingClip =
            game::runtime::shared_backend_pose::shouldTreatSceneClipAsLooping(unit, animIndex);
        const auto pose = game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
            entry->mesh,
            animIndex,
            unit.animTimeSec,
            true,
            loopingClip);
        if (!pose.hasScenePose) return false;

        for (std::size_t i = 0; i < entry->cachedGrowlNodeIndices.size(); ++i) {
            const int nodeIndex = entry->cachedGrowlNodeIndices[i];
            if (nodeIndex < 0 ||
                static_cast<std::size_t>(nodeIndex) >= pose.nodeGlobals.size()) {
                continue;
            }
            const glm::mat4 nodeWorld =
                instanceM * pose.nodeGlobals[static_cast<std::size_t>(nodeIndex)];
            const glm::vec3 worldPos(nodeWorld[3]);
            const float score =
                scoreGrowlAnchorCandidate(worldPos, expectedWorldPos, i, animBias);
            if (score < bestScore) {
                bestScore = score;
                outWorldPos = worldPos;
                found = true;
            }
        }
        return found;
    };

    (void)tryPose(activeAnim, 0.0f);
    if (idleAnim != activeAnim) {
        (void)tryPose(idleAnim, 0.01f);
    }
    return found;
}

}  // namespace

void GameWorld::emitGrowlImpact(const PokemonInstance& target,
                                const PokemonInstance* attacker,
                                const glm::vec3& forward) {
    if (!growlWaveVfxInitialized) {
        GrowlWaveVFX::Config c;
        c.spawnForwardOffset = 0.0f;
        c.spawnHeightOffset = 0.0f;
        c.drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
        growlWaveVfx.setConfig(c);
        growlWaveVfxInitialized = true;
    }

    glm::vec3 origin =
        attacker ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
                 : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));

    if (attacker) {
        const glm::vec3 fwdXZ = safeForwardXZ(forward);
        const glm::vec3 renderPos = attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f);
        const float worldScale = computeModelWorldScaleForMoveImpact(*attacker);

        // Emergency fallback near mouth/head area in world-space.
        // Current shipped rigs all expose viable anchor nodes, so this should
        // rarely be used outside malformed content or pathological clips.
        glm::vec3 fallbackOrigin = renderPos + glm::vec3(0.0f, 0.14f, 0.0f);
        fallbackOrigin += fwdXZ * 0.10f;

        static constexpr std::array<const char*, 22> kGrowlNodeCandidates = {
            "jaw", "Jaw",
            "EffMouth01", "effmouth01",
            "mouth01.", "Mouth01.",
            "mouth01", "Mouth01",
            "mouth", "Mouth",
            "Nose", "nose",
            "snout", "Snout",
            "muzzle", "Muzzle",
            "head", "Head",
            "neck", "Neck",
            "chin", "Chin"};

        glm::vec3 mouthWorld(0.0f);
        bool resolvedFromNode = false;
        if (tryResolveAnimatedNodeWorld(*attacker, kGrowlNodeCandidates, fallbackOrigin, mouthWorld) ||
            tryResolveBackendAnimatedNodeWorld(*attacker, data, kGrowlNodeCandidates, fallbackOrigin, mouthWorld)) {
            origin = mouthWorld;
            resolvedFromNode = true;
        } else {
            origin = fallbackOrigin;
        }

        // Safety checks: reject pathological node transforms that place the origin
        // underground or behind the caster (seen on some rigs/clips).
        if (resolvedFromNode) {
            const glm::vec3 planarDelta =
                glm::vec3(origin.x - renderPos.x, 0.0f, origin.z - renderPos.z);
            const float planarDist2 = glm::dot(planarDelta, planarDelta);
            const float maxPlanar = std::max(0.30f, getBoardCellSize() * 0.9f);
            const bool tooLow = origin.y < (renderPos.y + 0.04f);
            const bool tooFar = planarDist2 > (maxPlanar * maxPlanar);
            const bool behind = glm::dot(planarDelta, fwdXZ) < -0.05f;
            if (tooLow || tooFar || behind) {
                origin = fallbackOrigin;
            }
        }

        const std::string species = lowerCopy(attacker->name);
        float speciesGrowlYOffset = 0.0f;
        float speciesForwardBonus = 0.0f;
        if (species == "bulbasaur") {
            // Bulbasaur mouth sits slightly lower than generic anchors.
            speciesGrowlYOffset = -0.01f;
            speciesForwardBonus = 0.03f;
        }

        float forwardPush = 0.08f + speciesForwardBonus;
        if (attacker->model && attacker->model->hasBounds()) {
            const float r = attacker->model->getBoundsRadiusHorizontal() * worldScale;
            forwardPush = std::clamp(r * 0.38f + speciesForwardBonus, 0.08f, 0.18f);
        }

        origin += glm::vec3(0.0f, speciesGrowlYOffset, 0.0f);
        origin += fwdXZ * forwardPush;

        // Final vertical guardrail against extreme node-space values.
        const float minGrowlY = renderPos.y + 0.06f;
        const float maxGrowlY = renderPos.y + std::max(0.28f, getBoardCellSize() * 0.42f);
        origin.y = std::clamp(origin.y, minGrowlY, maxGrowlY);
    }

    growlWaveVfx.emitFrom(origin, forward, hasLastViewMatrix ? &lastViewMatrix : nullptr);
}
