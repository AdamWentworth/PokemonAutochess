#include "game/runtime/shared/projected/core/SharedProjectedDebugVfx.h"

#include "game/GameWorld.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

namespace game::runtime::shared_projected_debug {
namespace {

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

ProjectedDebugVfxBuilder::ProjectedDebugVfxBuilder(
    bool supportsWorldTriangles3D,
    const glm::mat4& view,
    const glm::mat4& proj,
    int drawableH,
    const glm::vec4& screenViewport,
    std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
    std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
    std::vector<IRenderBackend::DebugLine>& lines)
    : supportsWorldTriangles3D_(supportsWorldTriangles3D),
      view_(view),
      proj_(proj),
      screenViewport_(screenViewport),
      drawableH_(drawableH),
      worldTriangles_(worldTriangles),
      world3DTriangles_(world3DTriangles),
      lines_(lines) {}

bool ProjectedDebugVfxBuilder::projectWorld(
    const glm::vec3& worldPos,
    float& outX,
    float& outY,
    float& outZ) const {
    const glm::vec3 p = glm::project(worldPos, view_, proj_, screenViewport_);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    outX = p.x;
    outY = static_cast<float>(drawableH_) - p.y;
    outZ = p.z;
    return true;
}

void ProjectedDebugVfxBuilder::appendWorldTriangle(const glm::vec3& a,
                                                   const glm::vec3& b,
                                                   const glm::vec3& c,
                                                   float r,
                                                   float g,
                                                   float bl,
                                                   float alpha) {
    if (!supportsWorldTriangles3D_) return;
    IRenderBackend::WorldTriangle tri;
    tri.x1 = a.x;
    tri.y1 = a.y;
    tri.z1 = a.z;
    tri.x2 = b.x;
    tri.y2 = b.y;
    tri.z2 = b.z;
    tri.x3 = c.x;
    tri.y3 = c.y;
    tri.z3 = c.z;
    tri.r = r;
    tri.g = g;
    tri.b = bl;
    tri.a = alpha;
    world3DTriangles_.push_back(tri);
}

void ProjectedDebugVfxBuilder::appendWorldQuad(const glm::vec3& a,
                                               const glm::vec3& b,
                                               const glm::vec3& c,
                                               const glm::vec3& d,
                                               float r,
                                               float g,
                                               float bl,
                                               float alpha) {
    appendWorldTriangle(a, b, c, r, g, bl, alpha);
    appendWorldTriangle(a, c, d, r, g, bl, alpha);
}

void ProjectedDebugVfxBuilder::appendProjectedTriangle(const glm::vec3& a,
                                                       const glm::vec3& b,
                                                       const glm::vec3& c,
                                                       float r,
                                                       float g,
                                                       float bl,
                                                       float alpha) {
    float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f, z2 = 0.0f;
    float x3 = 0.0f, y3 = 0.0f, z3 = 0.0f;
    if (!projectWorld(a, x1, y1, z1) ||
        !projectWorld(b, x2, y2, z2) ||
        !projectWorld(c, x3, y3, z3)) {
        return;
    }
    if ((z1 < 0.0f || z1 > 1.0f) && (z2 < 0.0f || z2 > 1.0f) && (z3 < 0.0f || z3 > 1.0f)) {
        return;
    }
    IRenderBackend::DebugTriangle tri;
    tri.x1 = x1;
    tri.y1 = y1;
    tri.x2 = x2;
    tri.y2 = y2;
    tri.x3 = x3;
    tri.y3 = y3;
    tri.r = r;
    tri.g = g;
    tri.b = bl;
    tri.a = alpha;
    worldTriangles_.push_back(tri);
}

void ProjectedDebugVfxBuilder::appendProjectedQuad(const glm::vec3& a,
                                                   const glm::vec3& b,
                                                   const glm::vec3& c,
                                                   const glm::vec3& d,
                                                   float r,
                                                   float g,
                                                   float bl,
                                                   float alpha) {
    appendProjectedTriangle(a, b, c, r, g, bl, alpha);
    appendProjectedTriangle(a, c, d, r, g, bl, alpha);
}

void ProjectedDebugVfxBuilder::appendProjectedLine(const glm::vec3& a,
                                                   const glm::vec3& b,
                                                   float r,
                                                   float g,
                                                   float bl,
                                                   float alpha,
                                                   float thickness) {
    float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f, z2 = 0.0f;
    if (!projectWorld(a, x1, y1, z1) || !projectWorld(b, x2, y2, z2)) return;
    if ((z1 < 0.0f || z1 > 1.0f) && (z2 < 0.0f || z2 > 1.0f)) return;
    IRenderBackend::DebugLine l;
    l.x1 = x1;
    l.y1 = y1;
    l.x2 = x2;
    l.y2 = y2;
    l.thickness = thickness;
    l.r = r;
    l.g = g;
    l.b = bl;
    l.a = alpha;
    lines_.push_back(l);
}

glm::vec3 ProjectedDebugVfxBuilder::safeNormalize3(const glm::vec3& v, const glm::vec3& fallback) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-9f) return glm::normalize(v);
    return fallback;
}

