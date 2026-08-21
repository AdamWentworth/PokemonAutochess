#include "game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.h"

#include "engine/core/Paths.h"
#include "game/config/AnimSetLoader.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "engine/runtime/FixedStep.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCachedIndexedBatches.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"

#include <cmath>
#include <filesystem>
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

bool test_shared_projected_unit_renderer_cached_batch_material_identity_contract(
    std::string& outFail) {
    namespace cached = game::runtime::
        shared_projected_unit_backend_mesh_cached_indexed_batches;
    namespace support = game::runtime::
        shared_projected_unit_backend_mesh_support;
    namespace world = game::runtime::shared_world_batches;

    game::runtime::render_model::MeshData mesh;
    PokemonInstance unit{};
    unit.id = 17;
    game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState prep;
    prep.modelM = glm::mat4(1.0f);
    game::runtime::shared_projected_unit_backend_mesh_transforms::Resolver transforms;
    std::vector<glm::mat4> nodeGlobals;

    support::FastTexturedMeshTemplateCache fastCache;
    fastCache.batches.resize(2u);
    fastCache.batches[0].baseSubmeshIndex = 2u;
    fastCache.batches[0].geometryCacheKey = "visible-eye-geometry";
    fastCache.batches[1].baseSubmeshIndex = 4u;
    fastCache.batches[1].geometryCacheKey = "visible-body-geometry";
    for (auto& batch : fastCache.batches) {
        batch.gpuTemplateVertices.resize(3u);
        batch.indices = {0u, 1u, 2u};
    }

    std::vector<world::WorldIndexedBatch> sharedMaterials(6u);
    std::vector<world::WorldIndexedBatch> workingMaterials(6u);
    for (std::size_t index = 0u; index < sharedMaterials.size(); ++index) {
        sharedMaterials[index].materialMode = static_cast<std::uint8_t>(10u + index);
        sharedMaterials[index].textureKey = "material-" + std::to_string(index);
        workingMaterials[index].sharedTemplate = &sharedMaterials[index];
    }
    // Dynamic eye animation detaches its material from the immutable template.
    workingMaterials[2] = sharedMaterials[2];
    workingMaterials[2].sharedTemplate = nullptr;
    workingMaterials[2].materialMode =
        game::runtime::render_model::kNativeAnimatedEyeMaterialMode;
    workingMaterials[2].lightProjectionUvRowU = {1.0f, 1.0f, 0.25f, 0.5f};

    std::vector<std::uint8_t> gpuPaletteFlags;
    bool poseHashReady = false;
    std::uint64_t poseHash = 0u;
    const auto result = cached::buildCachedIndexedBatches(
        {
            .unit = &unit,
            .mesh = &mesh,
            .prep = &prep,
            .transforms = &transforms,
            .nodeGlobals = &nodeGlobals,
            .fastCache = &fastCache,
            .enableGpuClipSkinning = false,
            .cpuRewritePoseHashReady = &poseHashReady,
            .cpuRewritePoseHash = &poseHash,
            .modelIndexedBatchesPerSubmesh = &workingMaterials,
            .batchUsesGpuClipPalette = &gpuPaletteFlags,
        });

    if (!expect(
            result.handled && workingMaterials.size() == 2u,
            "Cached indexed batches should compact to the visible geometry-cache batches.",
            outFail)) {
        return false;
    }
    const auto& eye = world::resolvedMaterialBatch(workingMaterials[0]);
    const auto& body = world::resolvedMaterialBatch(workingMaterials[1]);
    return expect(
        workingMaterials[0].geometryCacheKey == "visible-eye-geometry" &&
            workingMaterials[1].geometryCacheKey == "visible-body-geometry" &&
            eye.materialMode ==
                game::runtime::render_model::kNativeAnimatedEyeMaterialMode &&
            eye.textureKey == "material-2" &&
            std::abs(eye.lightProjectionUvRowU[2] - 0.25f) < 0.0001f &&
            body.materialMode == 14u &&
            body.textureKey == "material-4",
        "Geometry-cache compaction must resolve material state by baseSubmeshIndex; hidden lower submeshes must not shift eye materials onto body geometry.",
        outFail);
}

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
    if (!expect(
            collapsed.nodeGlobals.size() == 3u &&
                std::abs(collapsed.nodeGlobals[2][3].x - 1.05f) < 0.0001f,
            "An alternate native wing chain must inherit its parent's concealment scale so inactive feathers collapse at the wing root.",
            outFail)) {
        return false;
    }

    // LGPE uses ordinary arm names for the alternate folded-wing chain. The
    // repeated source-authored 0.05 scale, rather than a species-specific name,
    // must carry the same concealment meaning.
    alternateWing.assetCacheIdentity =
        "segment-scale-compensation-test:lgpe-folded-wing";
    alternateWing.nodeNames = {"Spine", "LArm", "LForeArm"};
    const auto lgpeCollapsed =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                alternateWing,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored);
    return expect(
        lgpeCollapsed.nodeGlobals.size() == 3u &&
            std::abs(lgpeCollapsed.nodeGlobals[2][3].x - 1.05f) < 0.0001f,
        "An LGPE folded-wing chain must preserve repeated concealment scales through segment-scale-compensated children.",
        outFail);
}

