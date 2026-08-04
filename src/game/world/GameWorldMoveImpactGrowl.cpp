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
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/world/MoveImpactMath.h"
#include "vfx/effects/growl/GrowlWaveVfxConfig.h"

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
    if (!stats || stats->resolveModel(unit.modelVariant).empty()) return {};
    return "assets/models/" + stats->resolveModel(unit.modelVariant);
}

struct LegacyGrowlModelCacheEntry {
    bool cachedGrowlNodeIndicesReady = false;
    std::vector<int> cachedGrowlNodeIndices;
};

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
std::vector<int> resolveLegacyGrowlNodeIndices(const Model& model,
                                               const std::array<const char*, N>& candidates) {
    std::vector<int> result;
    result.reserve(N);

    auto appendIfMissing = [&](int nodeIndex) {
        if (nodeIndex < 0) return;
        if (std::find(result.begin(), result.end(), nodeIndex) == result.end()) {
            result.push_back(nodeIndex);
        }
    };

    for (const char* candidate : candidates) {
        if (!candidate || !candidate[0]) continue;
        int nodeIndex = -1;
        if (model.getNodeIndexByName(candidate, nodeIndex)) {
            appendIfMissing(nodeIndex);
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
    const Model* model = unit.model.get();
    if (!model) return false;

    static std::unordered_map<const Model*, LegacyGrowlModelCacheEntry> cache;
    auto& entry = cache[model];
    if (!entry.cachedGrowlNodeIndicesReady) {
        entry.cachedGrowlNodeIndices =
            resolveLegacyGrowlNodeIndices(*model, nodeNames);
        entry.cachedGrowlNodeIndicesReady = true;
    }
    if (entry.cachedGrowlNodeIndices.empty()) return false;

    const glm::mat4 instanceM = buildModelInstanceTransform(unit);
    const int activeAnim = (unit.activeAnimIndex >= 0) ? unit.activeAnimIndex : unit.animIdleIndex;
    const int idleAnim = unit.animIdleIndex;
    float bestScore = std::numeric_limits<float>::max();
    bool found = false;

    auto tryAnim = [&](int animIndex, float animBias) {
        if (animIndex < 0) return false;
        Model::AnimatedPose pose;
        model->sampleAnimatedPose(unit.animTimeSec, animIndex, pose);
        for (std::size_t i = 0; i < entry.cachedGrowlNodeIndices.size(); ++i) {
            const int nodeIndex = entry.cachedGrowlNodeIndices[i];
            if (nodeIndex < 0 ||
                static_cast<std::size_t>(nodeIndex) >= pose.globals.size()) {
                continue;
            }
            const glm::mat4 nodeWorld =
                instanceM * pose.globals[static_cast<std::size_t>(nodeIndex)];
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
    (void)nodeNames;
    const auto* mesh = resolveMoveImpactMeshForModelPath(modelPath);
    const auto* growlNodeIndices = resolveGrowlAnchorNodeIndicesForModelPath(modelPath);
    if (!mesh || !growlNodeIndices || growlNodeIndices->empty()) {
        return false;
    }

    const glm::mat4 instanceM =
        buildModelInstanceTransform(unit, std::max(0.01f, mesh->modelScaleFactor));
    const int activeAnim = (unit.activeAnimIndex >= 0) ? unit.activeAnimIndex : unit.animIdleIndex;
    const int idleAnim = unit.animIdleIndex;
    float bestScore = std::numeric_limits<float>::max();
    bool found = false;

    auto tryPose = [&](int animIndex, float animBias) {
        if (animIndex < 0) return false;
        const bool loopingClip =
            game::runtime::shared_backend_pose::shouldTreatSceneClipAsLooping(unit, animIndex);
        const auto pose = game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
            *mesh,
            animIndex,
            unit.animTimeSec,
            game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
            loopingClip);
        if (!pose.hasScenePose) return false;

        for (std::size_t i = 0; i < growlNodeIndices->size(); ++i) {
            const int nodeIndex = (*growlNodeIndices)[i];
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
        GrowlWaveVFX::Config c = vfx::growl_wave_config::makeSourceAlignedConfig();
        growlWaveVfx.setConfig(c);
        growlWaveVfxInitialized = true;
    }

    glm::vec3 origin =
        attacker ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
                 : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));

    if (attacker) {
        const glm::vec3 fwdXZ = safeForwardXZ(forward);
        const glm::vec3 renderPos = attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f);
        const std::string species = lowerCopy(attacker->name);
        const float speciesGrowlYOffset = (species == "bulbasaur") ? -0.01f : 0.0f;

        // Emergency fallback near mouth/head area in world-space.
        // Current shipped rigs all expose viable anchor nodes, so this should
        // rarely be used outside malformed content or pathological clips.
        glm::vec3 fallbackOrigin = renderPos + glm::vec3(0.0f, 0.14f, 0.0f);
        fallbackOrigin += fwdXZ * 0.10f;

        static constexpr std::array<const char*, 22> kGrowlNodeCandidates = {
            "EffMouth01", "effmouth01",
            "mouth01.", "Mouth01.",
            "mouth01", "Mouth01",
            "mouth", "Mouth",
            "jaw", "Jaw",
            "Nose", "nose",
            "snout", "Snout",
            "muzzle", "Muzzle",
            "chin", "Chin",
            "head", "Head",
            "neck", "Neck"};

        glm::vec3 mouthWorld(0.0f);
        bool resolvedFromNode =
            tryResolveAnimatedNodeWorld(*attacker, kGrowlNodeCandidates, fallbackOrigin, mouthWorld);
        if (!resolvedFromNode) {
            resolvedFromNode = tryResolveBackendAnimatedNodeWorld(
                *attacker, data, kGrowlNodeCandidates, fallbackOrigin, mouthWorld);
        }
        origin = resolvedFromNode ? mouthWorld : fallbackOrigin;

        // Safety checks: reject pathological node transforms that place the origin
        // underground or behind the caster (seen on some rigs/clips).
        const float minGrowlY = renderPos.y + 0.06f;
        const float maxGrowlY = renderPos.y + std::max(0.28f, getBoardCellSize() * 0.42f);
        if (resolvedFromNode) {
            const glm::vec3 planarDelta =
                glm::vec3(origin.x - renderPos.x, 0.0f, origin.z - renderPos.z);
            const float planarDist2 = glm::dot(planarDelta, planarDelta);
            const float maxPlanar = std::max(0.30f, getBoardCellSize() * 0.9f);
            const bool tooLow = origin.y < minGrowlY;
            const bool tooHigh = origin.y > maxGrowlY;
            const bool tooFar = planarDist2 > (maxPlanar * maxPlanar);
            const bool behind = glm::dot(planarDelta, fwdXZ) < -0.05f;
            if (tooLow || tooHigh || tooFar || behind) {
                resolvedFromNode = false;
                origin = fallbackOrigin;
            }
        }

        if (!resolvedFromNode) {
            origin += glm::vec3(0.0f, speciesGrowlYOffset, 0.0f);
            origin.y = std::clamp(origin.y, minGrowlY, maxGrowlY);
        }
    }

    growlWaveVfx.emitFrom(origin, forward, hasLastViewMatrix ? &lastViewMatrix : nullptr);
}
