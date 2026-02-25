#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "game/GameWorld.h"

class Model;
class Camera3D;
class ResourceManager;

namespace game::runtime::backend_model {
struct MeshData;
}

namespace game::runtime::shared_capture {

struct SnapshotCache {
    std::vector<GameWorld::CaptureAttemptRenderSnapshot> snaps;
    std::unordered_map<int, std::size_t> byTargetId;

    bool refresh(const GameWorld* gameWorld);
    const GameWorld::CaptureAttemptRenderSnapshot* findByTarget(int targetId) const;
};

float ballClipTimeSec(const GameWorld::CaptureAttemptRenderSnapshot& snap, float clipDurationSec);
glm::mat4 buildBallModelMatrix(const glm::vec3& pos, float yawDeg, float uniformScale);
glm::mat4 buildBallModelMatrix(const GameWorld::CaptureAttemptRenderSnapshot& snap, float uniformScale);
int findPokeballAnimIndex(const std::shared_ptr<Model>& model);
int findPokeballAnimIndex(const backend_model::MeshData& mesh);
bool drawOpenGlSharedCapturePokeballModels(const GameWorld* gameWorld,
                                           ResourceManager* resources,
                                           const Camera3D* camera);

} // namespace game::runtime::shared_capture
