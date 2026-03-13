#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBatches.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace game::runtime::shared_growl_batches {
namespace {

float hash01Local(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    const std::uint32_t v = x >> 8;
    return static_cast<float>(v) * (1.0f / 16777216.0f);
}

glm::quat rotationFromToSafeLocal(const glm::vec3& from, const glm::vec3& to) {
    glm::vec3 a = from;
    glm::vec3 b = to;
    const float la = glm::length(a);
    const float lb = glm::length(b);
    if (la <= 0.0001f || lb <= 0.0001f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    a /= la;
    b /= lb;
    const float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (d > 0.9999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.9999f) {
        glm::vec3 ortho = glm::cross(a, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::dot(ortho, ortho) <= 0.0001f) ortho = glm::cross(a, glm::vec3(0.0f, 1.0f, 0.0f));
        ortho = glm::normalize(ortho);
        return glm::angleAxis(3.14159265f, ortho);
    }
    const glm::vec3 axis = glm::normalize(glm::cross(a, b));
    return glm::angleAxis(std::acos(d), axis);
}

glm::vec3 safeNormalize3Local(const glm::vec3& v, const glm::vec3& fallback) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-9f) return glm::normalize(v);
    return fallback;
}

void appendTransformedMeshLocal(shared_world_batches::WorldIndexedBatch& batch,
                                const render_model::MeshData& mesh,
                                const glm::mat4& world,
                                const glm::vec4& color,
                                bool quantizeLineAlpha,
                                float lineTevK1A) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3u) return;
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.reserve(batch.vertices.size() + mesh.vertices.size());
    for (const auto& src : mesh.vertices) {
        const glm::vec4 wp = world * glm::vec4(src.position, 1.0f);
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = wp.x;
        vtx.y = wp.y;
        vtx.z = wp.z;
        vtx.u = src.uv.x;
        vtx.v = src.uv.y;
        vtx.r = color.r;
        vtx.g = color.g;
        vtx.b = color.b;
        const float srcAlpha = std::clamp(src.color.a, 0.0f, 1.0f);
        if (quantizeLineAlpha) {
            vtx.a = shared_growl::quantizeLineVertexAlpha(srcAlpha, lineTevK1A, color.a);
        } else {
            vtx.a = std::clamp(color.a * srcAlpha, 0.0f, 1.0f);
        }
        batch.vertices.push_back(vtx);
    }
    batch.indices.reserve(batch.indices.size() + mesh.indices.size());
    for (std::uint32_t idx : mesh.indices) {
        batch.indices.push_back(baseVertex + idx);
    }
}

void appendQuarterRingLocal(shared_world_batches::WorldIndexedBatch& batch,
                            const glm::mat4& world,
                            const glm::vec4& color) {
    static constexpr glm::vec3 kPositions[4] = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 1.0f),
    };
    static constexpr glm::vec2 kUvs[4] = {
        glm::vec2(1.0f, 1.0f),
        glm::vec2(0.0f, 1.0f),
        glm::vec2(1.0f, 0.0f),
        glm::vec2(0.0f, 0.0f),
    };
    static constexpr std::uint32_t kIndices[6] = {0u, 1u, 2u, 2u, 1u, 3u};
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.reserve(batch.vertices.size() + 4u);
    batch.indices.reserve(batch.indices.size() + 6u);
    for (int i = 0; i < 4; ++i) {
        const glm::vec4 wp = world * glm::vec4(kPositions[i], 1.0f);
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = wp.x;
        vtx.y = wp.y;
        vtx.z = wp.z;
        vtx.u = kUvs[i].x;
        vtx.v = kUvs[i].y;
        vtx.r = color.r;
        vtx.g = color.g;
        vtx.b = color.b;
        vtx.a = color.a;
        batch.vertices.push_back(vtx);
    }
    for (std::uint32_t idx : kIndices) {
        batch.indices.push_back(baseVertex + idx);
    }
}

} // namespace