void ProjectedDebugVfxBuilder::appendProjectedRing(const glm::vec3& center,
                                                   float radius,
                                                   float r,
                                                   float g,
                                                   float bl,
                                                   float alpha,
                                                   float thickness,
                                                   int segments) {
    const int safeSegments = std::max(8, segments);
    for (int seg = 0; seg < safeSegments; ++seg) {
        const float t0 =
            (static_cast<float>(seg) / static_cast<float>(safeSegments)) * 6.2831853f;
        const float t1 =
            (static_cast<float>(seg + 1) / static_cast<float>(safeSegments)) * 6.2831853f;
        const glm::vec3 p0 = center + glm::vec3(std::cos(t0) * radius, 0.0f, std::sin(t0) * radius);
        const glm::vec3 p1 = center + glm::vec3(std::cos(t1) * radius, 0.0f, std::sin(t1) * radius);
        appendProjectedLine(p0, p1, r, g, bl, alpha, thickness);
    }
}

void ProjectedDebugVfxBuilder::appendProjectedBurst(const glm::vec3& center,
                                                    const glm::vec3& forward,
                                                    float radius,
                                                    float r,
                                                    float g,
                                                    float bl,
                                                    float alpha,
                                                    float thickness,
                                                    int spokes) {
    const int safeSpokes = std::max(4, spokes);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 fwd = safeNormalize3(forward, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 right = safeNormalize3(glm::cross(up, fwd), glm::vec3(1.0f, 0.0f, 0.0f));
    for (int i = 0; i < safeSpokes; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(safeSpokes)) * 6.2831853f;
        const glm::vec3 dir = safeNormalize3(
            right * std::cos(t) + fwd * std::sin(t) + up * 0.25f,
            fwd);
        appendProjectedLine(center, center + dir * radius, r, g, bl, alpha, thickness);
    }
}

void ProjectedDebugVfxBuilder::appendProjectedTailFire(
    const PokemonInstance& unit,
    const glm::vec3& center,
    const game::runtime::render_prep_proxy::UnitProxyExtents& extents,
    float yawDeg,
    float thickness) {
    const std::string species = toLowerCopy(unit.name);
    if (species != "charmander") return;
    if (!unit.alive || unit.fainting) return;

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 fwd = game::runtime::render_prep_proxy::yawForward(yawDeg);
    const glm::vec3 right = game::runtime::render_prep_proxy::yawRight(yawDeg);
    const glm::vec3 tailBase =
        center - fwd * std::max(0.03f, extents.halfDepth * 0.95f) +
        up * std::max(0.02f, extents.height * 0.22f);
    const float flameHeight = std::max(0.05f, extents.height * 0.26f);
    const float flameRadius = std::max(0.015f, extents.halfWidth * 0.16f);
    const float pulse = 0.5f + 0.5f * std::sin(unit.animTimeSec * 13.0f + static_cast<float>(unit.id) * 0.93f);

    appendProjectedLine(
        tailBase,
        tailBase + up * flameHeight * (0.90f + pulse * 0.35f),
        1.00f, 0.62f, 0.20f, 0.92f,
        std::max(1.0f, thickness * 1.25f));
    appendProjectedLine(
        tailBase + right * flameRadius * 0.35f,
        tailBase + right * flameRadius * 0.10f + up * flameHeight * (0.65f + pulse * 0.25f),
        1.00f, 0.88f, 0.38f, 0.88f,
        std::max(1.0f, thickness * 1.05f));
    appendProjectedLine(
        tailBase - right * flameRadius * 0.30f,
        tailBase - right * flameRadius * 0.08f + up * flameHeight * (0.58f + pulse * 0.22f),
        1.00f, 0.80f, 0.32f, 0.82f,
        std::max(1.0f, thickness * 1.0f));

    const glm::vec3 tip = tailBase + up * flameHeight * (0.88f + pulse * 0.30f);
    appendProjectedRing(
        tip,
        flameRadius * (0.45f + pulse * 0.20f),
        1.00f, 0.66f, 0.22f, 0.70f,
        std::max(1.0f, thickness * 0.95f),
        10);
}

void ProjectedDebugVfxBuilder::appendProjectedLeechDrain(const GameWorld* gameWorld,
                                                         const PokemonInstance& target,
                                                         float worldY,
                                                         float thickness) {
    if (!target.leechSeeded || target.leechSeedSourceId < 0) return;
    if (!gameWorld) return;
    const PokemonInstance* source = gameWorld->findUnitById(target.leechSeedSourceId);
    if (!source || !source->alive) return;
    const glm::vec3 from =
        target.position + glm::vec3(0.0f, std::max(0.08f, worldY) + target.visualYOffset, 0.0f);
    const glm::vec3 to =
        source->position + glm::vec3(0.0f, std::max(0.08f, worldY) + source->visualYOffset, 0.0f);
    const float phase =
        std::fmod(target.animTimeSec * 1.8f + static_cast<float>(target.id) * 0.21f, 1.0f);
    const int segments = 6;
    for (int i = 0; i < segments; ++i) {
        const float t0 =
            std::fmod(phase + static_cast<float>(i) / static_cast<float>(segments), 1.0f);
        const float t1 = std::min(1.0f, t0 + 0.10f);
        const glm::vec3 p0 = glm::mix(from, to, t0);
        const glm::vec3 p1 = glm::mix(from, to, t1);
        appendProjectedLine(
            p0,
            p1,
            0.42f,
            0.94f,
            0.34f,
            0.86f,
            std::max(1.0f, thickness * 1.05f));
    }
}

} // namespace game::runtime::shared_projected_debug


