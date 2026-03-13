#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/runtime/render_prep/BackendWorldProxyGeometry.h"

#include <vector>

#include <glm/glm.hpp>

class GameWorld;

namespace game::runtime::shared_projected_debug {

class ProjectedDebugVfxBuilder {
public:
    ProjectedDebugVfxBuilder(bool supportsWorldTriangles3D,
                             const glm::mat4& view,
                             const glm::mat4& proj,
                             int drawableH,
                             const glm::vec4& screenViewport,
                             std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                             std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
                             std::vector<IRenderBackend::DebugLine>& lines);

    bool projectWorld(const glm::vec3& worldPos, float& outX, float& outY, float& outZ) const;

    void appendWorldTriangle(const glm::vec3& a,
                             const glm::vec3& b,
                             const glm::vec3& c,
                             float r,
                             float g,
                             float bl,
                             float alpha);
    void appendWorldQuad(const glm::vec3& a,
                         const glm::vec3& b,
                         const glm::vec3& c,
                         const glm::vec3& d,
                         float r,
                         float g,
                         float bl,
                         float alpha);
    void appendProjectedTriangle(const glm::vec3& a,
                                 const glm::vec3& b,
                                 const glm::vec3& c,
                                 float r,
                                 float g,
                                 float bl,
                                 float alpha);
    void appendProjectedQuad(const glm::vec3& a,
                             const glm::vec3& b,
                             const glm::vec3& c,
                             const glm::vec3& d,
                             float r,
                             float g,
                             float bl,
                             float alpha);
    void appendProjectedLine(const glm::vec3& a,
                             const glm::vec3& b,
                             float r,
                             float g,
                             float bl,
                             float alpha,
                             float thickness);
    void appendProjectedRing(const glm::vec3& center,
                             float radius,
                             float r,
                             float g,
                             float bl,
                             float alpha,
                             float thickness,
                             int segments = 14);
    void appendProjectedBurst(const glm::vec3& center,
                              const glm::vec3& forward,
                              float radius,
                              float r,
                              float g,
                              float bl,
                              float alpha,
                              float thickness,
                              int spokes = 8);
    void appendProjectedTailFire(const PokemonInstance& unit,
                                 const glm::vec3& center,
                                 const game::runtime::backend_proxy::UnitProxyExtents& extents,
                                 float yawDeg,
                                 float thickness);
    void appendProjectedLeechDrain(const GameWorld* gameWorld,
                                   const PokemonInstance& target,
                                   float worldY,
                                   float thickness);

private:
    static glm::vec3 safeNormalize3(const glm::vec3& v, const glm::vec3& fallback);

    bool supportsWorldTriangles3D_ = false;
    glm::mat4 view_{1.0f};
    glm::mat4 proj_{1.0f};
    glm::vec4 screenViewport_{0.0f};
    int drawableH_ = 0;
    std::vector<IRenderBackend::DebugTriangle>& worldTriangles_;
    std::vector<IRenderBackend::WorldTriangle>& world3DTriangles_;
    std::vector<IRenderBackend::DebugLine>& lines_;
};

} // namespace game::runtime::shared_projected_debug


