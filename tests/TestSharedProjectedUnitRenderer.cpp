#include "game/runtime/shared/projected/SharedProjectedUnitRenderer.h"

#include "engine/core/Paths.h"
#include "game/config/AnimSetLoader.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "engine/runtime/FixedStep.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"

#include <cmath>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) {
        return true;
    }
    outFail = message;
    return false;
}

bool readJson(const std::string& path, nlohmann::json& out, std::string& outFail) {
    std::ifstream f(path);
    if (!f) {
        outFail = "Failed to open JSON: " + path;
        return false;
    }
    try {
        f >> out;
    } catch (...) {
        outFail = "Failed to parse JSON: " + path;
        return false;
    }
    return true;
}

int resolveAnimIndex(const game::runtime::render_model::MeshData& mesh, const std::string& clipName) {
    const auto stripSuffix = [](const std::string& s, const std::string& suffix) {
        if (s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix) {
            return s.substr(0, s.size() - suffix.size());
        }
        return s;
    };
    for (std::size_t i = 0; i < mesh.animations.size(); ++i) {
        const std::string& name = mesh.animations[i].name;
        if (name == clipName ||
            stripSuffix(name, ".gfbanm") == stripSuffix(clipName, ".gfbanm")) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

struct PoseDeltaMetrics {
    float maxTranslationDelta = 0.0f;
    float maxScaleDelta = 0.0f;
    float maxRotationAngleDeg = 0.0f;
};

PoseDeltaMetrics measurePoseDelta(const game::runtime::shared_backend_pose::PoseEval& a,
                                  const game::runtime::shared_backend_pose::PoseEval& b) {
    PoseDeltaMetrics out;
    const std::size_t count = std::min(a.nodeLocals.size(), b.nodeLocals.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& lhs = a.nodeLocals[i];
        const auto& rhs = b.nodeLocals[i];
        out.maxTranslationDelta =
            std::max(out.maxTranslationDelta, glm::length(lhs.t - rhs.t));
        out.maxScaleDelta =
            std::max(out.maxScaleDelta, glm::length(lhs.s - rhs.s));
        const glm::quat qa = glm::normalize(lhs.r);
        const glm::quat qb = glm::normalize(rhs.r);
        const float dotAbs = std::clamp(std::abs(glm::dot(qa, qb)), 0.0f, 1.0f);
        const float angleDeg = glm::degrees(2.0f * std::acos(dotAbs));
        out.maxRotationAngleDeg = std::max(out.maxRotationAngleDeg, angleDeg);
    }
    return out;
}

} // namespace

bool test_shared_projected_unit_renderer_scene_pose_cache_contract(std::string& outFail) {
    using game::runtime::shared_projected_units::detail::canonicalSceneAnimTimeForCacheKey;

    game::runtime::render_model::MeshData loopedMesh;
    loopedMesh.animations.resize(1u);
    loopedMesh.animations[0].durationSec = 1.0f;

    const auto nearLoopEnd = canonicalSceneAnimTimeForCacheKey(
        loopedMesh,
        0,
        0.999f,
        engine::runtime::fixed_step::kSeconds);
    if (!expect(nearLoopEnd.cacheKey != 0u,
                "Near-loop samples should stay on a valid cached pose instead of snapping to the zero bucket early.",
                outFail)) {
        return false;
    }
    if (!expect(nearLoopEnd.animTimeSec > 0.99f && nearLoopEnd.animTimeSec < 1.0f,
                "Near-loop samples should clamp to the last valid clip time before wrap.",
                outFail)) {
        return false;
    }

    const auto initialLoopStart = canonicalSceneAnimTimeForCacheKey(
        loopedMesh,
        0,
        0.0f,
        engine::runtime::fixed_step::kSeconds);
    if (!expect(initialLoopStart.cacheKey == 0u && initialLoopStart.animTimeSec == 0.0f,
                "A true initial loop start should still map to the canonical zero sample.",
                outFail)) {
        return false;
    }

    const auto exactLoopWrap = canonicalSceneAnimTimeForCacheKey(
        loopedMesh,
        0,
        1.0f,
        engine::runtime::fixed_step::kSeconds);
    if (!expect(exactLoopWrap.cacheKey != 0u,
                "A positive loop boundary should reuse the last valid loop sample instead of flashing the first frame.",
                outFail)) {
        return false;
    }
    if (!expect(exactLoopWrap.animTimeSec > 0.99f && exactLoopWrap.animTimeSec < 1.0f,
                "A positive loop boundary should clamp to the last valid clip time before wrap.",
                outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData shortMesh;
    shortMesh.animations.resize(1u);
    shortMesh.animations[0].durationSec = 0.01f;
    const auto shortClip = canonicalSceneAnimTimeForCacheKey(
        shortMesh,
        0,
        0.005f,
        engine::runtime::fixed_step::kSeconds);
    if (!expect(shortClip.cacheKey != 0u,
                "Very short clips should bypass coarse quantization instead of collapsing to the zero bucket.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(shortClip.animTimeSec - 0.005f) < 0.00001f,
                "Very short clips should preserve their wrapped sample time when quantization exceeds clip duration.",
                outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData blendedMesh;
    blendedMesh.nodesDefault.resize(1u);
    blendedMesh.animations.resize(1u);
    auto& loopClip = blendedMesh.animations[0];
    loopClip.durationSec = 1.0f;
    loopClip.samplers.resize(1u);
    loopClip.channels.resize(1u);
    loopClip.samplers[0].inputs = {0.0f, 0.5f};
    loopClip.samplers[0].outputs = {
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(10.0f, 0.0f, 0.0f, 0.0f)};
    loopClip.channels[0].samplerIndex = 0;
    loopClip.channels[0].targetNode = 0;
    loopClip.channels[0].path = pac_model_types::ChannelPath::Translation;

    game::runtime::shared_backend_pose::PoseEval blendedPose;
    game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
        blendedMesh,
        0,
        0.75f,
        true,
        true,
        blendedPose);
    if (!expect(blendedPose.nodeLocals.size() == 1u,
                "Loop-blended pose evaluation should preserve the mesh node count.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(blendedPose.nodeLocals[0].t.x - 5.0f) < 0.001f,
                "Looped clips should blend from the last key back toward the first key near the end of the cycle instead of holding the last key and popping on wrap.",
                outFail)) {
        return false;
    }

    return true;
}

bool test_shared_projected_unit_renderer_idle_clip_loop_closure_contract(std::string& outFail) {
    struct CaseDef {
        std::string modelPath;
        std::string animsetPath;
        std::string roleKey;
        std::string fallbackCategory;
        std::vector<std::string> preferredSubstrings;
    };

    const std::vector<CaseDef> cases = {
        {
            engine::paths::asset("models/0025_Pikachu.glb"),
            engine::paths::asset("models/0025_Pikachu.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
        },
        {
            engine::paths::asset("models/0021_Spearow.glb"),
            engine::paths::asset("models/0021_Spearow.animset.json"),
            "air_idle",
            "idle",
            {"fi01_wait", "fly", "air", "hover"},
        },
        {
            engine::paths::asset("models/0056_Mankey.glb"),
            engine::paths::asset("models/0056_Mankey.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
        },
    };

    for (const auto& def : cases) {
        nlohmann::json j;
        if (!readJson(def.animsetPath, j, outFail)) return false;

        const auto pick = AnimSet::resolveRoleClip(
            j,
            def.roleKey,
            def.fallbackCategory,
            def.preferredSubstrings,
            true);
        if (!expect(pick.valid && !pick.clipName.empty(),
                    "Expected a resolved idle clip for " + def.animsetPath,
                    outFail)) {
            return false;
        }

        game::runtime::render_model::MeshData mesh;
        std::string meshError;
        if (!game::runtime::render_model::loadMeshFromCache(def.modelPath, mesh, &meshError)) {
            outFail = "Failed to load mesh cache for seam test: " + def.modelPath + " error=" + meshError;
            return false;
        }

        const int animIndex = resolveAnimIndex(mesh, pick.clipName);
        if (!expect(animIndex >= 0,
                    "Resolved clip '" + pick.clipName + "' was not found in mesh animations for " + def.modelPath,
                    outFail)) {
            return false;
        }

        const float durationSec = mesh.animations[static_cast<std::size_t>(animIndex)].durationSec;
        if (!expect(durationSec > 0.0f,
                    "Resolved idle clip should have positive duration for " + pick.clipName,
                    outFail)) {
            return false;
        }

        const auto nearEndPose =
            game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
                mesh,
                animIndex,
                std::nextafter(durationSec, 0.0f),
                true,
                true);
        const auto startPose =
            game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
                mesh,
                animIndex,
                0.0f,
                true,
                true);

        if (!expect(nearEndPose.nodeLocals.size() == startPose.nodeLocals.size(),
                    "Idle seam pose evaluations should agree on node counts.",
                    outFail)) {
            return false;
        }

        const PoseDeltaMetrics deltas = measurePoseDelta(nearEndPose, startPose);

        if (!expect(deltas.maxTranslationDelta <= 0.05f &&
                        deltas.maxScaleDelta <= 0.05f &&
                        deltas.maxRotationAngleDeg <= 8.0f,
                    "Idle seam discontinuity is still too large for '" + pick.clipName +
                        "' trans=" + std::to_string(deltas.maxTranslationDelta) +
                        " scale=" + std::to_string(deltas.maxScaleDelta) +
                        " rotDeg=" + std::to_string(deltas.maxRotationAngleDeg),
                    outFail)) {
            return false;
        }
    }

    return true;
}

bool test_shared_projected_unit_renderer_idle_fixed_step_wrap_contract(std::string& outFail) {
    struct CaseDef {
        std::string modelPath;
        std::string animsetPath;
        std::string roleKey;
        std::string fallbackCategory;
        std::vector<std::string> preferredSubstrings;
    };

    const std::vector<CaseDef> cases = {
        {
            engine::paths::asset("models/0025_Pikachu.glb"),
            engine::paths::asset("models/0025_Pikachu.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
        },
        {
            engine::paths::asset("models/0056_Mankey.glb"),
            engine::paths::asset("models/0056_Mankey.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
        },
        {
            engine::paths::asset("models/0014_Kakuna.glb"),
            engine::paths::asset("models/0014_Kakuna.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
        },
    };

    for (const auto& def : cases) {
        nlohmann::json j;
        if (!readJson(def.animsetPath, j, outFail)) return false;

        const auto pick = AnimSet::resolveRoleClip(
            j,
            def.roleKey,
            def.fallbackCategory,
            def.preferredSubstrings,
            true);
        if (!expect(pick.valid && !pick.clipName.empty(),
                    "Expected a resolved idle clip for " + def.animsetPath,
                    outFail)) {
            return false;
        }

        game::runtime::render_model::MeshData mesh;
        std::string meshError;
        if (!game::runtime::render_model::loadMeshFromCache(def.modelPath, mesh, &meshError)) {
            outFail = "Failed to load mesh cache for wrap test: " + def.modelPath + " error=" + meshError;
            return false;
        }

        const int animIndex = resolveAnimIndex(mesh, pick.clipName);
        if (!expect(animIndex >= 0,
                    "Resolved clip '" + pick.clipName + "' was not found in mesh animations for " + def.modelPath,
                    outFail)) {
            return false;
        }

        const float durationSec = mesh.animations[static_cast<std::size_t>(animIndex)].durationSec;
        if (!expect(durationSec > 0.0f,
                    "Resolved idle clip should have positive duration for " + pick.clipName,
                    outFail)) {
            return false;
        }

        const float dt = engine::runtime::fixed_step::kSeconds;
        int wrapStep = std::max(1, static_cast<int>(std::ceil(durationSec / dt)));
        float preWrapTime = std::fmod(static_cast<float>(wrapStep - 1) * dt, durationSec);
        float postWrapTime = std::fmod(static_cast<float>(wrapStep) * dt, durationSec);
        if (!(postWrapTime < preWrapTime)) {
            ++wrapStep;
            preWrapTime = std::fmod(static_cast<float>(wrapStep - 1) * dt, durationSec);
            postWrapTime = std::fmod(static_cast<float>(wrapStep) * dt, durationSec);
        }
        if (!expect(postWrapTime < preWrapTime,
                    "Expected a fixed-step wrap boundary for '" + pick.clipName + "'.",
                    outFail)) {
            return false;
        }

        const auto preWrapPose =
            game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
                mesh,
                animIndex,
                preWrapTime,
                true,
                true);
        const auto postWrapPose =
            game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
                mesh,
                animIndex,
                postWrapTime,
                true,
                true);
        if (!expect(preWrapPose.nodeLocals.size() == postWrapPose.nodeLocals.size(),
                    "Fixed-step wrap pose evaluations should agree on node counts.",
                    outFail)) {
            return false;
        }

        const PoseDeltaMetrics deltas = measurePoseDelta(preWrapPose, postWrapPose);
        if (!expect(deltas.maxTranslationDelta <= 0.08f &&
                        deltas.maxScaleDelta <= 0.08f &&
                        deltas.maxRotationAngleDeg <= 12.0f,
                    "Idle fixed-step wrap discontinuity is still too large for '" + pick.clipName +
                        "' trans=" + std::to_string(deltas.maxTranslationDelta) +
                        " scale=" + std::to_string(deltas.maxScaleDelta) +
                        " rotDeg=" + std::to_string(deltas.maxRotationAngleDeg),
                    outFail)) {
            return false;
        }
    }

    return true;
}
