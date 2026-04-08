#include "vfx/runtime/shared/SharedAuthoredVfxBatches.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vfx::runtime::authored_batches {

namespace render_model = vfx::runtime::authored_batches;
namespace shared_world_batches = vfx::runtime::authored_batches;

namespace {

struct SharedMeshGeometry {
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct SparkleSpriteDescriptor {
    glm::vec3 center{0.0f};
    float width = 1.0f;
    float height = 1.0f;
};

float hash01Local(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    const std::uint32_t v = x >> 8;
    return static_cast<float>(v) * (1.0f / 16777216.0f);
}

float computeBillboardSpinRadLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass, float age01) {
    const float clampedAge01 = glm::clamp(age01, 0.0f, 1.0f);
    return glm::radians(pass.billboardSpinStartDeg + 360.0f * pass.billboardSpinTurns * clampedAge01);
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

float fastLaunch01Local(float t) {
    const float clamped = glm::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - clamped;
    return 1.0f - inv * inv * inv;
}

glm::vec3 meshBoundsCenterLocal(const render_model::MeshData& mesh) {
    if (mesh.vertices.empty()) return glm::vec3(0.0f);
    glm::vec3 minP(std::numeric_limits<float>::max());
    glm::vec3 maxP(-std::numeric_limits<float>::max());
    for (const auto& vertex : mesh.vertices) {
        minP = glm::min(minP, vertex.position);
        maxP = glm::max(maxP, vertex.position);
    }
    return 0.5f * (minP + maxP);
}

glm::vec2 meshProjectionRangeLocal(const render_model::MeshData& mesh, const glm::vec3& axis) {
    if (mesh.vertices.empty()) return glm::vec2(0.0f);
    const glm::vec3 normalizedAxis = safeNormalize3Local(axis, glm::vec3(0.0f, 0.0f, 1.0f));
    float minProj = std::numeric_limits<float>::max();
    float maxProj = -std::numeric_limits<float>::max();
    for (const auto& vertex : mesh.vertices) {
        const float proj = glm::dot(vertex.position, normalizedAxis);
        minProj = std::min(minProj, proj);
        maxProj = std::max(maxProj, proj);
    }
    if (minProj > maxProj) return glm::vec2(0.0f);
    return glm::vec2(minProj, maxProj);
}

std::array<float, 16> toModelMatrixArrayLocal(const glm::mat4& matrix) {
    std::array<float, 16> out{};
    std::memcpy(out.data(), &matrix[0][0], sizeof(float) * out.size());
    return out;
}

std::vector<glm::vec3> makeFallbackDirectionsLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass) {
    return authored::resolveGeneratedDirections(pass);
}

float resolveRadialDistanceMulLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                                    std::uint32_t randomSeed,
                                    std::size_t dirIndex,
                                    int sequenceIndex) {
    float minMul = std::max(0.0f, pass.radialDistanceMinMul);
    float maxMul = std::max(0.0f, pass.radialDistanceMaxMul);
    if (maxMul < minMul) std::swap(minMul, maxMul);
    if (std::abs(maxMul - minMul) <= 0.0001f) return minMul;

    const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
    const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
    const std::uint32_t seqSalt = static_cast<std::uint32_t>(sequenceIndex + 17) * 0xc2b2ae35u;
    const float noise = hash01Local(randomSeed ^ passSalt ^ dirSalt ^ seqSalt ^ 0x6d2b79f5u);
    return glm::mix(minMul, maxMul, noise);
}

const SharedMeshGeometry& getSharedMeshGeometryLocal(const render_model::MeshData& mesh,
                                                     const std::string& geometryKey) {
    static thread_local std::unordered_map<std::string, SharedMeshGeometry> cache;

    const auto found = cache.find(geometryKey);
    if (found != cache.end()) return found->second;

    SharedMeshGeometry geometry;
    geometry.vertices.reserve(mesh.vertices.size());
    geometry.indices = mesh.indices;
    for (const auto& src : mesh.vertices) {
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = src.position.x;
        vtx.y = src.position.y;
        vtx.z = src.position.z;
        vtx.u = src.uv.x;
        vtx.v = src.uv.y;
        vtx.r = 1.0f;
        vtx.g = 1.0f;
        vtx.b = 1.0f;
        vtx.a = std::clamp(src.color.a, 0.0f, 1.0f);
        vtx.nx = src.normal.x;
        vtx.ny = src.normal.y;
        vtx.nz = src.normal.z;
        vtx.joint0 = static_cast<float>(src.j0);
        vtx.joint1 = static_cast<float>(src.j1);
        vtx.joint2 = static_cast<float>(src.j2);
        vtx.joint3 = static_cast<float>(src.j3);
        vtx.weight0 = src.w0;
        vtx.weight1 = src.w1;
        vtx.weight2 = src.w2;
        vtx.weight3 = src.w3;
        vtx.tx = src.tangent.x;
        vtx.ty = src.tangent.y;
        vtx.tz = src.tangent.z;
        vtx.tw = src.tangent.w;
        geometry.vertices.push_back(vtx);
    }

    return cache.emplace(geometryKey, std::move(geometry)).first->second;
}

const SharedMeshGeometry& getSharedLineMeshGeometryLocal(const render_model::MeshData& mesh,
                                                         const std::string& geometryKey,
                                                         float lineTevK1A) {
    static thread_local std::unordered_map<std::string, SharedMeshGeometry> cache;

    const auto found = cache.find(geometryKey);
    if (found != cache.end()) return found->second;

    SharedMeshGeometry geometry;
    geometry.vertices.reserve(mesh.vertices.size());
    geometry.indices = mesh.indices;
    for (const auto& src : mesh.vertices) {
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = src.position.x;
        vtx.y = src.position.y;
        vtx.z = src.position.z;
        vtx.u = src.uv.x;
        vtx.v = src.uv.y;
        vtx.r = 1.0f;
        vtx.g = 1.0f;
        vtx.b = 1.0f;
        vtx.a = authored::quantizeLineVertexAlpha(
            std::clamp(src.color.a, 0.0f, 1.0f),
            lineTevK1A,
            1.0f);
        vtx.nx = src.normal.x;
        vtx.ny = src.normal.y;
        vtx.nz = src.normal.z;
        vtx.joint0 = static_cast<float>(src.j0);
        vtx.joint1 = static_cast<float>(src.j1);
        vtx.joint2 = static_cast<float>(src.j2);
        vtx.joint3 = static_cast<float>(src.j3);
        vtx.weight0 = src.w0;
        vtx.weight1 = src.w1;
        vtx.weight2 = src.w2;
        vtx.weight3 = src.w3;
        vtx.tx = src.tangent.x;
        vtx.ty = src.tangent.y;
        vtx.tz = src.tangent.z;
        vtx.tw = src.tangent.w;
        geometry.vertices.push_back(vtx);
    }

    return cache.emplace(geometryKey, std::move(geometry)).first->second;
}