bool test_shared_projected_unit_renderer_facial_overlay_base_pose_contract(
    std::string& outFail) {
    using engine::render::model_types::AnimationChannel;
    using engine::render::model_types::AnimationClip;
    using engine::render::model_types::AnimationSampler;
    using engine::render::model_types::ChannelPath;

    game::runtime::render_model::MeshData mesh;
    mesh.assetCacheIdentity = "facial-overlay-base-pose-test";
    mesh.nodesDefault.resize(3u);
    mesh.nodeNames = {"root", "tongue_05", "left_eyelid"};
    mesh.nodeChildren = {{1, 2}, {}, {}};
    mesh.nodeParent = {-1, 0, 0};
    mesh.sceneRoots = {0};
    mesh.submeshMaterialModes = {
        game::runtime::render_model::kNativeFacialOverlayMaterialMode};
    mesh.submeshMaterialFlags = {4.0f};
    // Gastly's native bind pose deliberately presents the tongue. Ordinary
    // body clips fold it away; a partial eyelid clip must inherit that body.
    mesh.nodesDefault[1].t = glm::vec3(4.0f, 0.0f, 0.0f);

    const auto translationClip = [](
        const char* name,
        int targetNode,
        const glm::vec3& translation) {
        AnimationClip clip;
        clip.name = name;
        clip.durationSec = 1.0f;
        AnimationSampler sampler;
        sampler.inputs = {0.0f, 1.0f};
        sampler.outputs = {
            glm::vec4(translation, 0.0f),
            glm::vec4(translation, 0.0f),
        };
        sampler.interpolation = "LINEAR";
        clip.samplers.push_back(std::move(sampler));
        AnimationChannel channel;
        channel.samplerIndex = 0;
        channel.targetNode = targetNode;
        channel.path = ChannelPath::Translation;
        clip.channels.push_back(channel);
        return clip;
    };

    auto concealedWait = translationClip(
        "pm0092_00_00_20000_defaultwait01_loop",
        1,
        glm::vec3(1.0f, 0.0f, 0.0f));
    AnimationSampler concealmentScale;
    concealmentScale.inputs = {0.0f, 1.0f};
    concealmentScale.outputs = {
        glm::vec4(0.1f, 0.1f, 0.1f, 0.0f),
        glm::vec4(0.1f, 0.1f, 0.1f, 0.0f),
    };
    concealmentScale.interpolation = "LINEAR";
    concealedWait.samplers.push_back(std::move(concealmentScale));
    AnimationChannel concealmentChannel;
    concealmentChannel.samplerIndex = 1;
    concealmentChannel.targetNode = 1;
    concealmentChannel.path = ChannelPath::Scale;
    concealedWait.channels.push_back(concealmentChannel);
    mesh.animations.push_back(std::move(concealedWait));
    mesh.animations.push_back(translationClip(
        "pm0092_00_00_28000_eye01",
        2,
        glm::vec3(0.0f, 2.0f, 0.0f)));
    mesh.animations.push_back(translationClip(
        "pm0092_00_00_20310_appeal01",
        1,
        glm::vec3(6.0f, 0.0f, 0.0f)));

    const auto bindPose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                mesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored,
                false);
    if (!expect(
            !bindPose.hasClipPose &&
                game::runtime::shared_backend_pose::
                    isTongueSurfaceConcealed(mesh, bindPose),
            "Gastly's authoring bind pose must conceal its packed tongue surface until an authored reveal clip is selected.",
            outFail)) {
        return false;
    }

    const auto eyePose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                mesh,
                1,
                0.25f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored,
                false);
    if (!expect(
            eyePose.nodeGlobals.size() == 3u &&
                std::abs(eyePose.nodeGlobals[1][3].x - 1.0f) < 0.0001f &&
                glm::length(glm::vec3(eyePose.nodeGlobals[1][0])) <
                    0.0001f &&
                std::abs(eyePose.nodeGlobals[2][3].y - 2.0f) < 0.0001f,
            "A partial native facial clip must animate its eyelid over the wait body and fully collapse the source-concealed tongue.",
            outFail)) {
        return false;
    }
    if (!expect(
            game::runtime::shared_backend_pose::isTongueSurfaceConcealed(
                mesh,
                eyePose),
            "A partial native facial clip must publish tongue-surface concealment from its inherited wait pose.",
            outFail)) {
        return false;
    }

    const auto appealPose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                mesh,
                2,
                0.25f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored,
                false);
    return expect(
        appealPose.nodeGlobals.size() == 3u &&
            std::abs(appealPose.nodeGlobals[1][3].x - 6.0f) < 0.0001f &&
            std::abs(
                glm::length(glm::vec3(appealPose.nodeGlobals[1][0])) -
                1.0f) < 0.0001f,
        "A complete tongue presentation clip must retain its authored extension rather than inheriting the facial-overlay base.",
        outFail) &&
        expect(
            !game::runtime::shared_backend_pose::isTongueSurfaceConcealed(
                mesh,
                appealPose),
            "An authored tongue presentation must disable the packed tongue-surface concealment guard.",
            outFail);
}