bool appendPassBatch(std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
                     const GrowlWaveVFX::RenderSnapshot& snapshot,
                     const GrowlWaveVFX::Config::DrawPass& pass,
                     const shared_growl::TevState& passTev,
                     const render_model::MeshData* passMesh,
                     const TextureView& texture,
                     const glm::vec3& cameraWorldPos) {
    if (!pass.enabled) return false;
    if (snapshot.rings.empty()) return false;
    if (texture.rgba == nullptr || texture.width <= 0 || texture.height <= 0) return false;

    const bool drawQuarterRing = pass.textureQuarterRing;
    if (!drawQuarterRing && passMesh == nullptr) return false;

    const bool drawLinePass = shared_growl::isLinePass(snapshot.config, pass);
    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);

    shared_world_batches::WorldIndexedBatch batch;
    batch.textureKey =
        std::string("growl:") + pass.id + ":" +
        (pass.texturePath.empty() ? std::string("__white__") : pass.texturePath);
    batch.textureRgba = texture.rgba;
    batch.textureWidth = texture.width;
    batch.textureHeight = texture.height;
    batch.textureWrapS = 10497;
    batch.textureWrapT = 10497;
    batch.alphaMode = 2u;
    batch.blendMode = 1u; // Legacy growl passes use additive blending.
    batch.alphaCutoff = 0.0f;

    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
    const glm::vec3 passTint = drawLinePass
        ? glm::clamp(passTev.c0 * glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                     glm::vec3(0.0f),
                     glm::vec3(1.0f))
        : glm::vec3(1.0f, 1.0f, 1.0f);

    float sortDepth = 0.0f;
    bool hasGeometry = false;

    for (const auto& ring : snapshot.rings) {
        const float life = std::max(0.0001f, ring.lifeSec);
        const float age01 = glm::clamp(ring.ageSec / life, 0.0f, 1.0f);
        const float scale =
            glm::mix(ring.startScale, ring.endScale, age01) * std::max(0.0f, pass.scaleMul);
        if (scale <= 0.0001f) continue;

        const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
        right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 up = glm::cross(ringForward, right);
        up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

        std::vector<glm::vec3> localDirectionsFallback;
        const std::vector<glm::vec3>* localDirections = &pass.directionsLocal;
        if (localDirections->empty()) {
            localDirectionsFallback.push_back(
                pass.overrideDirection ? pass.directionLocal : glm::vec3(0.0f, 0.0f, 1.0f));
            localDirections = &localDirectionsFallback;
        }
        if (localDirections->empty()) continue;

        float fade = 1.0f;
        if (age01 > fadeStart) {
            const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
            fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
        }
        if (fade <= 0.001f) continue;

        const float radiusMul = std::max(0.0f, pass.radiusMul);
        const float thicknessMul = std::max(0.0f, pass.thicknessMul);
        const glm::vec3 axisScale =
            glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
        const glm::vec3 finalScale = glm::vec3(scale) * axisScale;

        for (std::size_t dirIndex = 0; dirIndex < localDirections->size(); ++dirIndex) {
            glm::vec3 localDirBasisRaw = (*localDirections)[dirIndex];
            if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) continue;

            if (pass.directionSpacingJitterDeg > 0.0001f && localDirections->size() > 1u) {
                const glm::vec2 baseXY(localDirBasisRaw.x, localDirBasisRaw.y);
                const float xyLen = glm::length(baseXY);
                if (xyLen > 0.0001f) {
                    const float baseAngle = std::atan2(baseXY.y, baseXY.x);
                    const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                    const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                    const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x68e31da4u);
                    const float delta =
                        glm::radians(pass.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
                    const float angle = baseAngle + delta;
                    localDirBasisRaw.x = std::cos(angle) * xyLen;
                    localDirBasisRaw.y = std::sin(angle) * xyLen;
                }
            }

            float lineAlphaMul = std::max(0.0f, pass.alphaMul);
            if (pass.lineAlphaMax > pass.lineAlphaMin + 0.0001f) {
                const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
                lineAlphaMul *= glm::mix(pass.lineAlphaMin, pass.lineAlphaMax, noise);
            }
            float passAlphaScale = std::clamp(fade * lineAlphaMul, 0.0f, 1.0f);
            if (!drawQuarterRing && !drawLinePass) {
                passAlphaScale *= passTev.k1a;
            }
            const float passAlpha = std::clamp(passAlphaScale, 0.0f, 1.0f);
            if (passAlpha <= 0.001f) continue;

            const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
            const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
            if (glm::dot(worldDir, worldDir) <= 0.000001f) continue;

            const glm::vec3 passForward = glm::normalize(worldDir);
            const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
            const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
            const glm::vec3 radialStartOffset =
                (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
            const glm::vec3 passPos = ring.pos + passForward * pass.forwardOffset + radialStartOffset;
            const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);
            sortDepth = std::max(sortDepth, distSq);

            const glm::vec4 color(passTint, passAlpha);
            if (drawQuarterRing) {
                const int quarterCount = std::max(1, pass.quarterCount);
                for (int i = 0; i < quarterCount; ++i) {
                    const float quarterDeg =
                        pass.quarterStartDeg + pass.quarterStepDeg * static_cast<float>(i);
                    const glm::quat quarterRot = glm::angleAxis(glm::radians(quarterDeg), meshForwardLocal);
                    const glm::mat4 world =
                        glm::translate(glm::mat4(1.0f), passPos) *
                        glm::mat4_cast(passRot * quarterRot) *
                        glm::scale(glm::mat4(1.0f), finalScale);
                    appendQuarterRingLocal(batch, world, color);
                    hasGeometry = true;
                }
            } else if (passMesh) {
                const glm::mat4 world =
                    glm::translate(glm::mat4(1.0f), passPos) *
                    glm::mat4_cast(passRot) *
                    glm::scale(glm::mat4(1.0f), finalScale);
                appendTransformedMeshLocal(batch, *passMesh, world, color, drawLinePass, passTev.k1a);
                hasGeometry = true;
            }
        }
    }

    if (!hasGeometry || batch.vertices.empty() || batch.indices.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return true;
}

} // namespace game::runtime::shared_growl_batches