const std::vector<SparkleSpriteDescriptor>& getSparkleSpriteDescriptorsLocal(
    const render_model::MeshData& mesh,
    const std::string& descriptorKey) {
    static thread_local std::unordered_map<std::string, std::vector<SparkleSpriteDescriptor>> cache;

    const auto found = cache.find(descriptorKey);
    if (found != cache.end()) return found->second;

    std::vector<SparkleSpriteDescriptor> descriptors;
    if (!mesh.vertices.empty() && mesh.indices.size() >= 6u) {
        descriptors.reserve(mesh.indices.size() / 6u);
        for (std::size_t i = 0; i + 5u < mesh.indices.size(); i += 6u) {
            std::array<std::uint32_t, 4> unique{};
            std::size_t uniqueCount = 0u;
            for (std::size_t j = 0; j < 6u; ++j) {
                const std::uint32_t idx = mesh.indices[i + j];
                bool seen = false;
                for (std::size_t k = 0; k < uniqueCount; ++k) {
                    if (unique[k] == idx) {
                        seen = true;
                        break;
                    }
                }
                if (seen) continue;
                if (uniqueCount < unique.size()) unique[uniqueCount++] = idx;
            }
            if (uniqueCount < 4u) continue;
            if (unique[0] >= mesh.vertices.size() || unique[1] >= mesh.vertices.size() ||
                unique[2] >= mesh.vertices.size() || unique[3] >= mesh.vertices.size()) {
                continue;
            }

            const glm::vec3 p0 = mesh.vertices[unique[0]].position;
            const glm::vec3 p1 = mesh.vertices[unique[1]].position;
            const glm::vec3 p2 = mesh.vertices[unique[2]].position;
            const glm::vec3 p3 = mesh.vertices[unique[3]].position;
            SparkleSpriteDescriptor desc;
            desc.center = (p0 + p1 + p2 + p3) * 0.25f;
            desc.width = std::max(0.0001f, glm::length(p1 - p0));
            desc.height = std::max(0.0001f, glm::length(p2 - p0));
            descriptors.push_back(desc);
        }
    }

    if (descriptors.empty()) {
        SparkleSpriteDescriptor fallback;
        fallback.center = meshBoundsCenterLocal(mesh);
        descriptors.push_back(fallback);
    }

    return cache.emplace(descriptorKey, std::move(descriptors)).first->second;
}

const std::array<IRenderBackend::WorldMeshVertex, 4>& quarterVerticesLocal() {
    static const std::array<IRenderBackend::WorldMeshVertex, 4> kVertices = [] {
        std::array<IRenderBackend::WorldMeshVertex, 4> verts{};
        constexpr std::array<glm::vec3, 4> kPositions = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(1.0f, 0.0f, 1.0f),
        };
        constexpr std::array<glm::vec2, 4> kUvs = {
            glm::vec2(1.0f, 1.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(0.0f, 0.0f),
        };
        for (std::size_t i = 0; i < verts.size(); ++i) {
            verts[i].x = kPositions[i].x;
            verts[i].y = kPositions[i].y;
            verts[i].z = kPositions[i].z;
            verts[i].u = kUvs[i].x;
            verts[i].v = kUvs[i].y;
            verts[i].r = 1.0f;
            verts[i].g = 1.0f;
            verts[i].b = 1.0f;
            verts[i].a = 1.0f;
        }
        return verts;
    }();
    return kVertices;
}

const std::array<IRenderBackend::WorldMeshVertex, 4>& centeredQuadVerticesLocal() {
    static const std::array<IRenderBackend::WorldMeshVertex, 4> kVertices = [] {
        std::array<IRenderBackend::WorldMeshVertex, 4> verts{};
        constexpr std::array<glm::vec3, 4> kPositions = {
            glm::vec3(-0.5f, 0.0f, -0.5f),
            glm::vec3(0.5f, 0.0f, -0.5f),
            glm::vec3(-0.5f, 0.0f, 0.5f),
            glm::vec3(0.5f, 0.0f, 0.5f),
        };
        constexpr std::array<glm::vec2, 4> kUvs = {
            glm::vec2(0.0f, 0.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(1.0f, 1.0f),
        };
        for (std::size_t i = 0; i < verts.size(); ++i) {
            verts[i].x = kPositions[i].x;
            verts[i].y = kPositions[i].y;
            verts[i].z = kPositions[i].z;
            verts[i].u = kUvs[i].x;
            verts[i].v = kUvs[i].y;
            verts[i].r = 1.0f;
            verts[i].g = 1.0f;
            verts[i].b = 1.0f;
            verts[i].a = 1.0f;
        }
        return verts;
    }();
    return kVertices;
}

const std::array<IRenderBackend::WorldMeshVertex, 4>& streakQuadVerticesLocal() {
    static const std::array<IRenderBackend::WorldMeshVertex, 4> kVertices = [] {
        std::array<IRenderBackend::WorldMeshVertex, 4> verts{};
        constexpr std::array<glm::vec3, 4> kPositions = {
            glm::vec3(-0.05f, 0.0f, 0.0f),
            glm::vec3(0.05f, 0.0f, 0.0f),
            glm::vec3(-0.05f, 0.0f, 1.0f),
            glm::vec3(0.05f, 0.0f, 1.0f),
        };
        constexpr std::array<float, 4> kAlpha = {0.0f, 0.0f, 1.0f, 1.0f};
        for (std::size_t i = 0; i < verts.size(); ++i) {
            verts[i].x = kPositions[i].x;
            verts[i].y = kPositions[i].y;
            verts[i].z = kPositions[i].z;
            verts[i].u = 0.0f;
            verts[i].v = 0.0f;
            verts[i].r = 1.0f;
            verts[i].g = 1.0f;
            verts[i].b = 1.0f;
            verts[i].a = kAlpha[i];
        }
        return verts;
    }();
    return kVertices;
}

const std::array<std::uint32_t, 6>& quarterIndicesLocal() {
    static const std::array<std::uint32_t, 6> kIndices = {0u, 1u, 2u, 2u, 1u, 3u};
    return kIndices;
}

std::string makeMeshGeometryCacheKeyLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass) {
    return std::string("__authored_vfx_geom_mesh_v1__:") + pass.meshPath;
}

std::string makeSparkleSpriteDescriptorCacheKeyLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass) {
    return std::string("__authored_vfx_sparkle_desc_v1__:") + pass.meshPath;
}

std::string makeLineGeometryCacheKeyLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                                          float lineTevK1A) {
    const int k1aMilli = static_cast<int>(std::lround(std::clamp(lineTevK1A, 0.0f, 1.0f) * 1000.0f));
    return std::string("__authored_vfx_geom_line_v1__:") + pass.id + ":" + pass.meshPath + ":" + std::to_string(k1aMilli);
}

std::string makeQuarterGeometryCacheKeyLocal() {
    return "__authored_vfx_geom_quarter_v1__";
}

std::string makeCenteredQuadGeometryCacheKeyLocal() {
    return "__authored_vfx_geom_centered_quad_v1__";
}

std::string makeStreakQuadGeometryCacheKeyLocal(float lineTevK1A) {
    const int k1aMilli = static_cast<int>(std::lround(std::clamp(lineTevK1A, 0.0f, 1.0f) * 1000.0f));
    return std::string("__authored_vfx_geom_streak_quad_v2__:") + std::to_string(k1aMilli);
}

bool hasCustomUvTransformLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass) {
    return std::abs(pass.uvScale.x - 1.0f) > 0.0001f ||
           std::abs(pass.uvScale.y - 1.0f) > 0.0001f ||
           std::abs(pass.uvOffset.x) > 0.0001f ||
           std::abs(pass.uvOffset.y) > 0.0001f;
}

std::vector<IRenderBackend::WorldMeshVertex> makeUvTransformedQuadVerticesLocal(
    const std::array<IRenderBackend::WorldMeshVertex, 4>& base,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass) {
    std::vector<IRenderBackend::WorldMeshVertex> verts(base.begin(), base.end());
    for (auto& vtx : verts) {
        vtx.u = vtx.u * pass.uvScale.x + pass.uvOffset.x;
        vtx.v = vtx.v * pass.uvScale.y + pass.uvOffset.y;
    }
    return verts;
}