bool test_shared_projected_unit_renderer_gastly_tongue_timeline_contract(
    std::string& outFail) {
    const std::string modelPath =
        engine::paths::asset("models/0092_Gastly_ZA.phmodel");
    game::runtime::render_model::MeshData mesh;
    std::string meshError;
    if (!game::runtime::render_model::loadMeshFromCache(
            modelPath,
            mesh,
            &meshError)) {
        outFail = "Failed to load Gastly for tongue timeline audit: " +
            meshError;
        return false;
    }

    bool sawEatReveal = false;
    bool sawAppealReveal = false;
    for (std::size_t animationIndex = 0u;
         animationIndex < mesh.animations.size();
         ++animationIndex) {
        const auto& clip = mesh.animations[animationIndex];
        std::string clipName = clip.name;
        std::transform(
            clipName.begin(),
            clipName.end(),
            clipName.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        const bool authoredTongueReveal =
            clipName.find("eat02_end") != std::string::npos ||
            clipName.find("appeal01") != std::string::npos;
        const int finalFrame =
            std::max(1, static_cast<int>(std::ceil(clip.durationSec * 60.0f)));
        bool previousConcealed = true;
        bool havePrevious = false;
        int visibilityTransitions = 0;
        bool sawConcealed = false;
        bool sawVisible = false;
        for (int frame = 0; frame <= finalFrame; ++frame) {
            const float sampleTime = std::min(
                static_cast<float>(frame) / 60.0f,
                clip.durationSec);
            const auto pose = game::runtime::shared_backend_pose::
                evaluateScenePoseForResolvedClipTime(
                    mesh,
                    static_cast<int>(animationIndex),
                    sampleTime,
                    game::runtime::shared_backend_pose::
                        RootMotionPolicy::PreserveAuthored,
                    false);
            const bool concealed =
                game::runtime::shared_backend_pose::
                    isTongueSurfaceConcealed(mesh, pose);
            sawConcealed = sawConcealed || concealed;
            sawVisible = sawVisible || !concealed;
            if (havePrevious && concealed != previousConcealed) {
                ++visibilityTransitions;
            }
            previousConcealed = concealed;
            havePrevious = true;

            if (!authoredTongueReveal && !concealed) {
                outFail = "Gastly tongue surface became visible in non-tongue clip '" +
                    clip.name + "' at source frame " + std::to_string(frame) + ".";
                return false;
            }
        }

        if (authoredTongueReveal) {
            if (!expect(
                    sawConcealed && sawVisible && visibilityTransitions == 2,
                    "Gastly authored tongue clip '" + clip.name +
                        "' must have one contiguous reveal window; concealed=" +
                        std::to_string(sawConcealed) + " visible=" +
                        std::to_string(sawVisible) + " transitions=" +
                        std::to_string(visibilityTransitions),
                    outFail)) {
                return false;
            }
            sawEatReveal = sawEatReveal ||
                clipName.find("eat02_end") != std::string::npos;
            sawAppealReveal = sawAppealReveal ||
                clipName.find("appeal01") != std::string::npos;
        }
    }

    return expect(
        sawEatReveal && sawAppealReveal,
        "Gastly's cooked animation set must retain both source-authored tongue reveal clips.",
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

    // LGPE airborne field locomotion uses Waist beneath Origin as a second
    // displacement carrier. The source data stays immutable, while in-place
    // runtime evaluation restores Waist to bind so game-owned flyer height is
    // not added on top of the source clip's large altitude offset.
    game::runtime::render_model::MeshData lgpeFlyerMesh;
    lgpeFlyerMesh.assetCacheIdentity = "test:lgpe-waist-root-motion";
    lgpeFlyerMesh.nodesDefault.resize(4u);
    lgpeFlyerMesh.nodesDefault[3].t = glm::vec3(0.0f, 12.0f, -7.0f);
    lgpeFlyerMesh.nodeNames = {
        "scene",
        "pm0021_00",
        "Origin",
        "Waist",
    };
    lgpeFlyerMesh.nodeParent = {-1, 0, 1, 2};
    lgpeFlyerMesh.nodeChildren = {{1}, {2}, {3}, {}};
    lgpeFlyerMesh.sceneRoots = {0};
    lgpeFlyerMesh.skins.resize(1u);
    lgpeFlyerMesh.skins[0].joints = {1, 2, 3};
    lgpeFlyerMesh.animations.resize(2u);
    for (auto& clip : lgpeFlyerMesh.animations) {
        clip.durationSec = 1.0f;
        clip.samplers.resize(1u);
        clip.channels.resize(1u);
        clip.samplers[0].inputs = {0.0f, 1.0f};
        clip.samplers[0].outputs = {
            glm::vec4(0.0f, 60.0f, -7.0f, 0.0f),
            glm::vec4(2.0f, 70.0f, 3.0f, 0.0f),
        };
        clip.channels[0].samplerIndex = 0;
        clip.channels[0].targetNode = 3;
        clip.channels[0].path =
            engine::render::model_types::ChannelPath::Translation;
    }
    lgpeFlyerMesh.animations[0].name = "pm0021_00_fi01_wait01";
    lgpeFlyerMesh.animations[1].name = "pm0021_00_ba10_waitA01";

    const auto authoredRoute1FieldPose =
        game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
            lgpeFlyerMesh,
            0,
            0.5f,
            game::runtime::shared_backend_pose::RootMotionPolicy::PreserveAuthored,
            false);
    if (!expect(
            std::abs(authoredRoute1FieldPose.nodeLocals[3].t.y - 65.0f) < 0.001f,
            "PreserveAuthored must retain LGPE's source Waist displacement.",
            outFail)) {
        return false;
    }
    const auto inPlaceRoute1FieldPose =
        game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
            lgpeFlyerMesh,
            0,
            0.5f,
            game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
            false);
    if (!expect(
            glm::length(
                inPlaceRoute1FieldPose.nodeLocals[3].t -
                lgpeFlyerMesh.nodesDefault[3].t) < 0.001f,
            "In-place LGPE field locomotion must restore Waist translation to bind.",
            outFail)) {
        return false;
    }
    const auto inPlaceLgpeBattlePose =
        game::runtime::shared_backend_pose::evaluateScenePoseForResolvedClipTime(
            lgpeFlyerMesh,
            1,
            0.5f,
            game::runtime::shared_backend_pose::RootMotionPolicy::InPlaceHorizontal,
            false);
    if (!expect(
            std::abs(inPlaceLgpeBattlePose.nodeLocals[3].t.y - 65.0f) < 0.001f,
            "In-place evaluation must preserve authored LGPE Waist pose outside field locomotion clips.",
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

    auto svContinuousOverlayMesh = continuousOverlayMesh;
    svContinuousOverlayMesh.assetCacheIdentity =
        "test:sv-continuous-native-overlay";
    svContinuousOverlayMesh.animations[0].name =
        "pm0110_00_00_28201_loop01_loop";
    auto svContinuousPose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                svContinuousOverlayMesh,
                -1,
                0.0f,
                game::runtime::shared_backend_pose::
                    RootMotionPolicy::PreserveAuthored,
                true);
    if (!expect(
            game::runtime::shared_backend_pose::
                    applyContinuousNativeOverlay(
                        svContinuousOverlayMesh,
                        0.25f,
                        svContinuousPose) &&
                std::abs(svContinuousPose.nodeLocals[2].t.x - 1.0f) <
                    0.001f,
            "SV's 28201 geometry controller must use the same continuous "
            "skeletal overlay path as an 08201 fire controller.",
            outFail)) {
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

    materialTrackMesh.animationMaterialParameters.resize(2u);
    auto clipEyeTrack = baseUvTrack;
    clipEyeTrack.submeshIndex = 7u;
    clipEyeTrack.sampling = game::runtime::render_model::
        MaterialAnimationSampling::HoldSourceFrame;
    clipEyeTrack.defaultValue =
        glm::vec4(2.0f, 4.0f, 0.0f, 0.0f);
    clipEyeTrack.components[0].keys = {
        {0.0f, 2.0f},
        {1.0f, 2.0f},
    };
    clipEyeTrack.components[1].keys = {
        {0.0f, 4.0f},
        {1.0f, 4.0f},
    };
    clipEyeTrack.components[2].keys = {
        {0.0f, 0.0f},
        {0.5f, 0.5f},
        {1.0f, 0.0f},
    };
    clipEyeTrack.components[3].keys = {
        {0.0f, 0.0f},
        {0.5f, 0.25f},
        {1.0f, 0.0f},
    };
    materialTrackMesh.animationMaterialParameters[1].push_back(
        clipEyeTrack);
    glm::vec4 clipEyeValue{0.0f};
    glm::vec4 wrongClipValue{0.0f};
    const bool sampledClipEye = game::runtime::
        shared_projected_unit_backend_mesh_prep::detail::
            sampleClipBoundMaterialAnimation(
                materialTrackMesh,
                1,
                7u,
                game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset,
                0.25f,
                clipEyeValue);
    const bool sampledWrongClip = game::runtime::
        shared_projected_unit_backend_mesh_prep::detail::
            sampleClipBoundMaterialAnimation(
                materialTrackMesh,
                0,
                7u,
                game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset,
                0.25f,
                wrongClipValue);
    materialTrackMesh.animationMaterialParameters[1][0].sampling =
        game::runtime::render_model::
            MaterialAnimationSampling::Linear;
    glm::vec4 genericClipEyeValue{0.0f};
    const bool sampledGenericClipEye = game::runtime::
        shared_projected_unit_backend_mesh_prep::detail::
            sampleClipBoundMaterialAnimation(
                materialTrackMesh,
                1,
                7u,
                game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset,
                0.25f,
                genericClipEyeValue);
    if (!expect(
            sampledClipEye && !sampledWrongClip &&
                sampledGenericClipEye &&
                std::abs(clipEyeValue.x - 2.0f) < 0.001f &&
                std::abs(clipEyeValue.y - 4.0f) < 0.001f &&
                std::abs(clipEyeValue.z) < 0.001f &&
                std::abs(clipEyeValue.w) < 0.001f &&
                std::abs(genericClipEyeValue.z - 0.25f) < 0.001f &&
                std::abs(genericClipEyeValue.w - 0.125f) < 0.001f,
            "Rattata eye UV animation must hold its authored atlas cell between source frames, while unreviewed families retain interpolation.",
            outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData smokeVisibilityMesh;
    smokeVisibilityMesh.submeshMeshIndex = {0};
    smokeVisibilityMesh.meshIndexToNode = {2};
    smokeVisibilityMesh.submeshMaterialModes = {
        game::runtime::render_model::
            kNativeLayeredUnlitMaterialMode};
    smokeVisibilityMesh.submeshMaterialFlags = {3.0f};
    smokeVisibilityMesh.submeshAlphaMode = {2u};
    smokeVisibilityMesh.animationMeshVisibility.resize(1u);
    game::runtime::render_model::MeshVisibilityTrack smokeTrack;
    smokeTrack.nodeIndex = 2;
    smokeTrack.sourceFrameRate = 60.0f;
    smokeTrack.inputs = {
        0.0f,
        37.0f / 60.0f,
        53.0f / 60.0f};
    smokeTrack.values = {0u, 1u, 0u};
    smokeVisibilityMesh.animationMeshVisibility[0].push_back(
        smokeTrack);
    const auto sampleSmokeVisibility = [&](float timeSec) {
        return game::runtime::
            shared_projected_unit_backend_mesh_prep::detail::
                sampleNativeEffectVisibilityAlpha(
                    smokeVisibilityMesh,
                    0,
                    0u,
                    timeSec,
                    timeSec);
    };
    const float beforeSmoke = sampleSmokeVisibility(0.50f);
    const float risingBoundary =
        sampleSmokeVisibility(37.0f / 60.0f);
    const float firstVisibleSmoke =
        sampleSmokeVisibility(38.0f / 60.0f);
    const float visibleSmoke = sampleSmokeVisibility(0.70f);
    const float lastVisibleSmoke =
        sampleSmokeVisibility(52.0f / 60.0f);
    const float fallingBoundary =
        sampleSmokeVisibility(53.0f / 60.0f);
    const float afterSmoke = sampleSmokeVisibility(0.95f);
    smokeVisibilityMesh.animationMeshVisibility[0][0].inputs = {
        0.0f,
        1.0f,
        61.0f / 60.0f};
    smokeVisibilityMesh.animationMeshVisibility[0][0].values = {
        0u,
        1u,
        0u};
    const float singleFrameVisibility =
        sampleSmokeVisibility(60.5f / 60.0f);
    smokeVisibilityMesh.submeshAlphaMode[0] = 0u;
    const float opaqueNonEffect = sampleSmokeVisibility(0.50f);
    if (!expect(
            beforeSmoke < 0.001f &&
                risingBoundary > 0.999f &&
                firstVisibleSmoke > 0.999f &&
                visibleSmoke > 0.999f &&
                lastVisibleSmoke > 0.999f &&
                fallingBoundary < 0.001f &&
                afterSmoke < 0.001f &&
                singleFrameVisibility > 0.999f &&
                opaqueNonEffect > 0.999f,
            "SV SSSEffect visibility must preserve each authored puff gate exactly and leave opaque non-effect materials unchanged.",
            outFail)) {
        return false;
    }

    auto idleSmokeControllerMesh = smokeVisibilityMesh;
    idleSmokeControllerMesh.assetCacheIdentity =
        "test:idle-smoke-controller";
    idleSmokeControllerMesh.submeshAlphaMode[0] = 2u;
    idleSmokeControllerMesh.animations.resize(2u);
    idleSmokeControllerMesh.animations[0].name =
        "pm0110_00_00_20000_defaultwait01_loop";
    idleSmokeControllerMesh.animations[0].durationSec = 2.0f;
    idleSmokeControllerMesh.animations[1].name =
        "pm0110_00_00_28201_loop01_loop";
    idleSmokeControllerMesh.animations[1].durationSec = 1.0f;
    idleSmokeControllerMesh.animationMeshVisibility.resize(2u);
    auto hiddenIdleTrack = smokeTrack;
    hiddenIdleTrack.inputs = {0.0f};
    hiddenIdleTrack.values = {0u};
    idleSmokeControllerMesh.animationMeshVisibility[0] = {
        hiddenIdleTrack};
    auto controllerPuffTrack = smokeTrack;
    controllerPuffTrack.inputs = {0.0f, 0.25f, 0.75f};
    controllerPuffTrack.values = {0u, 1u, 0u};
    idleSmokeControllerMesh.animationMeshVisibility[1] = {
        controllerPuffTrack};
    const auto sampleIdleSmokeController =
        [&](float bodyTimeSec, float controllerTimeSec) {
            return game::runtime::
                shared_projected_unit_backend_mesh_prep::detail::
                    sampleNativeEffectVisibilityAlpha(
                        idleSmokeControllerMesh,
                        0,
                        0u,
                        bodyTimeSec,
                        controllerTimeSec);
        };
    const float idleControllerHidden =
        sampleIdleSmokeController(0.5f, 0.10f);
    const float idleControllerVisible =
        sampleIdleSmokeController(0.5f, 0.50f);
    const float idleControllerHiddenAgain =
        sampleIdleSmokeController(0.5f, 0.90f);
    const float idleControllerWrapped =
        sampleIdleSmokeController(0.5f, 1.50f);
    idleSmokeControllerMesh.animationMeshVisibility[0][0].inputs = {
        0.0f, 0.25f, 0.75f};
    idleSmokeControllerMesh.animationMeshVisibility[0][0].values = {
        0u, 1u, 0u};
    const float bodyLifecycleOverridesVisibleController =
        sampleIdleSmokeController(0.10f, 0.50f);
    const float bodyLifecycleOverridesHiddenController =
        sampleIdleSmokeController(0.50f, 0.90f);
    if (!expect(
            idleControllerHidden < 0.001f &&
                idleControllerVisible > 0.999f &&
                idleControllerHiddenAgain < 0.001f &&
                idleControllerWrapped > 0.999f &&
                bodyLifecycleOverridesVisibleController < 0.001f &&
                bodyLifecycleOverridesHiddenController > 0.999f,
            "A retained 28201 controller must cycle smoke over a hidden idle "
            "clip, while roar/attack clips with their own lifecycle retain "
            "exclusive control.",
            outFail)) {
        return false;
    }

    // When the locally cooked proprietary review asset is present, exercise
    // the same node/submesh mapping used by the inspector. The synthetic
    // contract above protects CI; this catches stale or malformed local cooks
    // before visual review.
    const std::filesystem::path weezingObjectRoot =
        "content/phlosion/objects";
    if (std::filesystem::exists(weezingObjectRoot)) {
        std::filesystem::path weezingObject;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(
                 weezingObjectRoot)) {
            if (entry.is_regular_file() &&
                entry.path().filename() == "0110_Weezing_SV.phlo") {
                weezingObject = entry.path();
                break;
            }
        }
        if (!weezingObject.empty()) {
            game::runtime::render_model::MeshData weezingMesh;
            std::string loadError;
            if (!expect(
                    game::runtime::phlosion::loadModelObject(
                        weezingObject.string(),
                        weezingMesh,
                        &loadError),
                    "Failed to load local Weezing smoke regression asset: " +
                        loadError,
                    outFail)) {
                return false;
            }
            const int roarIndex = resolveAnimIndex(
                weezingMesh,
                "pm0110_00_00_20300_roar01");
            std::size_t effectSubmeshes = 0u;
            std::vector<std::size_t> effectSubmeshIndices;
            for (std::size_t submeshIndex = 0u;
                 submeshIndex < weezingMesh.submeshMaterialModes.size();
                 ++submeshIndex) {
                const bool effect =
                    weezingMesh.submeshMaterialModes[submeshIndex] ==
                        game::runtime::render_model::
                            kNativeLayeredUnlitMaterialMode &&
                    submeshIndex <
                        weezingMesh.submeshMaterialFlags.size() &&
                    weezingMesh.submeshMaterialFlags[submeshIndex] > 2.5f &&
                    weezingMesh.submeshMaterialFlags[submeshIndex] < 3.5f;
                if (!effect) continue;
                ++effectSubmeshes;
                effectSubmeshIndices.push_back(submeshIndex);
            }
            if (!expect(
                    roarIndex >= 0 && effectSubmeshes >= 6u,
                    "Cooked Weezing smoke regression asset is missing roar01 "
                    "or its paired cloud/plume SSSEffect submeshes.",
                    outFail)) {
                return false;
            }

            const int idleIndex = resolveAnimIndex(
                weezingMesh,
                "pm0110_00_00_20000_defaultwait01_loop");
            const int controllerIndex = game::runtime::
                shared_backend_pose::
                    continuousNativeOverlayAnimationIndex(weezingMesh);
            if (!expect(
                    idleIndex >= 0 && controllerIndex >= 0 &&
                        static_cast<std::size_t>(controllerIndex) <
                            weezingMesh.animations.size() &&
                        weezingMesh.animations[
                            static_cast<std::size_t>(controllerIndex)]
                                .name.find("_28201_loop01_loop") !=
                            std::string::npos,
                    "Cooked Weezing is missing its retained 28201 idle-smoke "
                    "controller.",
                    outFail)) {
                return false;
            }
            struct IdleSmokeFrameExpectation {
                float controllerFrame;
                std::size_t visibleSubmeshes;
            };
            const std::array<IdleSmokeFrameExpectation, 10u>
                idleSmokeLifecycle = {{
                    {0.0f, 0u},
                    {10.0f, 2u},
                    {12.0f, 4u},
                    {41.0f, 2u},
                    {44.0f, 0u},
                    {72.0f, 4u},
                    {110.0f, 0u},
                    {132.0f, 4u},
                    {192.0f, 4u},
                    {230.0f, 0u},
                }};
            for (const auto& expectation : idleSmokeLifecycle) {
                std::size_t visibleSubmeshes = 0u;
                const float controllerTime =
                    expectation.controllerFrame / 60.0f;
                for (const std::size_t submeshIndex :
                     effectSubmeshIndices) {
                    const float alpha = game::runtime::
                        shared_projected_unit_backend_mesh_prep::detail::
                            sampleNativeEffectVisibilityAlpha(
                                weezingMesh,
                                idleIndex,
                                submeshIndex,
                                0.5f,
                                controllerTime);
                    if (alpha > 0.999f) ++visibleSubmeshes;
                }
                if (!expect(
                        visibleSubmeshes == expectation.visibleSubmeshes,
                        "Cooked Weezing idle smoke differs from its 28201 "
                        "controller at source frame " +
                            std::to_string(static_cast<int>(
                                expectation.controllerFrame)) +
                            " (expected " +
                            std::to_string(expectation.visibleSubmeshes) +
                            ", found " +
                            std::to_string(visibleSubmeshes) + ").",
                        outFail)) {
                    return false;
                }
            }

            struct SmokeFrameExpectation {
                float sourceFrame;
                std::size_t visibleSubmeshes;
            };
            // SV's two short side emissions interrupt the longer top plume.
            // Preserve those authored gaps instead of synthesizing an opacity
            // curve from the unrelated scrolling-displacement UV track.
            const std::array<SmokeFrameExpectation, 8u> smokeLifecycle = {{
                {37.0f, 0u},
                {45.0f, 6u},
                {53.0f, 4u},
                {56.0f, 2u},
                {59.0f, 6u},
                {76.0f, 2u},
                {78.0f, 6u},
                {99.0f, 0u},
            }};
            for (const SmokeFrameExpectation& expectation : smokeLifecycle) {
                std::size_t visibleSubmeshes = 0u;
                const float sampleTime = expectation.sourceFrame / 60.0f;
                for (const std::size_t submeshIndex : effectSubmeshIndices) {
                    const float alpha = game::runtime::
                        shared_projected_unit_backend_mesh_prep::detail::
                            sampleNativeEffectVisibilityAlpha(
                                weezingMesh,
                                roarIndex,
                                submeshIndex,
                                sampleTime,
                                sampleTime);
                    if (alpha > 0.999f) ++visibleSubmeshes;
                }
                if (!expect(
                        visibleSubmeshes == expectation.visibleSubmeshes,
                        "Cooked Weezing roar01 smoke lifecycle differs from "
                        "SV at source frame " +
                            std::to_string(
                                static_cast<int>(expectation.sourceFrame)) +
                            " (expected " +
                            std::to_string(expectation.visibleSubmeshes) +
                            " visible cloud/plume submeshes, found " +
                            std::to_string(visibleSubmeshes) + ").",
                        outFail)) {
                    return false;
                }
            }
        }

        std::filesystem::path koffingObject;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(
                 weezingObjectRoot)) {
            if (entry.is_regular_file() &&
                entry.path().filename() == "0109_Koffing_SV.phlo") {
                koffingObject = entry.path();
                break;
            }
        }
        if (!koffingObject.empty()) {
            game::runtime::render_model::MeshData koffingMesh;
            std::string loadError;
            if (!expect(
                    game::runtime::phlosion::loadModelObject(
                        koffingObject.string(),
                        koffingMesh,
                        &loadError),
                    "Failed to load local Koffing idle-smoke regression "
                    "asset: " + loadError,
                    outFail)) {
                return false;
            }
            const int idleIndex = resolveAnimIndex(
                koffingMesh,
                "pm0109_00_00_20000_defaultwait01_loop");
            const int controllerIndex = game::runtime::
                shared_backend_pose::
                    continuousNativeOverlayAnimationIndex(koffingMesh);
            std::vector<std::size_t> effectSubmeshIndices;
            for (std::size_t submeshIndex = 0u;
                 submeshIndex < koffingMesh.submeshMaterialModes.size();
                 ++submeshIndex) {
                const bool effect =
                    koffingMesh.submeshMaterialModes[submeshIndex] ==
                        game::runtime::render_model::
                            kNativeLayeredUnlitMaterialMode &&
                    submeshIndex <
                        koffingMesh.submeshMaterialFlags.size() &&
                    koffingMesh.submeshMaterialFlags[submeshIndex] > 2.5f &&
                    koffingMesh.submeshMaterialFlags[submeshIndex] < 3.5f;
                if (effect) effectSubmeshIndices.push_back(submeshIndex);
            }
            if (!expect(
                    idleIndex >= 0 && controllerIndex >= 0 &&
                        effectSubmeshIndices.size() >= 6u,
                    "Cooked Koffing is missing its retained one-second "
                    "28201 controller or paired SSSEffect smoke submeshes "
                    "(idle=" + std::to_string(idleIndex) +
                        ", controller=" +
                        std::to_string(controllerIndex) +
                        ", effect_submeshes=" +
                        std::to_string(effectSubmeshIndices.size()) + ").",
                    outFail)) {
                return false;
            }
            struct KoffingIdleSmokeExpectation {
                float controllerFrame;
                std::size_t visibleSubmeshes;
            };
            const std::array<KoffingIdleSmokeExpectation, 8u>
                koffingIdleSmokeLifecycle = {{
                    {0.0f, 0u},
                    {10.0f, 2u},
                    {12.0f, 4u},
                    {41.0f, 2u},
                    {44.0f, 0u},
                    {60.0f, 0u},
                    {70.0f, 2u},
                    {72.0f, 4u},
                }};
            for (const auto& expectation :
                 koffingIdleSmokeLifecycle) {
                std::size_t visibleSubmeshes = 0u;
                const float controllerTime =
                    expectation.controllerFrame / 60.0f;
                for (const std::size_t submeshIndex :
                     effectSubmeshIndices) {
                    const float alpha = game::runtime::
                        shared_projected_unit_backend_mesh_prep::detail::
                            sampleNativeEffectVisibilityAlpha(
                                koffingMesh,
                                idleIndex,
                                submeshIndex,
                                0.5f,
                                controllerTime);
                    if (alpha > 0.999f) ++visibleSubmeshes;
                }
                if (!expect(
                        visibleSubmeshes ==
                            expectation.visibleSubmeshes,
                        "Cooked Koffing idle smoke differs from its "
                        "one-second paired controller at source frame " +
                            std::to_string(static_cast<int>(
                                expectation.controllerFrame)) +
                            " (expected " +
                            std::to_string(
                                expectation.visibleSubmeshes) +
                            ", found " +
                            std::to_string(visibleSubmeshes) + ").",
                        outFail)) {
                    return false;
                }
            }
        }
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
            engine::paths::asset("models/0025_Pikachu_SV.phmodel"),
            engine::paths::asset("models/0025_Pikachu_SV.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
        },
        {
            engine::paths::asset("models/0021_Spearow_LGPE.phmodel"),
            engine::paths::asset("models/0021_Spearow_LGPE.animset.json"),
            "air_idle",
            "idle",
            {"fi01_wait", "fly", "air", "hover"},
        },
        {
            engine::paths::asset("models/0056_Mankey_SV.phmodel"),
            engine::paths::asset("models/0056_Mankey_SV.animset.json"),
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
            engine::paths::asset("models/0025_Pikachu_SV.phmodel"),
            engine::paths::asset("models/0025_Pikachu_SV.animset.json"),
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
        },
        {
            engine::paths::asset("models/0056_Mankey_SV.phmodel"),
            engine::paths::asset("models/0056_Mankey_SV.animset.json"),
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

