#include "game/runtime/SharedCapturePresentation.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/Model.h"
#include "game/runtime/BackendModelCache.h"

namespace game::runtime::shared_capture {

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
    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(std::max(0.0f, uniformScale)));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0));
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), pos);
    return translation * rotationY * scale;
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

} // namespace game::runtime::shared_capture

