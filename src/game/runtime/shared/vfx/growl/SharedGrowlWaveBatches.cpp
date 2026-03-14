#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBatches.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace game::runtime::shared_growl_batches {
namespace {

struct SharedMeshGeometry {
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
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

std::array<float, 16> toModelMatrixArrayLocal(const glm::mat4& matrix) {
    std::array<float, 16> out{};
    std::memcpy(out.data(), &matrix[0][0], sizeof(float) * out.size());
    return out;
}

std::vector<glm::vec3> makeFallbackDirectionsLocal(const GrowlWaveVFX::Config::DrawPass& pass) {
    std::vector<glm::vec3> directions;
    directions.push_back(
        pass.overrideDirection ? pass.directionLocal : glm::vec3(0.0f, 0.0f, 1.0f));
    return directions;
}

const SharedMeshGeometry& getSharedMeshGeometryLocal(const render_model::MeshData& mesh) {
    static thread_local std::unordered_map<const render_model::MeshData*, SharedMeshGeometry> cache;

    const auto found = cache.find(&mesh);
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

    return cache.emplace(&mesh, std::move(geometry)).first->second;
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

const std::array<std::uint32_t, 6>& quarterIndicesLocal() {
    static const std::array<std::uint32_t, 6> kIndices = {0u, 1u, 2u, 2u, 1u, 3u};
    return kIndices;
}

std::string makeMeshGeometryCacheKeyLocal(const GrowlWaveVFX::Config::DrawPass& pass) {
    return std::string("__growl_geom_mesh_v1__:") + pass.meshPath;
}

const char* quarterGeometryCacheKeyLocal() {
    return "__growl_geom_quarter_unit_v1__";
}

shared_world_batches::WorldIndexedBatch makeBaseBatchLocal(
    const GrowlWaveVFX::RenderSnapshot& snapshot,
    const GrowlWaveVFX::Config::DrawPass& pass,
    const TextureView& texture) {
    shared_world_batches::WorldIndexedBatch batch;
    batch.textureKey =
        std::string("growl:") + pass.id + ":" +
        (pass.texturePath.empty() ? std::string("__white__") : pass.texturePath);
    batch.textureCacheKey = shared_growl::makeTextureCacheKey(snapshot.config, pass);
    batch.textureRgba = texture.rgba;
    batch.textureWidth = texture.width;
    batch.textureHeight = texture.height;
    batch.textureWrapS = 10497;
    batch.textureWrapT = 10497;
    batch.alphaMode = 2u;
    batch.blendMode = 1u; // Legacy growl passes use additive blending.
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

bool appendSharedMeshPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const GrowlWaveVFX::RenderSnapshot& snapshot,
    const GrowlWaveVFX::Config::DrawPass& pass,
    const shared_growl::TevState& passTev,
    const render_model::MeshData& passMesh,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;
    if (!pass.directionsLocal.empty() && pass.directionsLocal.size() != 1u) return false;
    if (pass.directionSpacingJitterDeg > 0.0001f) return false;

    const auto& ring = snapshot.rings.front();
    const float life = std::max(0.0001f, ring.lifeSec);
    const float age01 = glm::clamp(ring.ageSec / life, 0.0f, 1.0f);
    const float scale =
        glm::mix(ring.startScale, ring.endScale, age01) * std::max(0.0f, pass.scaleMul);
    if (scale <= 0.0001f) return false;

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

    float fade = 1.0f;
    if (age01 > fadeStart) {
        const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
        fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
    }
    if (fade <= 0.001f) return false;

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

    float passAlphaScale = std::clamp(fade * std::max(0.0f, pass.alphaMul), 0.0f, 1.0f);
    passAlphaScale *= passTev.k1a;
    const float passAlpha = std::clamp(passAlphaScale, 0.0f, 1.0f);
    if (passAlpha <= 0.001f) return false;

    const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
    const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
    if (glm::dot(worldDir, worldDir) <= 0.000001f) return false;

    const glm::vec3 passForward = glm::normalize(worldDir);
    const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
    const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
    const glm::vec3 radialStartOffset =
        (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
    const glm::vec3 passPos = ring.pos + passForward * pass.forwardOffset + radialStartOffset;
    const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);

    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
    const glm::vec3 finalScale = glm::vec3(scale) * axisScale;

    shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
    const SharedMeshGeometry& sharedGeometry = getSharedMeshGeometryLocal(passMesh);
    if (sharedGeometry.vertices.empty() || sharedGeometry.indices.size() < 3u) return false;

    batch.sharedVertices = sharedGeometry.vertices.data();
    batch.sharedVertexCount = sharedGeometry.vertices.size();
    batch.sharedIndices = sharedGeometry.indices.data();
    batch.sharedIndexCount = sharedGeometry.indices.size();
    batch.geometryCacheKey = makeMeshGeometryCacheKeyLocal(pass);
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

bool appendSharedQuarterPassSingleRingLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const GrowlWaveVFX::RenderSnapshot& snapshot,
    const GrowlWaveVFX::Config::DrawPass& pass,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    if (snapshot.rings.size() != 1u) return false;
    if (!pass.directionsLocal.empty() && pass.directionsLocal.size() != 1u) return false;
    if (pass.directionSpacingJitterDeg > 0.0001f) return false;

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

    const float passAlpha = std::clamp(fade * std::max(0.0f, pass.alphaMul), 0.0f, 1.0f);
    if (passAlpha <= 0.001f) return false;

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
    const glm::quat passRot = rotationFromToSafeLocal(meshForwardLocal, passForward);
    const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
    const glm::vec3 radialStartOffset =
        (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
    const glm::vec3 passPos = ring.pos + passForward * pass.forwardOffset + radialStartOffset;
    const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);

    const float radiusMul = std::max(0.0f, pass.radiusMul);
    const float thicknessMul = std::max(0.0f, pass.thicknessMul);
    const glm::vec3 axisScale =
        glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
    const glm::vec3 finalScale = glm::vec3(scale) * axisScale;

    const auto& sharedVertices = quarterVerticesLocal();
    const auto& sharedIndices = quarterIndicesLocal();
    const int quarterCount = std::max(1, pass.quarterCount);
    bool appendedAny = false;
    for (int i = 0; i < quarterCount; ++i) {
        const float quarterDeg =
            pass.quarterStartDeg + pass.quarterStepDeg * static_cast<float>(i);
        const glm::quat quarterRot = glm::angleAxis(glm::radians(quarterDeg), meshForwardLocal);

        shared_world_batches::WorldIndexedBatch batch = makeBaseBatchLocal(snapshot, pass, texture);
        batch.sharedVertices = sharedVertices.data();
        batch.sharedVertexCount = sharedVertices.size();
        batch.sharedIndices = sharedIndices.data();
        batch.sharedIndexCount = sharedIndices.size();
        batch.geometryCacheKey = quarterGeometryCacheKeyLocal();
        batch.vertexColorMulR = 1.0f;
        batch.vertexColorMulG = 1.0f;
        batch.vertexColorMulB = 1.0f;
        batch.vertexColorMulA = passAlpha;
        batch.modelMatrix = toModelMatrixArrayLocal(
            glm::translate(glm::mat4(1.0f), passPos) *
            glm::mat4_cast(passRot * quarterRot) *
            glm::scale(glm::mat4(1.0f), finalScale));
        batch.sortDepth = distSq;
        outBatches.push_back(std::move(batch));
        appendedAny = true;
    }
    return appendedAny;
}

bool appendDynamicPassBatchLocal(
    std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
    const GrowlWaveVFX::RenderSnapshot& snapshot,
    const GrowlWaveVFX::Config::DrawPass& pass,
    const shared_growl::TevState& passTev,
    const render_model::MeshData* passMesh,
    const TextureView& texture,
    const glm::vec3& cameraWorldPos) {
    const bool drawQuarterRing = pass.textureQuarterRing;
    const bool drawLinePass = shared_growl::isLinePass(snapshot.config, pass);
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
            localDirectionsFallback = makeFallbackDirectionsLocal(pass);
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
    if (!drawLinePass && passMesh &&
        appendSharedMeshPassSingleRingLocal(
            outBatches, snapshot, pass, passTev, *passMesh, texture, cameraWorldPos)) {
        return true;
    }

    if (drawQuarterRing &&
        appendSharedQuarterPassSingleRingLocal(
            outBatches, snapshot, pass, texture, cameraWorldPos)) {
        return true;
    }

    return appendDynamicPassBatchLocal(
        outBatches, snapshot, pass, passTev, passMesh, texture, cameraWorldPos);
}

} // namespace game::runtime::shared_growl_batches
