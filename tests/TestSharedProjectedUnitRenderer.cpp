#include "game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.h"

#include "engine/core/Paths.h"
#include "game/config/AnimSetLoader.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "engine/runtime/FixedStep.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"

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

bool test_shared_projected_unit_renderer_segment_scale_compensation_contract(
    std::string& outFail) {
    game::runtime::render_model::MeshData mesh;
    mesh.assetCacheIdentity = "segment-scale-compensation-test";
    mesh.nodesDefault.resize(2u);
    mesh.nodeNames = {"scaled_parent", "compensated_child"};
    mesh.nodeChildren = {{1}, {}};
    mesh.nodeParent = {-1, 0};
    mesh.sceneRoots = {0};
    mesh.nodesDefault[0].s = glm::vec3(2.0f, 1.0f, 1.0f);
    mesh.nodesDefault[1].t = glm::vec3(1.0f, 0.0f, 0.0f);

    auto ordinary =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                mesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored);
    if (!expect(
            ordinary.nodeGlobals.size() == 2u &&
                std::abs(ordinary.nodeGlobals[1][3].x - 2.0f) < 0.0001f,
            "An ordinary child should inherit its parent's authored scale.",
            outFail)) {
        return false;
    }

    mesh.assetCacheIdentity += ":ssc";
    mesh.nodesDefault[1].segmentScaleCompensate = true;
    const auto compensated =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                mesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored);
    if (!expect(
            compensated.nodeGlobals.size() == 2u &&
                std::abs(compensated.nodeGlobals[1][3].x - 1.0f) < 0.0001f,
            "A segment-scale-compensated child must cancel the immediate parent's local scale.",
            outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData alternateWing;
    alternateWing.assetCacheIdentity =
        "segment-scale-compensation-test:alternate-wing";
    alternateWing.nodesDefault.resize(3u);
    alternateWing.nodeNames = {
        "spine_02",
        "left_wing_a_01",
        "left_wing_a_02",
    };
    alternateWing.nodeChildren = {{1}, {2}, {}};
    alternateWing.nodeParent = {-1, 0, 1};
    alternateWing.sceneRoots = {0};
    alternateWing.nodesDefault[1].t = glm::vec3(1.0f, 0.0f, 0.0f);
    alternateWing.nodesDefault[1].s = glm::vec3(0.05f);
    alternateWing.nodesDefault[1].segmentScaleCompensate = true;
    alternateWing.nodesDefault[2].t = glm::vec3(1.0f, 0.0f, 0.0f);
    alternateWing.nodesDefault[2].s = glm::vec3(0.05f);
    alternateWing.nodesDefault[2].segmentScaleCompensate = true;

    const auto collapsed =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                alternateWing,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored);
    return expect(
        collapsed.nodeGlobals.size() == 3u &&
            std::abs(collapsed.nodeGlobals[2][3].x - 1.05f) < 0.0001f,
        "An alternate native wing chain must inherit its parent's concealment scale so inactive feathers collapse at the wing root.",
        outFail);
}

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
    loopClip.channels[0].path = engine::render::model_types::ChannelPath::Translation;

    game::runtime::shared_backend_pose::PoseEval blendedPose;
    game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
        blendedMesh,
        0,
        0.75f,
        game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
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

    // Scarlet locomotion stores world travel on the named `origin` joint,
    // below the top skin joint. Runtime in-place evaluation must suppress that
    // travel without flattening pose motion on descendants such as `waist`.
    game::runtime::render_model::MeshData scarletLikeMesh;
    scarletLikeMesh.assetCacheIdentity = "test:scarlet-origin-root-motion";
    scarletLikeMesh.nodesDefault.resize(4u);
    scarletLikeMesh.nodeNames = {
        "scene",
        "pm0001_00_00",
        "origin",
        "waist",
    };
    scarletLikeMesh.nodeParent = {-1, 0, 1, 2};
    scarletLikeMesh.nodeChildren = {{1}, {2}, {3}, {}};
    scarletLikeMesh.sceneRoots = {0};
    scarletLikeMesh.skins.resize(1u);
    scarletLikeMesh.skins[0].joints = {1, 2, 3};
    scarletLikeMesh.animations.resize(1u);
    auto& scarletClip = scarletLikeMesh.animations[0];
    scarletClip.durationSec = 1.0f;
    scarletClip.samplers.resize(2u);
    scarletClip.channels.resize(2u);
    scarletClip.samplers[0].inputs = {0.0f, 1.0f};
    scarletClip.samplers[0].outputs = {
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(2.0f, 0.5f, 4.0f, 0.0f),
    };
    scarletClip.channels[0].samplerIndex = 0;
    scarletClip.channels[0].targetNode = 2;
    scarletClip.channels[0].path =
        engine::render::model_types::ChannelPath::Translation;
    scarletClip.samplers[1].inputs = {0.0f, 1.0f};
    scarletClip.samplers[1].outputs = {
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 2.0f, 0.6f, 0.0f),
    };
    scarletClip.channels[1].samplerIndex = 1;
    scarletClip.channels[1].targetNode = 3;
    scarletClip.channels[1].path =
        engine::render::model_types::ChannelPath::Translation;

    const auto authoredRootMotionPose =
        game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
            scarletLikeMesh,
            0,
            0.5f,
            game::runtime::shared_backend_pose::RootMotionPolicy::PreserveAuthored,
            false);
    if (!expect(
            std::abs(authoredRootMotionPose.nodeLocals[2].t.x - 1.0f) < 0.001f &&
                std::abs(authoredRootMotionPose.nodeLocals[2].t.y - 0.25f) < 0.001f &&
                std::abs(authoredRootMotionPose.nodeLocals[2].t.z - 2.0f) < 0.001f,
            "PreserveAuthored must leave Scarlet origin-joint root motion unchanged.",
            outFail)) {
        return false;
    }

    const auto inPlaceHorizontalPose =
        game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
            scarletLikeMesh,
            0,
            0.5f,
            game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
            false);
    if (!expect(
            std::abs(inPlaceHorizontalPose.nodeLocals[2].t.x) < 0.001f &&
                std::abs(inPlaceHorizontalPose.nodeLocals[2].t.y - 0.25f) < 0.001f &&
                std::abs(inPlaceHorizontalPose.nodeLocals[2].t.z) < 0.001f,
            "InPlaceHorizontal must remove Scarlet origin-joint X/Z travel while retaining authored vertical motion.",
            outFail)) {
        return false;
    }
    if (!expect(
            std::abs(inPlaceHorizontalPose.nodeLocals[3].t.y - 1.5f) < 0.001f &&
                std::abs(inPlaceHorizontalPose.nodeLocals[3].t.z - 0.3f) < 0.001f,
            "In-place root-motion filtering must preserve descendant pose animation.",
            outFail)) {
        return false;
    }

    const auto fullyInPlacePose =
        game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
            scarletLikeMesh,
            0,
            0.5f,
            game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceAll,
            false);
    if (!expect(glm::length(fullyInPlacePose.nodeLocals[2].t) < 0.001f,
                "InPlaceAll must restore the complete origin-joint bind translation.",
                outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData replacedMesh;
    replacedMesh.assetCacheIdentity = "preview:first";
    replacedMesh.nodesDefault.resize(1u);
    replacedMesh.nodesDefault[0].t =
        glm::vec3(1.0f, 0.0f, 0.0f);
    const auto firstReplacementPose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                replacedMesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
                true);
    if (!expect(
            firstReplacementPose.nodeGlobals.size() == 1u &&
                std::abs(
                    firstReplacementPose.nodeGlobals[0][3].x -
                    1.0f) <
                    0.001f,
            "The first mutable mesh pose should use its authored bind transform.",
            outFail)) {
        return false;
    }

    replacedMesh =
        game::runtime::render_model::MeshData{};
    replacedMesh.assetCacheIdentity = "preview:second";
    replacedMesh.nodesDefault.resize(1u);
    replacedMesh.nodesDefault[0].t =
        glm::vec3(2.0f, 0.0f, 0.0f);
    const auto secondReplacementPose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                replacedMesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
                true);
    if (!expect(
            secondReplacementPose.nodeGlobals.size() == 1u &&
                std::abs(
                    secondReplacementPose.nodeGlobals[0][3].x -
                    2.0f) <
                    0.001f,
            "Replacing a mesh at the same address must invalidate cached bind-pose data.",
            outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData continuousOverlayMesh;
    continuousOverlayMesh.assetCacheIdentity =
        "test:continuous-native-overlay";
    continuousOverlayMesh.nodesDefault.resize(3u);
    continuousOverlayMesh.nodesDefault[0].t =
        glm::vec3(3.0f, 0.0f, 0.0f);
    continuousOverlayMesh.nodesDefault[1].t =
        glm::vec3(5.0f, 0.0f, 0.0f);
    continuousOverlayMesh.nodeNames = {
        "body_root", "left_arm", "fire_joint"};
    continuousOverlayMesh.nodeParent = {-1, 0, 0};
    continuousOverlayMesh.nodeChildren = {{1, 2}, {}, {}};
    continuousOverlayMesh.sceneRoots = {0};
    continuousOverlayMesh.vertices.resize(3u);
    for (auto& vertex : continuousOverlayMesh.vertices) {
        vertex.j0 = 2u;
        vertex.w0 = 1.0f;
    }
    continuousOverlayMesh.indices = {0u, 1u, 2u};
    continuousOverlayMesh.submeshIndexOffset = {0u};
    continuousOverlayMesh.submeshIndexCount = {3u};
    continuousOverlayMesh.submeshMaterialModes = {
        game::runtime::render_model::
            kNativeLayeredUnlitMaterialMode};
    continuousOverlayMesh.triangleSkinIndex = {0};
    continuousOverlayMesh.skins.resize(1u);
    continuousOverlayMesh.skins[0].joints = {0, 1, 2};
    continuousOverlayMesh.skins[0].inverseBind.assign(
        3u,
        glm::mat4(1.0f));
    continuousOverlayMesh.animations.resize(1u);
    auto& fireOverlay = continuousOverlayMesh.animations[0];
    fireOverlay.name = "pm0077_00_00_08201_loop01_loop";
    fireOverlay.durationSec = 1.0f;
    fireOverlay.samplers.resize(1u);
    fireOverlay.channels.resize(2u);
    fireOverlay.samplers[0].inputs = {
        0.0f,
        0.25f,
        0.75f,
        1.0f};
    fireOverlay.samplers[0].outputs = {
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(2.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
    };
    fireOverlay.channels[0].samplerIndex = 0;
    fireOverlay.channels[0].targetNode = 2;
    fireOverlay.channels[0].path =
        engine::render::model_types::ChannelPath::Translation;
    fireOverlay.channels[1].samplerIndex = 0;
    fireOverlay.channels[1].targetNode = 1;
    fireOverlay.channels[1].path =
        engine::render::model_types::ChannelPath::Translation;

    auto continuousPose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                continuousOverlayMesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored,
                true);
    if (!expect(
            game::runtime::shared_backend_pose::
                applyContinuousNativeOverlay(
                    continuousOverlayMesh,
                    0.25f,
                    continuousPose),
            "A native fire clip must be discovered and applied as an always-running overlay.",
            outFail)) {
        return false;
    }
    if (!expect(
            std::abs(continuousPose.nodeLocals[0].t.x - 3.0f) <
                    0.001f &&
                std::abs(continuousPose.nodeLocals[1].t.x - 5.0f) <
                    0.001f &&
                std::abs(continuousPose.nodeLocals[2].t.x - 1.0f) <
                    0.001f &&
                std::abs(continuousPose.nodeGlobals[2][3].x - 4.0f) <
                    0.001f,
            "The continuous fire overlay must preserve body-limb animation while updating only its authored fire branch and rebuilt global transform.",
            outFail)) {
        outFail +=
            " root=" +
            std::to_string(continuousPose.nodeLocals[0].t.x) +
            " armLocal=" +
            std::to_string(continuousPose.nodeLocals[1].t.x) +
            " fireLocal=" +
            std::to_string(continuousPose.nodeLocals[2].t.x) +
            " fireGlobal=" +
            std::to_string(continuousPose.nodeGlobals[2][3].x);
        return false;
    }

    auto preLoopPose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                continuousOverlayMesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored,
                true);
    auto postLoopPose = preLoopPose;
    game::runtime::shared_backend_pose::applyContinuousNativeOverlay(
        continuousOverlayMesh,
        0.999f,
        preLoopPose);
    game::runtime::shared_backend_pose::applyContinuousNativeOverlay(
        continuousOverlayMesh,
        1.001f,
        postLoopPose);
    if (!expect(
            glm::length(
                preLoopPose.nodeLocals[2].t -
                postLoopPose.nodeLocals[2].t) < 0.01f,
            "The source-authored continuous fire overlay must remain continuous across its loop boundary.",
            outFail)) {
        return false;
    }

    auto referencePoseOverlayMesh = continuousOverlayMesh;
    referencePoseOverlayMesh.assetCacheIdentity =
        "test:constant-native-overlay";
    for (auto& output :
         referencePoseOverlayMesh.animations[0].samplers[0].outputs) {
        output = glm::vec4(9.0f, 0.0f, 0.0f, 0.0f);
    }
    auto referencePose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                referencePoseOverlayMesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored,
                true);
    if (!expect(
            !game::runtime::shared_backend_pose::
                 applyContinuousNativeOverlay(
                     referencePoseOverlayMesh,
                     0.25f,
                     referencePose) &&
                std::abs(referencePose.nodeLocals[2].t.x) <
                    0.001f,
            "A constant loop01 reference pose must not overwrite the selected body animation; its continuous material tracks run separately.",
            outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData materialTrackMesh;
    game::runtime::render_model::ContinuousMaterialAnimationTrack
        baseUvTrack;
    baseUvTrack.submeshIndex = 3u;
    baseUvTrack.parameter = game::runtime::render_model::
        MaterialAnimationParameter::UvScaleOffset;
    baseUvTrack.durationSec = 1.0f;
    baseUvTrack.sourceFrameRate = 60.0f;
    baseUvTrack.loop = true;
    baseUvTrack.defaultValue = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    baseUvTrack.components[2].keys = {
        {0.0f, 1.0f},
        {0.5f, 0.0f},
        {1.0f, 1.0f},
    };
    materialTrackMesh.continuousMaterialAnimations.push_back(
        baseUvTrack);
    game::runtime::render_model::ContinuousMaterialAnimationTrack
        displacementUvTrack;
    displacementUvTrack.submeshIndex = 3u;
    displacementUvTrack.parameter = game::runtime::render_model::
        MaterialAnimationParameter::UvScaleOffset3;
    displacementUvTrack.durationSec = 1.0f;
    displacementUvTrack.sourceFrameRate = 60.0f;
    displacementUvTrack.loop = true;
    displacementUvTrack.defaultValue =
        glm::vec4(1.25f, 0.75f, 0.125f, 0.0f);
    displacementUvTrack.components[3].keys = {
        {0.0f, 0.0f},
        {0.5f, 1.0f},
        {1.0f, 0.0f},
    };
    materialTrackMesh.continuousMaterialAnimations.push_back(
        displacementUvTrack);
    game::runtime::render_model::ContinuousMaterialAnimationTrack
        periodicResetTrack;
    periodicResetTrack.submeshIndex = 4u;
    periodicResetTrack.parameter = game::runtime::render_model::
        MaterialAnimationParameter::UvScaleOffset;
    periodicResetTrack.durationSec = 1.0f;
    periodicResetTrack.sourceFrameRate = 60.0f;
    periodicResetTrack.loop = true;
    periodicResetTrack.defaultValue =
        glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    periodicResetTrack.components[2].keys = {
        {0.0f, 1.0f},
        {59.0f / 60.0f, 0.0f},
        {1.0f, 1.0f},
    };
    materialTrackMesh.continuousMaterialAnimations.push_back(
        periodicResetTrack);

    glm::vec4 baseUvValue{0.0f};
    glm::vec4 displacementUvValue{0.0f};
    glm::vec4 periodicResetValue{0.0f};
    const bool sampledBase = game::runtime::
        shared_projected_unit_backend_mesh_prep::detail::
            sampleContinuousMaterialAnimation(
                materialTrackMesh,
                3u,
                game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset,
                1.25f,
                baseUvValue);
    const bool sampledDisplacement = game::runtime::
        shared_projected_unit_backend_mesh_prep::detail::
            sampleContinuousMaterialAnimation(
                materialTrackMesh,
                3u,
                game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset3,
                0.25f,
                displacementUvValue);
    const bool sampledPeriodicReset = game::runtime::
        shared_projected_unit_backend_mesh_prep::detail::
            sampleContinuousMaterialAnimation(
                materialTrackMesh,
                4u,
                game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset,
                59.5f / 60.0f,
                periodicResetValue);
    if (!expect(
            sampledBase && sampledDisplacement && sampledPeriodicReset &&
                std::abs(baseUvValue.x - 1.0f) < 0.001f &&
                std::abs(baseUvValue.y - 1.0f) < 0.001f &&
                std::abs(baseUvValue.z - 0.5f) < 0.001f &&
                std::abs(baseUvValue.w) < 0.001f &&
                std::abs(displacementUvValue.x - 1.25f) < 0.001f &&
                std::abs(displacementUvValue.y - 0.75f) < 0.001f &&
                std::abs(displacementUvValue.z - 0.125f) < 0.001f &&
                std::abs(displacementUvValue.w - 0.5f) < 0.001f &&
                std::abs(periodicResetValue.z) < 0.001f,
            "Exact native material playback must wrap by source duration, interpolate retained keys, preserve periodic one-frame UV resets, and leave unauthored axes at their source defaults.",
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
                game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
                true);
        const auto startPose =
            game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
                mesh,
                animIndex,
                0.0f,
                game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
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
            engine::paths::asset("models/0014_Kakuna_ZA.phmodel"),
            engine::paths::asset("models/0014_Kakuna_ZA.animset.json"),
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
                game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
                true);
        const auto postWrapPose =
            game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
                mesh,
                animIndex,
                postWrapTime,
                game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
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