bool computePassSequenceStateLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                                   int sequenceCount,
                                   float age01,
                                   float fadeStart,
                                   int sequenceIndex,
                                   float& outLocalAge01,
                                   float& outFade) {
    if (sequenceCount <= 1) {
        outLocalAge01 = age01;
    } else {
        const float sequenceLife = std::clamp(pass.sequenceLife, 0.01f, 1.0f);
        const float sequenceStep = std::max(0.0f, pass.sequenceStep);
        const float sequenceStart = sequenceStep * static_cast<float>(sequenceIndex);
        const float sequenceTimelineSpan =
            sequenceStep * static_cast<float>(sequenceCount - 1) + sequenceLife;
        const float sequenceAge = age01 * sequenceTimelineSpan;
        outLocalAge01 = (sequenceAge - sequenceStart) / sequenceLife;
        if (outLocalAge01 < 0.0f || outLocalAge01 > 1.0f) return false;
    }

    outFade = 1.0f;
    if (outLocalAge01 > fadeStart) {
        const float t = (outLocalAge01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
        outFade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
    }
    return outFade > 0.001f;
}

bool computeDelayedPassLaunchStateLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                                        int sequenceCount,
                                        float age01,
                                        int sequenceIndex,
                                        float& outLaunchAge01) {
    if (sequenceCount <= 1) {
        outLaunchAge01 = age01;
        return true;
    }

    const float sequenceLife = std::clamp(pass.sequenceLife, 0.01f, 1.0f);
    const float sequenceStep = std::max(0.0f, pass.sequenceStep);
    const float sequenceStart = sequenceStep * static_cast<float>(sequenceIndex);
    const float sequenceTimelineSpan =
        sequenceStep * static_cast<float>(sequenceCount - 1) + sequenceLife;
    const float sequenceAge = age01 * sequenceTimelineSpan;
    if (sequenceAge < sequenceStart) return false;

    outLaunchAge01 = glm::clamp((sequenceAge - sequenceStart) / sequenceLife, 0.0f, 1.0f);
    return true;
}

float computeSharedDelayedFadeLocal(float age01, float fadeStart) {
    const float delayedFadeStart = std::max(fadeStart, 0.92f);
    if (age01 <= delayedFadeStart) return 1.0f;
    const float t = (age01 - delayedFadeStart) / std::max(0.0001f, 1.0f - delayedFadeStart);
    return 1.0f - glm::clamp(t, 0.0f, 1.0f);
}

float computeQuarterAnimatedScaleLocal(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                                       const SharedAuthoredBatchVFX::RenderRing& ring,
                                       int sequenceCount,
                                       float age01,
                                       float localAge01) {
    const float passScale = std::max(0.0f, pass.scaleMul);
    if (sequenceCount <= 1) {
        return glm::mix(ring.startScale, ring.endScale, age01) * passScale;
    }

    const float radiusGrowthMul = std::max(1.0f, pass.radiusGrowthMul);
    return std::max(0.0f, ring.startScale) * passScale *
           glm::mix(1.0f, radiusGrowthMul, glm::clamp(localAge01, 0.0f, 1.0f));
}

