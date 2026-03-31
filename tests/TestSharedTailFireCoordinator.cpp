#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool approx(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

bool approxVec3(const glm::vec3& a, const glm::vec3& b, float eps = 0.0001f) {
    return approx(a.x, b.x, eps) &&
           approx(a.y, b.y, eps) &&
           approx(a.z, b.z, eps);
}

} // namespace

bool test_shared_tail_fire_coordinator_contract(std::string& outFail) {
    namespace tail_fire = game::runtime::shared_tail_fire_coordinator;

    if (!expect(tail_fire::speciesUsesTailFireMeshPlayback("Charmander") &&
                    tail_fire::speciesUsesTailFireMeshPlayback("charmeleon") &&
                    tail_fire::speciesUsesTailFireMeshPlayback("charizard") &&
                    !tail_fire::speciesUsesTailFireMeshPlayback("pikachu"),
                "Tail Fire coordinator should centralize the authored-playback species set.",
                outFail)) {
        return false;
    }

    const auto& playbackSpecies = tail_fire::playbackSpeciesOrder();
    if (!expect(playbackSpecies.size() == 3u &&
                    playbackSpecies[0] == "charmander" &&
                    playbackSpecies[1] == "charmeleon" &&
                    playbackSpecies[2] == "charizard",
                "Tail Fire coordinator should expose the stable authored-playback species order used by runtime helpers.",
                outFail)) {
        return false;
    }

    if (!expect(tail_fire::backendUsesAuthoredMeshPlayback(nullptr) &&
                    tail_fire::backendUsesAuthoredMeshPlayback("opengl") &&
                    tail_fire::backendUsesAuthoredMeshPlayback("d3d12"),
                "Tail Fire coordinator should keep authored-mesh playback enabled across backends.",
                outFail)) {
        return false;
    }

    if (!expect(tail_fire::backendUsesGpuClipSkinning(nullptr, "charmander") &&
                    tail_fire::backendUsesGpuClipSkinning("opengl", "charmeleon") &&
                    !tail_fire::backendUsesGpuClipSkinning("d3d12", "charmander") &&
                    !tail_fire::backendUsesGpuClipSkinning("d3d12", "charizard") &&
                    tail_fire::backendUsesGpuClipSkinning("d3d12", "pikachu"),
                "Tail Fire coordinator should preserve the D3D12 GPU-clip-skinning guard for authored Tail Fire species.",
                outFail)) {
        return false;
    }

    const TailFireVFXConfig& primaryCfg = tail_fire::resolvePrimaryPlaybackConfig();
    const TailFireVFXConfig& explicitCfg = tail_fire::resolvePlaybackConfig("charmander");
    if (!expect(&primaryCfg == &explicitCfg &&
                    !primaryCfg.flipbookPath.empty(),
                "Tail Fire coordinator should centralize the primary playback config lookup.",
                outFail)) {
        return false;
    }

    const auto* primaryAuthoredSpec = tail_fire::resolvePrimaryAuthoredFlipbookSpec();
    if (!expect(primaryAuthoredSpec != nullptr &&
                    primaryAuthoredSpec->path != nullptr &&
                    std::string(primaryAuthoredSpec->path).find("charmander_fire_uv_flipbook") != std::string::npos,
                "Tail Fire coordinator should centralize the primary authored flipbook lookup used by prewarm and playback helpers.",
                outFail)) {
        return false;
    }

    const auto& authoredSpecs = tail_fire::authoredFlipbookSpecs();
    if (!expect(authoredSpecs.size() == playbackSpecies.size() &&
                    authoredSpecs[2].path != nullptr &&
                    std::string(authoredSpecs[2].path).find("CharizardFireUVFlipbook") != std::string::npos,
                "Tail Fire coordinator should expose the authored flipbook set for the whole starter line.",
                outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData mesh;
    mesh.modelScaleFactor = 1.25f;
    mesh.nodeNames = {"root", "tail_06", "fire_anchor_base", "fire_anchor_tip"};
    mesh.bindNodeGlobals.resize(4u, glm::mat4(1.0f));

    game::runtime::shared_backend_pose::PoseEval poseEval;
    poseEval.hasScenePose = true;
    poseEval.nodeLocals.resize(4u);
    poseEval.nodeGlobals = {
        glm::mat4(1.0f),
        glm::translate(glm::mat4(1.0f), glm::vec3(-0.2f, 0.5f, -0.6f)),
        glm::translate(glm::mat4(1.0f), glm::vec3(-0.1f, 0.7f, -0.8f)),
        glm::translate(glm::mat4(1.0f), glm::vec3(-0.15f, 0.95f, -0.92f)),
    };

    TailFireVFXConfig cfg;
    cfg.tailTipNodeName = "tail_06";
    cfg.fireAnchorBaseNodeName = "fire_anchor_base";
    cfg.fireAnchorTipNodeName = "fire_anchor_tip";
    cfg.backDir = glm::vec3(0.0f, 0.0f, 1.0f);

    game::runtime::shared_tail_fire_fallback::Anchor exactAnchor;
    if (!expect(
            tail_fire::exportPlaybackAnchor(
                {
                    .unitId = 77,
                    .mesh = &mesh,
                    .scenePose = &poseEval,
                    .resolvedScaleCorrection = 0.8f,
                    .config = &cfg,
                    .worldMatrixForNode =
                        [&](int nodeIndex) {
                            return poseEval.nodeGlobals[static_cast<std::size_t>(nodeIndex)];
                        },
                },
                exactAnchor) &&
                exactAnchor.valid &&
                exactAnchor.exactFireAnchor &&
                !exactAnchor.meshCarrierActive &&
                approxVec3(exactAnchor.pos, glm::vec3(-0.1f, 0.7f, -0.8f)) &&
                approxVec3(exactAnchor.tipPos, glm::vec3(-0.15f, 0.95f, -0.92f)) &&
                approx(exactAnchor.particleSizeScale, 1.0f),
            "Tail Fire coordinator should export exact authored fire anchors from the shared rig nodes.",
            outFail)) {
        return false;
    }

    TailFireVFXConfig fallbackCfg;
    fallbackCfg.tailTipNodeName = "tail_06";
    fallbackCfg.backDir = glm::vec3(1.0f, 0.0f, 0.0f);

    game::runtime::shared_tail_fire_fallback::Anchor tailTipAnchor;
    if (!expect(
            tail_fire::exportPlaybackAnchor(
                {
                    .unitId = 78,
                    .mesh = &mesh,
                    .scenePose = &poseEval,
                    .resolvedScaleCorrection = 1.0f,
                    .config = &fallbackCfg,
                    .worldMatrixForNode =
                        [&](int nodeIndex) {
                            return poseEval.nodeGlobals[static_cast<std::size_t>(nodeIndex)];
                        },
                },
                tailTipAnchor) &&
                tailTipAnchor.valid &&
                !tailTipAnchor.exactFireAnchor &&
                approxVec3(tailTipAnchor.pos, glm::vec3(-0.2f, 0.5f, -0.6f)),
            "Tail Fire coordinator should fall back to the tail-tip anchor when exact authored fire nodes are unavailable.",
            outFail)) {
        return false;
    }

    const auto* profile = tail_fire::resolvePlaybackProfile("charmander", &mesh);
    if (!expect(profile != nullptr &&
                    tail_fire::baseSubmeshUsesAuthoredFire(0u, profile) == false &&
                    tail_fire::baseSubmeshUsesAuthoredFire(999u, profile) == false,
                "Tail Fire coordinator should expose authored-submesh queries without leaking renderer-specific types.",
                outFail)) {
        return false;
    }

    return true;
}
