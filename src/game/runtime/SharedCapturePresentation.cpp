#include "game/runtime/SharedCapturePresentation.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"
#include "engine/utils/ResourceManager.h"
#include "game/runtime/backend_model_cache/BackendModelCache.h"

namespace game::runtime::shared_capture {

namespace {
// Pokeball model forward axis is rotated relative to the game's capture yaw convention.
// Without this offset the ball reads as "pointing right" during shake/resolve instead of
// facing back toward the camera/player side.
constexpr float kPokeballCaptureYawModelOffsetDeg = -90.0f;
}

bool SnapshotCache::refresh(const GameWorld* gameWorld) {
    snaps.clear();
    byTargetId.clear();
    if (!gameWorld) return false;
    if (!gameWorld->buildCaptureAttemptRenderSnapshots(snaps)) return false;
    byTargetId.reserve(snaps.size());
    for (std::size_t i = 0; i < snaps.size(); ++i) {
        const auto& snap = snaps[i];
        if (snap.targetId < 0) continue;
        byTargetId[snap.targetId] = i;
    }
    return !snaps.empty();
}

const GameWorld::CaptureAttemptRenderSnapshot* SnapshotCache::findByTarget(int targetId) const {
    const auto it = byTargetId.find(targetId);
    if (it == byTargetId.end()) return nullptr;
    if (it->second >= snaps.size()) return nullptr;
    return &snaps[it->second];
}

float ballClipTimeSec(const GameWorld::CaptureAttemptRenderSnapshot& snap, float clipDurationSec) {
    if (clipDurationSec <= 0.0f) return 0.0f;
    if (snap.phase != 1) return 0.0f; // Absorb only; keep closed during throw/shake/resolve.
    return std::clamp(snap.absorbNorm01, 0.0f, 1.0f) * clipDurationSec;
}

glm::mat4 buildBallModelMatrix(const glm::vec3& pos, float yawDeg, float uniformScale) {
    yawDeg += kPokeballCaptureYawModelOffsetDeg;
    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(std::max(0.0f, uniformScale)));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0));
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), pos);
    return translation * rotationY * scale;
}

glm::mat4 buildBallModelMatrix(const GameWorld::CaptureAttemptRenderSnapshot& snap, float uniformScale) {
    float yawDeg = snap.ballYawDeg;
    float rollDeg = 0.0f;

    // Legacy capture behavior centers shake yaw at 0 degrees and only rocks side-to-side.
    // Using a derived facing yaw here caused visible left/right facing flips relative to the
    // camera/ally side for some board positions.
    if (snap.phase == 2 /* Shake */ || snap.phase == 3 /* Resolve */) {
        yawDeg = 0.0f;
    }
    if (snap.phase == 2 /* Shake */) {
        const float cycles = static_cast<float>(std::max(1, snap.shakes));
        const float theta = std::clamp(snap.phaseNorm01, 0.0f, 1.0f) * cycles * 6.28318530718f;
        rollDeg = std::sin(theta) * 18.0f;
    }

    yawDeg += kPokeballCaptureYawModelOffsetDeg;

    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(std::max(0.0f, uniformScale)));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0));
    const glm::mat4 rotationZ =
        glm::rotate(glm::mat4(1.0f), glm::radians(rollDeg), glm::vec3(0, 0, 1));
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), snap.ballPos);
    return translation * rotationY * rotationZ * scale;
}

int findPokeballAnimIndex(const std::shared_ptr<Model>& model) {
    if (!model) return -1;
    int animIndex = model->findAnimationIndexByName("Hinge_TopAction");
    if (animIndex < 0 && model->getAnimationCount() > 0) animIndex = 0;
    return animIndex;
}

int findPokeballAnimIndex(const backend_model::MeshData& mesh) {
    if (mesh.animations.empty()) return -1;
    for (std::size_t ai = 0; ai < mesh.animations.size(); ++ai) {
        if (mesh.animations[ai].name == "Hinge_TopAction") return static_cast<int>(ai);
    }
    return 0;
}

bool drawOpenGlSharedCapturePokeballModels(const GameWorld* gameWorld,
                                           ResourceManager* resources,
                                           const Camera3D* camera) {
    if (!gameWorld || !resources || !camera) return false;

    SnapshotCache cache;
    if (!cache.refresh(gameWorld)) return false;

    std::shared_ptr<Model> pokeballModel = resources->getModel("assets/models/pokeball.glb");
    if (!pokeballModel) return false;

    const int captureAnimIndex = findPokeballAnimIndex(pokeballModel);
    const float captureAnimDurSec =
        (captureAnimIndex >= 0) ? pokeballModel->getAnimationDurationSec(captureAnimIndex) : 0.0f;

    bool drewAny = false;
    for (const auto& snap : cache.snaps) {
        if (snap.timeLeftSec <= 0.0f) continue;
        const float scaleFactor =
            pokeballModel->getScaleFactor() * std::max(0.0f, snap.ballScale);
        const glm::mat4 instanceTransform =
            buildBallModelMatrix(snap, scaleFactor);
        const float animTimeSec =
            (captureAnimIndex >= 0 && captureAnimDurSec > 0.0f)
                ? ballClipTimeSec(snap, captureAnimDurSec)
                : 0.0f;
        const int animIndexForDraw = (captureAnimIndex >= 0) ? captureAnimIndex : 0;
        pokeballModel->drawAnimated(*camera, instanceTransform, animTimeSec, animIndexForDraw);
        drewAny = true;
    }

    return drewAny;
}

} // namespace game::runtime::shared_capture