shared_world_batches::WorldIndexedBatch makeBaseBatchLocal(
    const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass,
    const TextureView& texture) {
    shared_world_batches::WorldIndexedBatch batch;
    batch.textureKey =
        std::string("authored_vfx:") + pass.id + ":" +
        (pass.texturePath.empty() ? std::string("__white__") : pass.texturePath);
    batch.textureCacheKey = authored::makeTextureCacheKey(snapshot.config, pass);
    batch.textureRgba = texture.rgba;
    batch.textureWidth = texture.width;
    batch.textureHeight = texture.height;
    batch.textureWrapS = pass.textureQuarterRing ? 33071 : 10497;
    batch.textureWrapT = pass.textureQuarterRing ? 33071 : 10497;
    batch.alphaMode = 2u;
    batch.blendMode = authored::resolveBlendMode(snapshot.config, pass);
    batch.dualSourceBlendEnabled = pass.dualSourceBlend ? 1u : 0u;
    batch.alphaCutoff = 0.0f;
    return batch;
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
            vtx.a = authored::quantizeLineVertexAlpha(srcAlpha, lineTevK1A, color.a);
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
    const auto& kVertices = quarterVerticesLocal();
    const auto& kIndices = quarterIndicesLocal();
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.reserve(batch.vertices.size() + kVertices.size());
    batch.indices.reserve(batch.indices.size() + kIndices.size());
    for (const auto& src : kVertices) {
        const glm::vec4 wp = world * glm::vec4(src.x, src.y, src.z, 1.0f);
        IRenderBackend::WorldMeshVertex vtx = src;
        vtx.x = wp.x;
        vtx.y = wp.y;
        vtx.z = wp.z;
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

void appendSharedGeometryLocal(shared_world_batches::WorldIndexedBatch& batch,
                               const SharedMeshGeometry& geometry,
                               const glm::mat4& world,
                               const glm::vec4& color) {
    if (geometry.vertices.empty() || geometry.indices.size() < 3u) return;

    const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.reserve(batch.vertices.size() + geometry.vertices.size());
    batch.indices.reserve(batch.indices.size() + geometry.indices.size());

    for (const auto& src : geometry.vertices) {
        const glm::vec4 wp = world * glm::vec4(src.x, src.y, src.z, 1.0f);
        IRenderBackend::WorldMeshVertex vtx;
        vtx.x = wp.x;
        vtx.y = wp.y;
        vtx.z = wp.z;
        vtx.u = src.u;
        vtx.v = src.v;
        vtx.r = std::clamp(src.r * color.r, 0.0f, 1.0f);
        vtx.g = std::clamp(src.g * color.g, 0.0f, 1.0f);
        vtx.b = std::clamp(src.b * color.b, 0.0f, 1.0f);
        vtx.a = std::clamp(src.a * color.a, 0.0f, 1.0f);
        batch.vertices.push_back(vtx);
    }
    for (std::uint32_t idx : geometry.indices) {
        batch.indices.push_back(baseVertex + idx);
    }
}

bool appendSharedMeshPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass,
    const authored::TevState& passTev,
    const render_model::MeshData& passMesh,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;
    if (!pass.directionsLocal.empty() && pass.directionsLocal.size() != 1u) return false;
    if (pass.directionSpacingJitterDeg > 0.0001f) return false;

    const bool quarterTextureBake =
        authored::usesQuarterTextureBake(snapshot.config, pass);

    const auto& ring = snapshot.rings.front();
    const float life = std::max(0.0001f, ring.lifeSec);
    const float age01 = glm::clamp(ring.ageSec / life, 0.0f, 1.0f);

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const int rawSequenceCount = std::max(1, pass.sequenceCount);
    const int delayedSequenceIndex =
        (rawSequenceCount > 1) ? std::clamp(pass.sequenceIndex, -1, rawSequenceCount - 1) : -1;
    const bool delayedSinglePass = delayedSequenceIndex >= 0;
    float localAge01 = age01;

    float fade = 1.0f;
    if (delayedSinglePass) {
        if (!computeDelayedPassLaunchStateLocal(
                pass, rawSequenceCount, age01, delayedSequenceIndex, localAge01)) {
            return false;
        }
        if (pass.sequenceFadeLocal) {
            if (localAge01 >= 0.999f) {
                fade = 0.0f;
            } else if (localAge01 > fadeStart) {
                const float t =
                    (localAge01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
                fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
            }
        } else {
            fade = computeSharedDelayedFadeLocal(age01, fadeStart);
        }
    } else if (age01 > fadeStart) {
        const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
        fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
    }
    if (fade <= 0.001f) return false;
    const float scale =
        glm::mix(ring.startScale, ring.endScale, localAge01) * std::max(0.0f, pass.scaleMul);
    if (scale <= 0.0001f) return false;

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    float passAlphaScale = std::clamp(fade * std::max(0.0f, pass.alphaMul), 0.0f, 1.0f);
    if (!quarterTextureBake) passAlphaScale *= passTev.k1a;
    const float passAlpha = std::clamp(passAlphaScale, 0.0f, 1.0f);
    if (passAlpha <= 0.001f) return false;

    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    std::vector<glm::vec3> fallbackDirections;
    const std::vector<glm::vec3>* localDirections = &pass.directionsLocal;
    if (localDirections->empty()) {
        fallbackDirections = makeFallbackDirectionsLocal(pass);
        localDirections = &fallbackDirections;
    }
    if (localDirections->size() != 1u) return false;

    const glm::vec3 localDirBasisRaw = (*localDirections)[0];
    if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) return false;

    const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
    const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
    if (glm::dot(worldDir, worldDir) <= 0.000001f) return false;

    const glm::vec3 passForward = glm::normalize(worldDir);
    const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
    const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
    const glm::vec3 radialStartOffset =
        (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
    const float forwardTravel =
        delayedSinglePass ? (pass.forwardOffset * fastLaunch01Local(localAge01)) : pass.forwardOffset;
    const glm::vec3 passPos = ring.pos + passForward * forwardTravel + radialStartOffset;
    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
    const glm::vec3 finalScale = glm::vec3(scale) * axisScale;
    const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);

    const std::string geometryCacheKey = makeMeshGeometryCacheKeyLocal(pass);
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    const SharedMeshGeometry& sharedGeometry =
        getSharedMeshGeometryLocal(passMesh, geometryCacheKey);
    if (sharedGeometry.vertices.empty() || sharedGeometry.indices.size() < 3u) return false;

    batch.sharedVertices = sharedGeometry.vertices.data();
    batch.sharedVertexCount = sharedGeometry.vertices.size();
    batch.sharedIndices = sharedGeometry.indices.data();
    batch.sharedIndexCount = sharedGeometry.indices.size();
    batch.geometryCacheKey = geometryCacheKey;
    batch.vertexColorMulR = 1.0f;
    batch.vertexColorMulG = 1.0f;
    batch.vertexColorMulB = 1.0f;
    batch.vertexColorMulA = passAlpha;
    batch.modelMatrix = toModelMatrixArrayLocal(
        glm::translate(glm::mat4(1.0f), passPos) *
        glm::mat4_cast(passRot) *
        glm::scale(glm::mat4(1.0f), finalScale));
    batch.sortDepth = distSq;
    outBatches.push_back(std::move(batch));
    return true;
}

bool appendSharedSparkleBillboardPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass,
    const render_model::MeshData& passMesh,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto& ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, true);
    authored::PassTimingState timingState;
    if (!authored::evaluatePassTiming(
            pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, 0, timingState)) {
        return false;
    }

    const float baseScale = std::max(0.0f, pass.scaleMul);
    if (baseScale <= 0.0001f) return false;

    const float passAlpha =
        std::clamp(timingState.fade * std::max(0.0f, pass.alphaMul), 0.0f, 1.0f);
    if (passAlpha <= 0.001f) return false;

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis =
        pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);

    const auto& sharedVertices = quarterVerticesLocal();
    const auto& sharedIndices = quarterIndicesLocal();
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedVertices = sharedVertices.data();
    batch.sharedVertexCount = sharedVertices.size();
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    batch.geometryCacheKey = makeQuarterGeometryCacheKeyLocal();

    const glm::vec3 meshCenter = meshBoundsCenterLocal(passMesh);
    const auto& descriptors = getSparkleSpriteDescriptorsLocal(
        passMesh, makeSparkleSpriteDescriptorCacheKeyLocal(pass));
    batch.instances.reserve(descriptors.size());

    const float startSpread = std::max(0.0f, pass.startRadiusMul);
    const float endSpread = std::max(startSpread, std::max(0.0f, pass.radiusMul));
    const float spreadT = glm::mix(startSpread, endSpread, timingState.localAge01);
    const float sizeMul = std::max(0.0f, pass.thicknessMul);
    float minCenteredLocalZ = 0.0f;
    float avgSpriteExtent = 0.0f;
    if (!descriptors.empty()) {
        minCenteredLocalZ = descriptors.front().center.z - meshCenter.z;
        for (const auto& desc : descriptors) {
            const glm::vec3 centeredLocal = desc.center - meshCenter;
            minCenteredLocalZ = std::min(minCenteredLocalZ, centeredLocal.z);
            avgSpriteExtent += 0.5f * (desc.width + desc.height);
        }
        avgSpriteExtent /= static_cast<float>(descriptors.size());
    }
    const float lateralWeight = 1.2f;
    const float verticalWeight = 1.15f;
    const float forwardWeight = 0.35f;
    const float forwardBiasLocal = std::max(0.0001f, avgSpriteExtent * 0.18f);
    const glm::vec3 sparkleOrigin =
        ring.pos + ringForward * pass.forwardOffset + up * pass.heightOffset;
    bool appendedAny = false;
    float sortDepth = 0.0f;

    for (const auto& desc : descriptors) {
        const glm::vec3 centeredLocal = desc.center - meshCenter;
        const glm::vec3 coneSeedLocal(
            centeredLocal.x * lateralWeight,
            centeredLocal.y * verticalWeight,
            ((centeredLocal.z - minCenteredLocalZ) + forwardBiasLocal) * forwardWeight);
        const float authoredDistance = std::max(0.0001f, glm::length(coneSeedLocal));
        const glm::vec3 worldDir =
            safeNormalize3Local(right * coneSeedLocal.x + up * coneSeedLocal.y + ringForward * coneSeedLocal.z,
                                ringForward);
        const glm::vec3 sparklePos = sparkleOrigin + worldDir * (authoredDistance * baseScale * spreadT);

        const glm::vec3 toCamera = safeNormalize3Local(cameraWorldPos - sparklePos, ringForward);
        const glm::quat billboardRot = rotationFromToSafeLocal(meshForwardLocal, toCamera);
        const glm::vec3 sparkleScale(
            std::max(0.0001f, desc.width * baseScale * sizeMul),
            1.0f,
            std::max(0.0001f, desc.height * baseScale * sizeMul));

        IRenderBackend::WorldMeshInstance instance;
        instance.modelMatrix = toModelMatrixArrayLocal(
            glm::translate(glm::mat4(1.0f), sparklePos) *
            glm::mat4_cast(billboardRot) *
            glm::scale(glm::mat4(1.0f), sparkleScale));
        instance.vertexColorMulA = passAlpha;
        batch.instances.push_back(std::move(instance));
        const float distSq = glm::dot(sparklePos - cameraWorldPos, sparklePos - cameraWorldPos);
        sortDepth = std::max(sortDepth, distSq);
        appendedAny = true;
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return true;
}

bool appendSharedGlowBillboardPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto& ring = snapshot.rings.front();
    const float life = std::max(0.0001f, ring.lifeSec);
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, false);
    authored::PassTimingState timingState;
    if (!authored::evaluatePassTiming(
            pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, 0, timingState)) {
        return false;
    }
    const float localScaleMul = authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);

    const float animatedScale =
        glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
        std::max(0.0f, pass.scaleMul) *
        localScaleMul;
    if (animatedScale <= 0.0001f) return false;

    const float passAlpha =
        std::clamp(timingState.fade * std::max(0.0f, pass.alphaMul), 0.0f, 1.0f);
    if (passAlpha <= 0.001f) return false;

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis =
        pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);

    std::vector<glm::vec3> fallbackDirections = makeFallbackDirectionsLocal(pass);
    const std::vector<glm::vec3>* localDirections = &fallbackDirections;
    if (localDirections->empty()) return false;

    const glm::vec3 finalScale(
        std::max(0.0001f, animatedScale * std::max(0.0f, pass.radiusMul)),
        1.0f,
        std::max(0.0001f, animatedScale * std::max(0.0f, pass.thicknessMul)));
    const bool hasAuthoredBillboards = !pass.authoredBillboardsLocal.empty();

    const auto& sharedIndices = quarterIndicesLocal();
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    if (hasCustomUvTransformLocal(pass)) {
        batch.vertices = makeUvTransformedQuadVerticesLocal(centeredQuadVerticesLocal(), pass);
    } else {
        const auto& sharedVertices = centeredQuadVerticesLocal();
        batch.sharedVertices = sharedVertices.data();
        batch.sharedVertexCount = sharedVertices.size();
        batch.geometryCacheKey = makeCenteredQuadGeometryCacheKeyLocal();
    }

    float sortDepth = 0.0f;
    bool appendedAny = false;
    if (hasAuthoredBillboards) {
        batch.instances.reserve(pass.authoredBillboardsLocal.size());
        const float passAlpha = std::clamp(timingState.fade * std::max(0.0f, pass.alphaMul), 0.0f, 1.0f);
        if (passAlpha <= 0.001f) return false;
        const float spinRad = computeBillboardSpinRadLocal(pass, timingState.localAge01);
        for (const auto& authored : pass.authoredBillboardsLocal) {
            const glm::vec3 localPos = authored.positionLocal * pass.authoredBillboardPositionScale;
            const glm::vec3 glowPos =
                ring.pos + right * localPos.x + up * localPos.y + ringForward * localPos.z;
            const glm::vec3 toCamera = safeNormalize3Local(cameraWorldPos - glowPos, ringForward);
            glm::quat billboardRot = rotationFromToSafeLocal(meshForwardLocal, toCamera);
            if (std::abs(spinRad) > 0.0001f) {
                billboardRot *= glm::angleAxis(spinRad, meshForwardLocal);
            }

            IRenderBackend::WorldMeshInstance instance;
            instance.modelMatrix = toModelMatrixArrayLocal(
                glm::translate(glm::mat4(1.0f), glowPos) *
                glm::mat4_cast(billboardRot) *
                glm::scale(glm::mat4(1.0f), finalScale * std::max(0.0f, authored.scaleMul)));
            instance.vertexColorMulA = passAlpha * std::max(0.0f, authored.alphaMul);
            batch.instances.push_back(std::move(instance));
            sortDepth = std::max(sortDepth, glm::dot(glowPos - cameraWorldPos, glowPos - cameraWorldPos));
            appendedAny = true;
        }

        if (!appendedAny || batch.instances.empty()) return false;
        batch.sortDepth = sortDepth;
        outBatches.push_back(std::move(batch));
        return true;
    }

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

        float instanceAlpha = passAlpha;
        if (pass.lineAlphaMax > pass.lineAlphaMin + 0.0001f) {
            const std::uint32_t passSalt = static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
            const std::uint32_t dirSalt = static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
            const float noise = hash01Local(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
            instanceAlpha *= glm::mix(pass.lineAlphaMin, pass.lineAlphaMax, noise);
        }
        instanceAlpha = std::clamp(instanceAlpha, 0.0f, 1.0f);
        if (instanceAlpha <= 0.001f) continue;

        const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
        const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
        const glm::vec3 passForward = safeNormalize3Local(worldDir, ringForward);
        const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
        const glm::vec3 radialStartOffset =
            (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
        const glm::vec3 glowPos = ring.pos + radialStartOffset + passForward * pass.forwardOffset;
        const glm::vec3 toCamera =
            safeNormalize3Local(cameraWorldPos - glowPos, ringForward);
        glm::quat billboardRot = rotationFromToSafeLocal(meshForwardLocal, toCamera);
        const float spinRad = computeBillboardSpinRadLocal(pass, timingState.localAge01);
        if (std::abs(spinRad) > 0.0001f) {
            billboardRot *= glm::angleAxis(spinRad, meshForwardLocal);
        }

        IRenderBackend::WorldMeshInstance instance;
        instance.modelMatrix = toModelMatrixArrayLocal(
            glm::translate(glm::mat4(1.0f), glowPos) *
            glm::mat4_cast(billboardRot) *
            glm::scale(glm::mat4(1.0f), finalScale));
        instance.vertexColorMulA = instanceAlpha;
        batch.instances.push_back(std::move(instance));
        sortDepth = std::max(sortDepth, glm::dot(glowPos - cameraWorldPos, glowPos - cameraWorldPos));
        appendedAny = true;
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return true;
}

bool appendSharedQuarterPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;
    if (!pass.directionsLocal.empty() && pass.directionsLocal.size() != 1u) return false;
    if (pass.directionSpacingJitterDeg > 0.0001f) return false;

    const auto& ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, true);

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    std::vector<glm::vec3> fallbackDirections;
    const std::vector<glm::vec3>* localDirections = &pass.directionsLocal;
    if (localDirections->empty()) {
        fallbackDirections = makeFallbackDirectionsLocal(pass);
        localDirections = &fallbackDirections;
    }
    if (localDirections->size() != 1u) return false;

    const glm::vec3 localDirBasisRaw = (*localDirections)[0];
    if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) return false;

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;

    const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
    const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
    if (glm::dot(worldDir, worldDir) <= 0.000001f) return false;

    const glm::vec3 passForward = glm::normalize(worldDir);
    const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
    const glm::vec3 radialStartOffset =
        (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
    const glm::vec3 passPos = ring.pos + passForward * pass.forwardOffset + radialStartOffset;
    const glm::vec3 facingDir = pass.cameraFacing
        ? safeNormalize3Local(cameraWorldPos - passPos, ringForward)
        : passForward;
    const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, facingDir);
    const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);

    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;

    const auto& sharedVertices = quarterVerticesLocal();
    const auto& sharedIndices = quarterIndicesLocal();
    const int quarterCount = std::max(1, pass.quarterCount);
    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedVertices = sharedVertices.data();
    batch.sharedVertexCount = sharedVertices.size();
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    batch.geometryCacheKey = makeQuarterGeometryCacheKeyLocal();
    batch.instances.reserve(static_cast<std::size_t>(quarterCount * timingPlan.sequenceLoopCount));
    bool appendedAny = false;
    for (int sequenceOrdinal = 0; sequenceOrdinal < timingPlan.sequenceLoopCount; ++sequenceOrdinal) {
        authored::PassTimingState timingState;
        if (!authored::evaluatePassTiming(
                pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
            continue;
        }

        const float passAlpha =
            std::clamp(timingState.fade * std::max(0.0f, pass.alphaMul), 0.0f, 1.0f);
        if (passAlpha <= 0.001f) continue;
        const float localScaleMul =
            authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);

        const float animatedScale =
            timingPlan.repeatedSequencePass
                ? computeQuarterAnimatedScaleLocal(
                      pass,
                      ring,
                      timingPlan.rawSequenceCount,
                      timingState.globalAge01,
                      timingState.localAge01)
                : (glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
                   std::max(0.0f, pass.scaleMul) *
                   localScaleMul);
        if (animatedScale <= 0.0001f) continue;
        const glm::vec3 finalScale = glm::vec3(animatedScale) * axisScale;

        for (int i = 0; i < quarterCount; ++i) {
            const float quarterDeg =
                pass.quarterStartDeg + pass.quarterStepDeg * static_cast<float>(i);
            const glm::quat quarterRot = glm::angleAxis(glm::radians(quarterDeg), meshForwardLocal);
            IRenderBackend::WorldMeshInstance instance;
            instance.modelMatrix = toModelMatrixArrayLocal(
                glm::translate(glm::mat4(1.0f), passPos) *
                glm::mat4_cast(passRot * quarterRot) *
                glm::scale(glm::mat4(1.0f), finalScale));
            instance.vertexColorMulA = passAlpha;
            batch.instances.push_back(std::move(instance));
            appendedAny = true;
        }
    }
    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = distSq;
    outBatches.push_back(std::move(batch));
    return appendedAny;
}

bool appendSharedLinePassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass,
    const authored::TevState& passTev,
    const render_model::MeshData& passMesh,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto& ring = snapshot.rings.front();
    const float life = std::max(0.0001f, ring.lifeSec);
    const float age01 = glm::clamp(ring.ageSec / life, 0.0f, 1.0f);
    const float scale =
        glm::mix(ring.startScale, ring.endScale, age01) * std::max(0.0f, pass.scaleMul);
    if (scale <= 0.0001f) return false;

    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    float fade = 1.0f;
    if (age01 > fadeStart) {
        const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
        fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
    }
    if (fade <= 0.001f) return false;

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
    const glm::vec3 passTint =
        glm::clamp(passTev.c0 * glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                   glm::vec3(0.0f),
                   glm::vec3(1.0f));

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    std::vector<glm::vec3> fallbackDirections;
    const std::vector<glm::vec3>* localDirections = &pass.directionsLocal;
    if (localDirections->empty()) {
        fallbackDirections = makeFallbackDirectionsLocal(pass);
        localDirections = &fallbackDirections;
    }
    if (localDirections->empty()) return false;

    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
    const glm::vec2 meshForwardRange = meshProjectionRangeLocal(passMesh, meshForwardLocal);
    const float meshForwardScale = glm::length(axisScale * glm::abs(meshForwardLocal));

    const std::string geometryCacheKey = makeLineGeometryCacheKeyLocal(pass, passTev.k1a);
    const SharedMeshGeometry& sharedGeometry =
        getSharedLineMeshGeometryLocal(passMesh, geometryCacheKey, passTev.k1a);
    if (sharedGeometry.vertices.empty() || sharedGeometry.indices.size() < 3u) return false;

    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    batch.sharedVertices = sharedGeometry.vertices.data();
    batch.sharedVertexCount = sharedGeometry.vertices.size();
    batch.sharedIndices = sharedGeometry.indices.data();
    batch.sharedIndexCount = sharedGeometry.indices.size();
    batch.geometryCacheKey = geometryCacheKey;
    const int rawSequenceCount = std::max(1, pass.sequenceCount);
    const int delayedSequenceIndex =
        (rawSequenceCount > 1) ? std::clamp(pass.sequenceIndex, -1, rawSequenceCount - 1) : -1;
    const bool delayedSinglePass = delayedSequenceIndex >= 0;
    const int sequenceLoopCount = delayedSinglePass ? 1 : rawSequenceCount;
    batch.instances.reserve(localDirections->size() * static_cast<std::size_t>(sequenceLoopCount));
    float sortDepth = 0.0f;
    bool appendedAny = false;
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
        const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
        const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
        if (glm::dot(worldDir, worldDir) <= 0.000001f) continue;

        const glm::vec3 passForward = glm::normalize(worldDir);
        const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
        const float radialDistanceMul =
            resolveRadialDistanceMulLocal(pass, ring.randomSeed, dirIndex, 0);
        const float radialRadius =
            pass.heightOffset * std::max(0.0f, pass.startRadiusMul) * radialDistanceMul;
        const glm::vec3 radialStartOffset =
            (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
        const glm::vec3 passPosBase =
            ring.pos + passForward * pass.forwardOffset + radialStartOffset;
        for (int sequenceOrdinal = 0; sequenceOrdinal < sequenceLoopCount; ++sequenceOrdinal) {
            const int sequenceIndex = delayedSinglePass ? delayedSequenceIndex : sequenceOrdinal;
            float localAge01 = age01;
            float localFade = 1.0f;
            if (delayedSinglePass) {
                if (!computeDelayedPassLaunchStateLocal(
                        pass, rawSequenceCount, age01, sequenceIndex, localAge01)) {
                    continue;
                }
                if (pass.sequenceFadeLocal) {
                    if (localAge01 >= 0.999f) {
                        localFade = 0.0f;
                    } else if (localAge01 > fadeStart) {
                        const float t =
                            (localAge01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
                        localFade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
                    }
                    if (localFade <= 0.001f) continue;
                } else {
                    localFade = computeSharedDelayedFadeLocal(age01, fadeStart);
                    if (localFade <= 0.001f) continue;
                }
            } else if (!computePassSequenceStateLocal(
                           pass, rawSequenceCount, age01, fadeStart, sequenceIndex, localAge01, localFade)) {
                continue;
            }

            const float animatedScale =
                glm::mix(ring.startScale, ring.endScale, localAge01) * std::max(0.0f, pass.scaleMul);
            if (animatedScale <= 0.0001f) continue;
            const glm::vec3 finalScale = glm::vec3(animatedScale) * axisScale;
            const float tailAnchorOffset =
                -meshForwardRange.x * animatedScale * meshForwardScale;
            const float forwardTravel =
                ((delayedSinglePass || rawSequenceCount > 1))
                    ? (animatedScale * std::max(radiusMul, thicknessMul) * 1.5f *
                       glm::clamp(localAge01, 0.0f, 1.0f))
                    : 0.0f;
            const glm::vec3 passPos =
                passPosBase + passForward * (tailAnchorOffset + forwardTravel);
            const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);
            sortDepth = std::max(sortDepth, distSq);

            const float passAlpha = std::clamp(localFade * lineAlphaMul, 0.0f, 1.0f);
            if (passAlpha <= 0.001f) continue;

            IRenderBackend::WorldMeshInstance instance;
            instance.modelMatrix = toModelMatrixArrayLocal(
                glm::translate(glm::mat4(1.0f), passPos) *
                glm::mat4_cast(passRot) *
                glm::scale(glm::mat4(1.0f), finalScale));
            instance.vertexColorMulR = passTint.r;
            instance.vertexColorMulG = passTint.g;
            instance.vertexColorMulB = passTint.b;
            instance.vertexColorMulA = passAlpha;
            batch.instances.push_back(std::move(instance));
            appendedAny = true;
        }
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return appendedAny;
}

bool appendSharedStreakQuadPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass,
    const authored::TevState& passTev,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;

    const auto& ring = snapshot.rings.front();
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);
    const auto timingPlan = authored::planPassTiming(pass, true);

    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis ? pass.meshForwardAxis : defaultMeshForward;
    const glm::vec3 meshForwardLocal =
        (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : glm::normalize(passMeshForwardAxis);
    const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
    const glm::vec3 passTint =
        glm::clamp(passTev.c0 * glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                   glm::vec3(0.0f),
                   glm::vec3(1.0f));

    const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
    right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::cross(ringForward, right);
    up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

    const auto& authoredSegments = authored::resolveAuthoredStreakSegments(pass);
    if (!authoredSegments.empty()) {
        const float radiusMul = std::max(0.0f, pass.radiusMul);
        const float thicknessMul = std::max(0.0f, pass.thicknessMul);
        const float visualScaleMul = std::max(0.0f, pass.scaleMul);
        const bool centerOrigin = pass.authoredSegmentCenterOrigin;
        const float positionScale = std::max(0.0f, pass.authoredSegmentPositionScale);
        const float lengthScale = std::max(0.0f, pass.authoredSegmentLengthScale);

        shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
        const auto& sharedVertices = streakQuadVerticesLocal();
        const auto& sharedIndices = quarterIndicesLocal();
        batch.sharedVertices = sharedVertices.data();
        batch.sharedVertexCount = sharedVertices.size();
        batch.sharedIndices = sharedIndices.data();
        batch.sharedIndexCount = sharedIndices.size();
        batch.geometryCacheKey = makeStreakQuadGeometryCacheKeyLocal(passTev.k1a);

        batch.instances.reserve(authoredSegments.size() * static_cast<std::size_t>(timingPlan.sequenceLoopCount));
        float sortDepth = 0.0f;
        bool appendedAny = false;
        for (const auto& segment : authoredSegments) {
            for (int sequenceOrdinal = 0; sequenceOrdinal < timingPlan.sequenceLoopCount; ++sequenceOrdinal) {
                authored::PassTimingState timingState;
                if (!authored::evaluatePassTiming(
                        pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
                    continue;
                }

                const float localScaleMul =
                    authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);
                const float spreadScale =
                    glm::mix(ring.startScale, ring.endScale, timingState.localAge01) * localScaleMul;
                if (spreadScale <= 0.0001f || visualScaleMul <= 0.0001f) continue;

                const glm::vec3 segmentDirection = authored::resolveAuthoredStreakDirection(segment);
                const float segmentLengthBase = authored::resolveAuthoredStreakLength(segment);
                if (segmentLengthBase <= 0.0001f) continue;
                const float lengthDecay =
                    authored::resolveAuthoredDecayFactor(
                        pass,
                        timingState.localAge01,
                        ring.lifeSec,
                        pass.authoredSegmentLengthDecayPerFrame);
                const float alphaDecay =
                    authored::resolveAuthoredDecayFactor(
                        pass,
                        timingState.localAge01,
                        ring.lifeSec,
                        pass.authoredSegmentAlphaDecayPerFrame);
                const float widthDecay = lengthDecay;

                glm::vec3 localStart(0.0f);
                glm::vec3 localVector(0.0f);
                if (centerOrigin) {
                    const float travelDistance =
                        authored::resolveAuthoredStreakTravelDistance(
                            pass,
                            timingState.localAge01,
                            ring.lifeSec);
                    localStart = segmentDirection * travelDistance;
                    localVector =
                        segmentDirection *
                        (segmentLengthBase * lengthScale * lengthDecay);
                } else {
                    localStart = segment.startLocal * (spreadScale * positionScale);
                    localVector =
                        (segment.endLocal - segment.startLocal) *
                        (spreadScale * lengthScale * lengthDecay);
                }
                const glm::vec3 localEnd = localStart + localVector;
                const float segmentLength = glm::length(localVector);
                if (segmentLength <= 0.0001f) continue;
                const float visibilityFade =
                    authored::resolveAuthoredStreakVisibilityFade(
                        pass,
                        localStart);
                if (visibilityFade <= 0.001f) continue;

                const glm::vec3 worldStart =
                    ring.pos +
                    right * localStart.x +
                    up * localStart.y +
                    ringForward * (localStart.z + pass.forwardOffset);
                const glm::vec3 worldEnd =
                    ring.pos +
                    right * localEnd.x +
                    up * localEnd.y +
                    ringForward * (localEnd.z + pass.forwardOffset);
                const glm::vec3 worldVector = worldEnd - worldStart;
                if (glm::dot(worldVector, worldVector) <= 0.000001f) continue;

                const glm::vec3 passForward = glm::normalize(worldVector);
                const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);

                IRenderBackend::WorldMeshInstance instance;
                instance.modelMatrix = toModelMatrixArrayLocal(
                    glm::translate(glm::mat4(1.0f), worldStart) *
                    glm::mat4_cast(passRot) *
                    glm::scale(
                        glm::mat4(1.0f),
                        glm::vec3(
                            visualScaleMul * radiusMul * widthDecay,
                            visualScaleMul * radiusMul * widthDecay,
                            visualScaleMul * thicknessMul * segmentLength)));
                instance.vertexColorMulR = passTint.r;
                instance.vertexColorMulG = passTint.g;
                instance.vertexColorMulB = passTint.b;
                instance.vertexColorMulA =
                    std::clamp(timingState.fade * std::max(0.0f, pass.alphaMul) *
                                   std::max(0.0f, segment.alphaMul) * alphaDecay * lengthDecay * visibilityFade,
                               0.0f,
                               1.0f);
                batch.instances.push_back(std::move(instance));
                const glm::vec3 worldMid = 0.5f * (worldStart + worldEnd);
                sortDepth =
                    std::max(sortDepth, glm::dot(worldMid - cameraWorldPos, worldMid - cameraWorldPos));
                appendedAny = true;
            }
        }

        if (!appendedAny || batch.instances.empty()) return false;
        batch.sortDepth = sortDepth;
        outBatches.push_back(std::move(batch));
        return true;
    }

    std::vector<glm::vec3> fallbackDirections = makeFallbackDirectionsLocal(pass);
    const std::vector<glm::vec3>* localDirections = &fallbackDirections;
    if (localDirections->empty()) return false;

    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;

    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    const auto& sharedVertices = streakQuadVerticesLocal();
    const auto& sharedIndices = quarterIndicesLocal();
    batch.sharedVertices = sharedVertices.data();
    batch.sharedVertexCount = sharedVertices.size();
    batch.sharedIndices = sharedIndices.data();
    batch.sharedIndexCount = sharedIndices.size();
    batch.geometryCacheKey = makeStreakQuadGeometryCacheKeyLocal(passTev.k1a);

    batch.instances.reserve(localDirections->size() * static_cast<std::size_t>(timingPlan.sequenceLoopCount));
    float sortDepth = 0.0f;
    bool appendedAny = false;
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
        const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
        const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
        if (glm::dot(worldDir, worldDir) <= 0.000001f) continue;

        const glm::vec3 passForward = glm::normalize(worldDir);
        const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
        for (int sequenceOrdinal = 0; sequenceOrdinal < timingPlan.sequenceLoopCount; ++sequenceOrdinal) {
            authored::PassTimingState timingState;
            if (!authored::evaluatePassTiming(
                    pass, ring.ageSec, ring.lifeSec, fadeStart, timingPlan, sequenceOrdinal, timingState)) {
                continue;
            }
            const float localScaleMul =
                authored::resolveLocalScaleMul(pass, timingState.localAge01, ring.lifeSec);

            const float animatedScale =
                glm::mix(ring.startScale, ring.endScale, timingState.localAge01) *
                std::max(0.0f, pass.scaleMul) *
                localScaleMul;
            if (animatedScale <= 0.0001f) continue;
            const glm::vec3 finalScale = glm::vec3(animatedScale) * axisScale;
            const float radialDistanceMul =
                resolveRadialDistanceMulLocal(pass, ring.randomSeed, dirIndex, sequenceOrdinal);
            const float radialRadius =
                pass.heightOffset * std::max(0.0f, pass.startRadiusMul) * radialDistanceMul;
            const glm::vec3 radialStartOffset =
                (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
            const glm::vec3 passPosBase =
                ring.pos + passForward * pass.forwardOffset + radialStartOffset;
            const float forwardTravel =
                animatedScale * std::max(radiusMul, thicknessMul) * 1.5f *
                glm::clamp(timingState.localAge01, 0.0f, 1.0f);
            const glm::vec3 passPos = passPosBase + passForward * forwardTravel;

            IRenderBackend::WorldMeshInstance instance;
            instance.modelMatrix = toModelMatrixArrayLocal(
                glm::translate(glm::mat4(1.0f), passPos) *
                glm::mat4_cast(passRot) *
                glm::scale(glm::mat4(1.0f), finalScale));
            instance.vertexColorMulR = passTint.r;
            instance.vertexColorMulG = passTint.g;
            instance.vertexColorMulB = passTint.b;
            instance.vertexColorMulA = std::clamp(timingState.fade * lineAlphaMul, 0.0f, 1.0f);
            batch.instances.push_back(std::move(instance));
            sortDepth = std::max(sortDepth, glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos));
            appendedAny = true;
        }
    }

    if (!appendedAny || batch.instances.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return appendedAny;
}

bool appendDynamicPassBatchLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
    const SharedAuthoredBatchVFX::Config::DrawPass& pass,
    const authored::TevState& passTev,
    const render_model::MeshData* passMesh,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    const bool drawQuarterRing = pass.textureQuarterRing;
    const bool quarterTextureBake =
        authored::usesQuarterTextureBake(snapshot.config, pass);
    const bool drawLinePass = authored::isLinePass(snapshot.config, pass);
    const glm::vec3 defaultMeshForward =
        (glm::dot(snapshot.config.meshForwardAxis, snapshot.config.meshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(snapshot.config.meshForwardAxis);
    const float fadeStart = glm::clamp(snapshot.config.fadeStart, 0.0f, 1.0f);

    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);

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

        const glm::vec3 ringForward = safeNormalize3Local(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
        right = safeNormalize3Local(right, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 up = glm::cross(ringForward, right);
        up = safeNormalize3Local(up, glm::vec3(0.0f, 1.0f, 0.0f));

        std::vector<glm::vec3> localDirectionsFallback = makeFallbackDirectionsLocal(pass);
        const std::vector<glm::vec3>* localDirections = &localDirectionsFallback;
        if (localDirections->empty()) continue;

        const float radiusMul = std::max(0.0f, pass.radiusMul);
        const float thicknessMul = std::max(0.0f, pass.thicknessMul);
        const glm::vec3 axisScale =
            glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
        const int rawSequenceCount = std::max(1, pass.sequenceCount);
        const int delayedSequenceIndex =
            (rawSequenceCount > 1) ? std::clamp(pass.sequenceIndex, -1, rawSequenceCount - 1) : -1;
        const bool delayedSinglePass = delayedSequenceIndex >= 0;
        const bool repeatedSequencePass =
            ((drawQuarterRing || drawLinePass) && rawSequenceCount > 1 && !delayedSinglePass);
        const int sequenceLoopCount = repeatedSequencePass ? rawSequenceCount : 1;

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
            const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
            const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
            if (glm::dot(worldDir, worldDir) <= 0.000001f) continue;

            const glm::vec3 passForward = glm::normalize(worldDir);
            const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
            const float radialDistanceMul =
                resolveRadialDistanceMulLocal(pass, ring.randomSeed, dirIndex, 0);
            const float radialRadius =
                pass.heightOffset * std::max(0.0f, pass.startRadiusMul) * radialDistanceMul;
            const glm::vec3 radialStartOffset =
                (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
            const glm::vec3 passPosBase = ring.pos + radialStartOffset;

            for (int sequenceOrdinal = 0; sequenceOrdinal < sequenceLoopCount; ++sequenceOrdinal) {
                const int sequenceIndex = delayedSinglePass ? delayedSequenceIndex : sequenceOrdinal;
                float localAge01 = age01;
                float fade = 1.0f;
                if (repeatedSequencePass) {
                    if (!computePassSequenceStateLocal(
                            pass, rawSequenceCount, age01, fadeStart, sequenceIndex, localAge01, fade)) {
                        continue;
                    }
                } else if (delayedSinglePass) {
                    if (!computeDelayedPassLaunchStateLocal(
                            pass, rawSequenceCount, age01, sequenceIndex, localAge01)) {
                        continue;
                    }
                    if (pass.sequenceFadeLocal) {
                        if (localAge01 >= 0.999f) {
                            fade = 0.0f;
                        } else if (localAge01 > fadeStart) {
                            const float t =
                                (localAge01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
                            fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
                        }
                    } else {
                        fade = computeSharedDelayedFadeLocal(age01, fadeStart);
                    }
                    if (fade <= 0.001f) continue;
                } else {
                    if (age01 > fadeStart) {
                        const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
                        fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
                    }
                    if (fade <= 0.001f) continue;
                }

                float passAlphaScale = std::clamp(fade * lineAlphaMul, 0.0f, 1.0f);
                if (!quarterTextureBake && !drawLinePass) {
                    passAlphaScale *= passTev.k1a;
                }
                const float passAlpha = std::clamp(passAlphaScale, 0.0f, 1.0f);
                if (passAlpha <= 0.001f) continue;

                const float animatedScale = drawQuarterRing
                    ? computeQuarterAnimatedScaleLocal(pass, ring, rawSequenceCount, age01, localAge01)
                    : ((drawLinePass && (repeatedSequencePass || delayedSinglePass))
                        ? (glm::mix(ring.startScale, ring.endScale, localAge01) *
                           std::max(0.0f, pass.scaleMul))
                        : (glm::mix(ring.startScale, ring.endScale, delayedSinglePass ? localAge01 : age01) *
                           std::max(0.0f, pass.scaleMul)));
                if (animatedScale <= 0.0001f) continue;
                const glm::vec3 finalScale = glm::vec3(animatedScale) * axisScale;
                const float forwardTravel =
                    (drawLinePass && (repeatedSequencePass || delayedSinglePass))
                        ? (pass.forwardOffset +
                           animatedScale * std::max(radiusMul, thicknessMul) * 1.5f *
                               glm::clamp(localAge01, 0.0f, 1.0f))
                        : ((delayedSinglePass && !drawQuarterRing)
                               ? (pass.forwardOffset * fastLaunch01Local(localAge01))
                               : pass.forwardOffset);
                const glm::vec3 passPos = passPosBase + passForward * forwardTravel;
                const glm::vec3 facingDir = (drawQuarterRing && pass.cameraFacing)
                    ? safeNormalize3Local(cameraWorldPos - passPos, ringForward)
                    : passForward;
                const glm::quat orientedPassRot =
                    rotationFromToSafeLocal(meshForwardLocal, facingDir);
                const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);
                sortDepth = std::max(sortDepth, distSq);

                const glm::vec4 color(passTint, passAlpha);
                if (drawQuarterRing) {
                    const int quarterCount = std::max(1, pass.quarterCount);
                    for (int i = 0; i < quarterCount; ++i) {
                        const float quarterDeg =
                            pass.quarterStartDeg + pass.quarterStepDeg * static_cast<float>(i);
                        const glm::quat quarterRot =
                            glm::angleAxis(glm::radians(quarterDeg), meshForwardLocal);
                        const glm::mat4 world =
                            glm::translate(glm::mat4(1.0f), passPos) *
                            glm::mat4_cast(orientedPassRot * quarterRot) *
                            glm::scale(glm::mat4(1.0f), finalScale);
                        appendQuarterRingLocal(batch, world, color);
                        hasGeometry = true;
                    }
                } else if (passMesh) {
                    const glm::mat4 world =
                        glm::translate(glm::mat4(1.0f), passPos) *
                        glm::mat4_cast(orientedPassRot) *
                        glm::scale(glm::mat4(1.0f), finalScale);
                    appendTransformedMeshLocal(
                        batch, *passMesh, world, color, drawLinePass, passTev.k1a);
                    hasGeometry = true;
                }
            }
        }
    }

    if (!hasGeometry || batch.vertices.empty() || batch.indices.empty()) return false;
    batch.sortDepth = sortDepth;
    outBatches.push_back(std::move(batch));
    return true;
}

} // namespace

bool appendPassBatch(std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
                     const SharedAuthoredBatchVFX::RenderSnapshot& snapshot,
                     const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                     const authored::TevState& passTev,
                     const render_model::MeshData* passMesh,
                     const TextureView& texture,
                     const glm::vec3& cameraWorldPos) {
    if (!pass.enabled) return false;
    if (snapshot.rings.empty()) return false;
    if (texture.rgba == nullptr || texture.width <= 0 || texture.height <= 0) return false;

    const bool drawQuarterRing = pass.textureQuarterRing;
    const bool glowBillboardPass = authored::isGlowBillboardPass(pass);
    const bool streakQuadPass = authored::isStreakQuadPass(pass);
    if (!drawQuarterRing && !glowBillboardPass && !streakQuadPass && passMesh == nullptr) return false;

    const bool drawLinePass = authored::isLinePass(snapshot.config, pass);
    const bool singleRingSnapshot = snapshot.rings.size() == 1u;
    if (drawLinePass && streakQuadPass && singleRingSnapshot) {
        return appendSharedStreakQuadPassSingleRingLocal(
            outBatches, snapshot, pass, passTev, texture, cameraWorldPos);
    }
    const bool sparkleMeshPass = authored::isSparkleMeshPass(pass);
    if (drawLinePass && passMesh && singleRingSnapshot) {
        return appendSharedLinePassSingleRingLocal(
            outBatches, snapshot, pass, passTev, *passMesh, texture, cameraWorldPos);
    }

    if (sparkleMeshPass && passMesh && singleRingSnapshot) {
        return appendSharedSparkleBillboardPassSingleRingLocal(
            outBatches, snapshot, pass, *passMesh, texture, cameraWorldPos);
    }

    if (glowBillboardPass && singleRingSnapshot) {
        return appendSharedGlowBillboardPassSingleRingLocal(
            outBatches, snapshot, pass, texture, cameraWorldPos);
    }

    if (!drawLinePass && passMesh &&
        appendSharedMeshPassSingleRingLocal(
            outBatches, snapshot, pass, passTev, *passMesh, texture, cameraWorldPos)) {
        return true;
    }

    if (drawQuarterRing && singleRingSnapshot &&
        (pass.directionsLocal.empty() || pass.directionsLocal.size() == 1u) &&
        pass.directionSpacingJitterDeg <= 0.0001f) {
        return appendSharedQuarterPassSingleRingLocal(
            outBatches, snapshot, pass, texture, cameraWorldPos);
    }

    return appendDynamicPassBatchLocal(
        outBatches, snapshot, pass, passTev, passMesh, texture, cameraWorldPos);
}

} // namespace vfx::runtime::authored_batches
