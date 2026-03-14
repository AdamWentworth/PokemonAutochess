#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/NeutralPmrem.h"
#include "engine/render/RendererParityContract.h"
#include "engine/core/Environment.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <glad/glad.h>

namespace {

float resolveVertexChannel(float channel, float fallback) {
    return (channel >= 0.0f) ? channel : fallback;
}

bool pbrBindingLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

int pbrDebugViewMode() {
    static const int mode = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_DEBUG_VIEW");
        if (!env.has_value()) return 0;
        try {
            return std::clamp(std::atoi(env->c_str()), 0, 8);
        } catch (...) {
            return 0;
        }
    }();
    return mode;
}

int pbrBindingLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG_MAX");
        if (!env.has_value()) return 64;
        try {
            return (std::max)(1, std::atoi(env->c_str()));
        } catch (...) {
            return 64;
        }
    }();
    return maxEntries;
}

void maybeLogPbrBindingOpenGL(const IRenderBackend::WorldTextureData* texture,
                              bool hasBase,
                              bool hasNormal,
                              bool hasMetallicRoughness,
                              bool hasOcclusion,
                              bool hasEmissive) {
    if (!pbrBindingLogEnabled()) return;
    if (!texture || texture->materialMode < 2u) return;
    static int sPrinted = 0;
    if (sPrinted >= pbrBindingLogMaxEntries()) return;
    ++sPrinted;
    std::cout
        << "[PBRBind][OpenGL] key=" << (texture->key ? texture->key : "<null>")
        << " mode=" << static_cast<int>(texture->materialMode)
        << " has(base/norm/mr/occ/emi)="
        << (hasBase ? "1" : "0") << "/"
        << (hasNormal ? "1" : "0") << "/"
        << (hasMetallicRoughness ? "1" : "0") << "/"
        << (hasOcclusion ? "1" : "0") << "/"
        << (hasEmissive ? "1" : "0")
        << " texSize=" << texture->width << "x" << texture->height
        << " normSize=" << texture->normalWidth << "x" << texture->normalHeight
        << " mrSize=" << texture->metallicRoughnessWidth << "x" << texture->metallicRoughnessHeight
        << " roughF=" << texture->roughnessFactor
        << " metalF=" << texture->metallicFactor
        << " occF=" << texture->occlusionStrength
        << "\n";
}

struct OpenGLWorldInstanceVertexData {
    float model[16];
    float color[4];
};

static_assert(sizeof(OpenGLWorldInstanceVertexData) == sizeof(float) * 20u);

void packWorldInstanceVertexData(const IRenderBackend::WorldMeshInstance& instance,
                                 OpenGLWorldInstanceVertexData& out) {
    for (std::size_t i = 0; i < 16u; ++i) {
        out.model[i] = instance.modelMatrix[i];
    }
    out.color[0] = instance.vertexColorMulR;
    out.color[1] = instance.vertexColorMulG;
    out.color[2] = instance.vertexColorMulB;
    out.color[3] = instance.vertexColorMulA;
}

OpenGLWorldInstanceVertexData makeIdentityInstanceData() {
    OpenGLWorldInstanceVertexData out{};
    out.model[0] = 1.0f;
    out.model[5] = 1.0f;
    out.model[10] = 1.0f;
    out.model[15] = 1.0f;
    out.color[0] = 1.0f;
    out.color[1] = 1.0f;
    out.color[2] = 1.0f;
    out.color[3] = 1.0f;
    return out;
}

} // namespace

void OpenGLRenderBackend::beginWorldIndexedBatchSubmission() {
    if (worldIndexedBatchSubmissionState_.active) {
        ++worldIndexedBatchSubmissionState_.depth;
        return;
    }

    auto& state = worldIndexedBatchSubmissionState_;
    state.active = true;
    state.depth = 1;
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.prevArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state.prevElementArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.prevActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.prevTexture2DOnActive);
    for (int unit = 0; unit < 6; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.prevTexture2DOnUnit[static_cast<std::size_t>(unit)]);
    }
    glActiveTexture(static_cast<GLenum>(state.prevActiveTexture));

    state.depthEnabled = (glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
    state.blendEnabled = (glIsEnabled(GL_BLEND) == GL_TRUE);
    state.cullEnabled = (glIsEnabled(GL_CULL_FACE) == GL_TRUE);
    glGetIntegerv(GL_FRONT_FACE, &state.prevFrontFace);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    state.prevDepthMask = (prevDepthMask == GL_TRUE);
    glGetIntegerv(GL_DEPTH_FUNC, &state.prevDepthFunc);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.prevBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.prevBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.prevBlendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.prevBlendEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.prevBlendEqAlpha);
    state.currentProgram = state.prevProgram;
    state.currentVao = state.prevVao;
    state.currentArrayBuffer = state.prevArrayBuffer;
    state.currentElementArrayBuffer = state.prevElementArrayBuffer;
    state.currentActiveTexture = state.prevActiveTexture;
    state.currentTexture2DOnUnit = state.prevTexture2DOnUnit;
    state.currentDepthEnabled = state.depthEnabled;
    state.currentBlendEnabled = state.blendEnabled;
    state.currentCullEnabled = state.cullEnabled;
    state.currentFrontFace = state.prevFrontFace;
    state.currentDepthMask = state.prevDepthMask;
    state.currentDepthFunc = state.prevDepthFunc;
    state.currentBlendSrcRgb = state.prevBlendSrcRgb;
    state.currentBlendDstRgb = state.prevBlendDstRgb;
    state.currentBlendSrcAlpha = state.prevBlendSrcAlpha;
    state.currentBlendDstAlpha = state.prevBlendDstAlpha;
    state.currentBlendEqRgb = state.prevBlendEqRgb;
    state.currentBlendEqAlpha = state.prevBlendEqAlpha;
    state.worldProgramStaticUniformsApplied = false;
    state.worldProgramDynamicUniforms = {};
}

void OpenGLRenderBackend::endWorldIndexedBatchSubmission() {
    auto& state = worldIndexedBatchSubmissionState_;
    if (!state.active) return;
    if (state.depth > 1) {
        --state.depth;
        return;
    }

    glBindVertexArray(static_cast<GLuint>(state.prevVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.prevArrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(state.prevElementArrayBuffer));
    glUseProgram(static_cast<GLuint>(state.prevProgram));

    for (int unit = 0; unit < 6; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.prevTexture2DOnUnit[static_cast<std::size_t>(unit)]));
    }
    glActiveTexture(static_cast<GLenum>(state.prevActiveTexture));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.prevTexture2DOnActive));

    glDepthMask(state.prevDepthMask ? GL_TRUE : GL_FALSE);
    glDepthFunc(static_cast<GLenum>(state.prevDepthFunc));
    glBlendEquationSeparate(static_cast<GLenum>(state.prevBlendEqRgb), static_cast<GLenum>(state.prevBlendEqAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(state.prevBlendSrcRgb),
                        static_cast<GLenum>(state.prevBlendDstRgb),
                        static_cast<GLenum>(state.prevBlendSrcAlpha),
                        static_cast<GLenum>(state.prevBlendDstAlpha));
    if (state.blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    glFrontFace(static_cast<GLenum>(state.prevFrontFace));
    if (state.cullEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (state.depthEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    state = WorldIndexedBatchSubmissionState{};
}

OpenGLRenderBackend::CachedWorldMesh* OpenGLRenderBackend::ensureCachedWorldMesh(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount) {
    if (!geometryKey || geometryKey[0] == '\0' || !vertices || !indices || vertexCount == 0u || indexCount < 3u) {
        return nullptr;
    }
    ensureWorldPipeline();
    if (worldInstanceVbo_ == 0u) return nullptr;

    constexpr std::size_t kMaxWorldVertices = 540000u;
    constexpr std::size_t kMaxWorldIndices = 900000u;
    const std::size_t safeVertexCount = std::min(vertexCount, kMaxWorldVertices);
    const std::size_t safeIndexCount = std::min(indexCount, kMaxWorldIndices);
    if (safeVertexCount == 0u || safeIndexCount < 3u) return nullptr;
    for (std::size_t i = 0; i < safeIndexCount; ++i) {
        if (indices[i] >= safeVertexCount) return nullptr;
    }

    const std::string key(geometryKey);
    const std::size_t vertexBytes = safeVertexCount * sizeof(WorldMeshVertex);
    const std::size_t indexBytes = safeIndexCount * sizeof(std::uint32_t);

    auto existing = cachedWorldMeshes_.find(key);
    if (existing != cachedWorldMeshes_.end()) {
        CachedWorldMesh& mesh = existing->second;
        if (mesh.valid &&
            mesh.vertexCount == safeVertexCount &&
            mesh.indexCount == safeIndexCount &&
            mesh.vertexBytes == vertexBytes &&
            mesh.indexBytes == indexBytes) {
            return &mesh;
        }
        if (mesh.indexBuffer != 0u) glDeleteBuffers(1, &mesh.indexBuffer);
        if (mesh.vertexBuffer != 0u) glDeleteBuffers(1, &mesh.vertexBuffer);
        if (mesh.vao != 0u) glDeleteVertexArrays(1, &mesh.vao);
        cachedWorldMeshes_.erase(existing);
    }

    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    CachedWorldMesh mesh{};
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vertexBuffer);
    glGenBuffers(1, &mesh.indexBuffer);
    if (mesh.vao == 0u || mesh.vertexBuffer == 0u || mesh.indexBuffer == 0u) {
        if (mesh.indexBuffer != 0u) glDeleteBuffers(1, &mesh.indexBuffer);
        if (mesh.vertexBuffer != 0u) glDeleteBuffers(1, &mesh.vertexBuffer);
        if (mesh.vao != 0u) glDeleteVertexArrays(1, &mesh.vao);
        glBindVertexArray(static_cast<GLuint>(prevVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
        return nullptr;
    }

    configureWorldMeshVertexLayout(mesh.vao, mesh.vertexBuffer, mesh.indexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertexBytes),
                 vertices,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indexBytes),
                 indices,
                 GL_STATIC_DRAW);

    mesh.vertexCount = safeVertexCount;
    mesh.indexCount = safeIndexCount;
    mesh.vertexBytes = vertexBytes;
    mesh.indexBytes = indexBytes;
    mesh.valid = true;

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));

    auto [it, _] = cachedWorldMeshes_.emplace(key, std::move(mesh));
    return &it->second;
}

void OpenGLRenderBackend::drawWorldTriangles(const WorldTriangle* triangles,
                                             std::size_t triangleCount,
                                             const float* viewProjectionMatrix4x4,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (!triangles || triangleCount == 0 || !viewProjectionMatrix4x4) return;
    constexpr std::size_t kMaxWorldTriangles = 180000;
    const std::size_t safeCount = std::min(triangleCount, kMaxWorldTriangles);
    if (safeCount == 0) return;

    std::vector<WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(safeCount * 3u);
    indices.reserve(safeCount * 3u);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const WorldTriangle& tri = triangles[i];
        const WorldMeshVertex v0{
            tri.x1, tri.y1, tri.z1,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r1, tri.r),
            resolveVertexChannel(tri.g1, tri.g),
            resolveVertexChannel(tri.b1, tri.b),
            resolveVertexChannel(tri.a1, tri.a)};
        const WorldMeshVertex v1{
            tri.x2, tri.y2, tri.z2,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r2, tri.r),
            resolveVertexChannel(tri.g2, tri.g),
            resolveVertexChannel(tri.b2, tri.b),
            resolveVertexChannel(tri.a2, tri.a)};
        const WorldMeshVertex v2{
            tri.x3, tri.y3, tri.z3,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r3, tri.r),
            resolveVertexChannel(tri.g3, tri.g),
            resolveVertexChannel(tri.b3, tri.b),
            resolveVertexChannel(tri.a3, tri.a)};
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        indices.push_back(base + 0u);
        indices.push_back(base + 1u);
        indices.push_back(base + 2u);
    }
    if (vertices.empty() || indices.empty()) return;

    drawWorldIndexedMeshTextured(vertices.data(),
                                 vertices.size(),
                                 indices.data(),
                                 indices.size(),
                                 nullptr,
                                 viewProjectionMatrix4x4,
                                 surfaceWidth,
                                 surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                                               std::size_t vertexCount,
                                               const std::uint32_t* indices,
                                               std::size_t indexCount,
                                               const float* viewProjectionMatrix4x4,
                                               int surfaceWidth,
                                               int surfaceHeight) {
    drawWorldIndexedMeshTextured(vertices,
                                 vertexCount,
                                 indices,
                                 indexCount,
                                 nullptr,
                                 viewProjectionMatrix4x4,
                                 surfaceWidth,
                                 surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshCached(const char* geometryKey,
                                                     const WorldMeshVertex* vertices,
                                                     std::size_t vertexCount,
                                                     const std::uint32_t* indices,
                                                     std::size_t indexCount,
                                                     const float* viewProjectionMatrix4x4,
                                                     int surfaceWidth,
                                                     int surfaceHeight) {
    ensureWorldPipeline();
    if (CachedWorldMesh* cached =
            ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount)) {
        drawWorldIndexedMeshTexturedInternal(cached->vao,
                                             cached->vertexBuffer,
                                             cached->indexBuffer,
                                             vertices,
                                             vertexCount,
                                             indices,
                                             indexCount,
                                             false,
                                             nullptr,
                                             nullptr,
                                             0u,
                                             viewProjectionMatrix4x4,
                                             surfaceWidth,
                                             surfaceHeight);
        return;
    }

    drawWorldIndexedMesh(vertices,
                         vertexCount,
                         indices,
                         indexCount,
                         viewProjectionMatrix4x4,
                         surfaceWidth,
                         surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshTexturedCached(const char* geometryKey,
                                                             const WorldMeshVertex* vertices,
                                                             std::size_t vertexCount,
                                                             const std::uint32_t* indices,
                                                             std::size_t indexCount,
                                                             const WorldTextureData* texture,
                                                             const float* viewProjectionMatrix4x4,
                                                             int surfaceWidth,
                                                             int surfaceHeight) {
    ensureWorldPipeline();
    if (CachedWorldMesh* cached =
            ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount)) {
        drawWorldIndexedMeshTexturedInternal(cached->vao,
                                             cached->vertexBuffer,
                                             cached->indexBuffer,
                                             vertices,
                                             vertexCount,
                                             indices,
                                             indexCount,
                                             false,
                                             texture,
                                             nullptr,
                                             0u,
                                             viewProjectionMatrix4x4,
                                             surfaceWidth,
                                             surfaceHeight);
        return;
    }

    drawWorldIndexedMeshTextured(vertices,
                                 vertexCount,
                                 indices,
                                 indexCount,
                                 texture,
                                 viewProjectionMatrix4x4,
                                 surfaceWidth,
                                 surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const WorldTextureData* texture,
    const WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    ensureWorldPipeline();
    if (CachedWorldMesh* cached =
            ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount)) {
        drawWorldIndexedMeshTexturedInternal(cached->vao,
                                             cached->vertexBuffer,
                                             cached->indexBuffer,
                                             vertices,
                                             vertexCount,
                                             indices,
                                             indexCount,
                                             false,
                                             texture,
                                             instances,
                                             instanceCount,
                                             viewProjectionMatrix4x4,
                                             surfaceWidth,
                                             surfaceHeight);
        return;
    }

    IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(geometryKey,
                                                                vertices,
                                                                vertexCount,
                                                                indices,
                                                                indexCount,
                                                                texture,
                                                                instances,
                                                                instanceCount,
                                                                viewProjectionMatrix4x4,
                                                                surfaceWidth,
                                                                surfaceHeight);
}

void OpenGLRenderBackend::prewarmWorldIndexedMeshCached(const char* geometryKey,
                                                        const WorldMeshVertex* vertices,
                                                        std::size_t vertexCount,
                                                        const std::uint32_t* indices,
                                                        std::size_t indexCount) {
    ensureWorldPipeline();
    (void)ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
}

void OpenGLRenderBackend::prewarmWorldTextureData(const WorldTextureData* texture) {
    if (!texture) return;

    (void)ensureWorldTexture(texture);
    if (texture->normalRgba && texture->normalWidth > 0 && texture->normalHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->normalKey,
            texture->normalRgba,
            texture->normalWidth,
            texture->normalHeight,
            texture->normalWrapS,
            texture->normalWrapT,
            /*srgb=*/false);
    }
    if (texture->metallicRoughnessRgba &&
        texture->metallicRoughnessWidth > 0 &&
        texture->metallicRoughnessHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->metallicRoughnessKey,
            texture->metallicRoughnessRgba,
            texture->metallicRoughnessWidth,
            texture->metallicRoughnessHeight,
            texture->metallicRoughnessWrapS,
            texture->metallicRoughnessWrapT,
            /*srgb=*/false);
    }
    if (texture->occlusionRgba && texture->occlusionWidth > 0 && texture->occlusionHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->occlusionKey,
            texture->occlusionRgba,
            texture->occlusionWidth,
            texture->occlusionHeight,
            texture->occlusionWrapS,
            texture->occlusionWrapT,
            /*srgb=*/false);
    }
    if (texture->emissiveRgba && texture->emissiveWidth > 0 && texture->emissiveHeight > 0) {
        (void)ensureWorldTextureRaw(
            texture->emissiveKey,
            texture->emissiveRgba,
            texture->emissiveWidth,
            texture->emissiveHeight,
            texture->emissiveWrapS,
            texture->emissiveWrapT,
            /*srgb=*/true);
    }
}

void OpenGLRenderBackend::prewarmWorldRenderAssets() {
    const auto t0 = std::chrono::high_resolution_clock::now();
    ensureWorldPipeline();
    const auto t1 = std::chrono::high_resolution_clock::now();

    static const unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};
    static const unsigned char kFallbackFlatNormalRgba[4] = {128u, 128u, 255u, 255u};
    (void)ensureWorldTextureRaw(
        "__world_fallback_white_srgb_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/true);
    (void)ensureWorldTextureRaw(
        "__world_fallback_white_linear_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/false);
    (void)ensureWorldTextureRaw(
        "__world_fallback_flat_normal_1x1__",
        kFallbackFlatNormalRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/false);
    const auto t2 = std::chrono::high_resolution_clock::now();

    const auto& neutralPmremAtlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    const auto t3 = std::chrono::high_resolution_clock::now();
    if (!neutralPmremAtlas.rgba16f.empty()) {
        (void)ensureWorldTextureRawHalfFloat(
            "__neutral_room_pmrem_rgba16f_v1__",
            neutralPmremAtlas.rgba16f.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            33071,
            33071);
    } else if (!neutralPmremAtlas.rgba.empty()) {
        (void)ensureWorldTextureRaw(
            "__neutral_room_pmrem_rgbm_v1__",
            neutralPmremAtlas.rgba.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            33071,
            33071,
            /*srgb=*/false);
    }
    const auto t4 = std::chrono::high_resolution_clock::now();

    const auto ms = [](const auto& a, const auto& b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::cout
        << "[Renderer][OpenGL][Prewarm] worldPipeline=" << ms(t0, t1) << "ms"
        << " fallbackTex=" << ms(t1, t2) << "ms"
        << " pmremAtlas=" << ms(t2, t3) << "ms"
        << " pmremUpload=" << ms(t3, t4) << "ms\n";
}

void OpenGLRenderBackend::drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                                       std::size_t vertexCount,
                                                       const std::uint32_t* indices,
                                                       std::size_t indexCount,
                                                       const WorldTextureData* texture,
                                                       const float* viewProjectionMatrix4x4,
                                                       int surfaceWidth,
                                                       int surfaceHeight) {
    drawWorldIndexedMeshTexturedInternal(worldVao_,
                                         worldVbo_,
                                         worldIbo_,
                                         vertices,
                                         vertexCount,
                                         indices,
                                         indexCount,
                                         true,
                                         texture,
                                         nullptr,
                                         0u,
                                         viewProjectionMatrix4x4,
                                         surfaceWidth,
                                         surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshTexturedInternal(unsigned int vao,
                                                               unsigned int vertexBuffer,
                                                               unsigned int indexBuffer,
                                                               const WorldMeshVertex* vertices,
                                                               std::size_t vertexCount,
                                                               const std::uint32_t* indices,
                                                               std::size_t indexCount,
                                                               bool uploadGeometry,
                                                               const WorldTextureData* texture,
                                                               const WorldMeshInstance* instances,
                                                               std::size_t instanceCount,
                                                               const float* viewProjectionMatrix4x4,
                                                               int surfaceWidth,
                                                               int surfaceHeight) {
    if (!vertices || !indices || vertexCount == 0 || indexCount < 3 || !viewProjectionMatrix4x4) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    ensureWorldPipeline();
    if (worldProgram_ == 0 || vao == 0u || vertexBuffer == 0u || indexBuffer == 0u || worldInstanceVbo_ == 0u ||
        worldViewProjLoc_ < 0 || worldModelLoc_ < 0 ||
        worldUseTextureLoc_ < 0 || worldTextureSamplerLoc_ < 0 ||
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldVertexColorMulLoc_ < 0 ||
        worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0 ||
        worldCameraPosLoc_ < 0 || worldCameraForwardLoc_ < 0 ||
        worldMaterialModeLoc_ < 0 || worldMaterialTimeLoc_ < 0 || worldMaterialFlagsLoc_ < 0 ||
        worldMaterialAtlasSizeLoc_ < 0 || worldMaterialRect0Loc_ < 0 || worldMaterialRect1Loc_ < 0 ||
        worldMaterialFlipbook0Loc_ < 0 || worldMaterialFlipbook1Loc_ < 0 ||
        worldSkinningEnabledLoc_ < 0 || worldSkinMatrixCountLoc_ < 0 || worldSkinMatricesLoc_ < 0) {
        return;
    }

    constexpr std::size_t kMaxWorldVertices = 540000;
    constexpr std::size_t kMaxWorldIndices = 900000;
    const std::size_t safeVertexCount = std::min(vertexCount, kMaxWorldVertices);
    const std::size_t safeIndexCount = std::min(indexCount, kMaxWorldIndices);
    if (safeVertexCount == 0 || safeIndexCount < 3) return;

    for (std::size_t i = 0; i < safeIndexCount; ++i) {
        if (indices[i] >= safeVertexCount) return;
    }

    const std::uint8_t alphaMode = texture ? std::min<std::uint8_t>(2u, texture->alphaMode) : 0u;
    const std::uint8_t blendMode = texture ? std::min<std::uint8_t>(2u, texture->blendMode) : 0u;
    const float alphaCutoff = texture ? std::clamp(texture->alphaCutoff, 0.0f, 1.0f) : 0.5f;
    const std::uint8_t materialMode = texture ? texture->materialMode : 0u;
    const float vertexColorMulR = texture ? texture->vertexColorMulR : 1.0f;
    const float vertexColorMulG = texture ? texture->vertexColorMulG : 1.0f;
    const float vertexColorMulB = texture ? texture->vertexColorMulB : 1.0f;
    const float vertexColorMulA = texture ? texture->vertexColorMulA : 1.0f;
    const GLuint worldTexture = ensureWorldTexture(texture);
    const bool hasTexture = (worldTexture != 0u);
    static const unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};
    static const unsigned char kFallbackFlatNormalRgba[4] = {128u, 128u, 255u, 255u};
    const GLuint fallbackWhiteSrgbTexture = ensureWorldTextureRaw(
        "__world_fallback_white_srgb_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/true);
    const GLuint fallbackWhiteLinearTexture = ensureWorldTextureRaw(
        "__world_fallback_white_linear_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/false);
    const GLuint fallbackFlatNormalTexture = ensureWorldTextureRaw(
        "__world_fallback_flat_normal_1x1__",
        kFallbackFlatNormalRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/false);
    const GLuint boundTexture = hasTexture ? worldTexture : fallbackWhiteSrgbTexture;
    const float useTexture = hasTexture ? 1.0f : 0.0f;
    const GLuint normalTexture = texture
        ? ensureWorldTextureRaw(
            texture->normalKey,
            texture->normalCacheKey,
            texture->normalRgba,
            texture->normalWidth,
            texture->normalHeight,
            texture->normalWrapS,
            texture->normalWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasNormalTexture = (normalTexture != 0u);
    const GLuint boundNormalTexture = hasNormalTexture ? normalTexture : fallbackFlatNormalTexture;
    const GLuint metallicRoughnessTexture = texture
        ? ensureWorldTextureRaw(
            texture->metallicRoughnessKey,
            texture->metallicRoughnessCacheKey,
            texture->metallicRoughnessRgba,
            texture->metallicRoughnessWidth,
            texture->metallicRoughnessHeight,
            texture->metallicRoughnessWrapS,
            texture->metallicRoughnessWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasMetallicRoughnessTexture = (metallicRoughnessTexture != 0u);
    const GLuint boundMetallicRoughnessTexture =
        hasMetallicRoughnessTexture ? metallicRoughnessTexture : fallbackWhiteLinearTexture;
    const GLuint occlusionTexture = texture
        ? ensureWorldTextureRaw(
            texture->occlusionKey,
            texture->occlusionCacheKey,
            texture->occlusionRgba,
            texture->occlusionWidth,
            texture->occlusionHeight,
            texture->occlusionWrapS,
            texture->occlusionWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasOcclusionTexture = (occlusionTexture != 0u);
    const GLuint boundOcclusionTexture = hasOcclusionTexture ? occlusionTexture : fallbackWhiteLinearTexture;
    const GLuint emissiveTexture = texture
        ? ensureWorldTextureRaw(
            texture->emissiveKey,
            texture->emissiveCacheKey,
            texture->emissiveRgba,
            texture->emissiveWidth,
            texture->emissiveHeight,
            texture->emissiveWrapS,
            texture->emissiveWrapT,
            /*srgb=*/true)
        : 0u;
    const bool hasEmissiveTexture = (emissiveTexture != 0u);
    const GLuint boundEmissiveTexture = hasEmissiveTexture ? emissiveTexture : fallbackWhiteSrgbTexture;
    const auto& neutralPmremAtlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    const GLuint neutralPmremTexture = !neutralPmremAtlas.rgba16f.empty()
        ? ensureWorldTextureRawHalfFloat(
            "__neutral_room_pmrem_rgba16f_v1__",
            neutralPmremAtlas.rgba16f.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            33071,
            33071)
        : ensureWorldTextureRaw(
            "__neutral_room_pmrem_rgbm_v1__",
            neutralPmremAtlas.rgba.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            33071,
            33071,
            /*srgb=*/false);
    const GLuint boundEnvTexture = (neutralPmremTexture != 0u) ? neutralPmremTexture : fallbackWhiteLinearTexture;
    const GLfloat wrapS = static_cast<GLfloat>(texture ? texture->wrapS : 10497);
    const GLfloat wrapT = static_cast<GLfloat>(texture ? texture->wrapT : 10497);
    const bool blendAlpha = (alphaMode == 2u);

    maybeLogPbrBindingOpenGL(
        texture,
        hasTexture,
        hasNormalTexture,
        hasMetallicRoughnessTexture,
        hasOcclusionTexture,
        hasEmissiveTexture);

    const bool preserveState = !worldIndexedBatchSubmissionState_.active;
    auto* batchState = preserveState ? nullptr : &worldIndexedBatchSubmissionState_;
    auto* dynamicUniformState = batchState ? &batchState->worldProgramDynamicUniforms : nullptr;
    const auto bindProgram = [&](GLuint program) {
        if (!batchState || batchState->currentProgram != static_cast<int>(program)) {
            glUseProgram(program);
            if (batchState) {
                batchState->currentProgram = static_cast<int>(program);
                batchState->worldProgramStaticUniformsApplied = false;
                batchState->worldProgramDynamicUniforms.valid = false;
            }
        }
    };
    const auto bindVertexArray = [&](GLuint vaoId) {
        if (!batchState || batchState->currentVao != static_cast<int>(vaoId)) {
            glBindVertexArray(vaoId);
            if (batchState) {
                batchState->currentVao = static_cast<int>(vaoId);
                batchState->currentElementArrayBuffer = -1;
            }
        }
    };
    const auto bindArrayBuffer = [&](GLuint bufferId) {
        if (!batchState || batchState->currentArrayBuffer != static_cast<int>(bufferId)) {
            glBindBuffer(GL_ARRAY_BUFFER, bufferId);
            if (batchState) {
                batchState->currentArrayBuffer = static_cast<int>(bufferId);
            }
        }
    };
    const auto bindElementArrayBuffer = [&](GLuint bufferId) {
        if (!batchState || batchState->currentElementArrayBuffer != static_cast<int>(bufferId)) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferId);
            if (batchState) {
                batchState->currentElementArrayBuffer = static_cast<int>(bufferId);
            }
        }
    };
    const auto setActiveTextureUnit = [&](int unit) {
        const GLint glUnit = static_cast<GLint>(GL_TEXTURE0 + unit);
        if (!batchState || batchState->currentActiveTexture != glUnit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            if (batchState) {
                batchState->currentActiveTexture = glUnit;
            }
        }
    };
    const auto bindTextureUnit2D = [&](int unit, GLuint textureId) {
        setActiveTextureUnit(unit);
        if (!batchState ||
            batchState->currentTexture2DOnUnit[static_cast<std::size_t>(unit)] != static_cast<int>(textureId)) {
            glBindTexture(GL_TEXTURE_2D, textureId);
            if (batchState) {
                batchState->currentTexture2DOnUnit[static_cast<std::size_t>(unit)] =
                    static_cast<int>(textureId);
            }
        }
    };
    const auto setDepthEnabled = [&](bool enabled) {
        if (!batchState || batchState->currentDepthEnabled != enabled) {
            if (enabled) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            if (batchState) {
                batchState->currentDepthEnabled = enabled;
            }
        }
    };
    const auto setBlendEnabled = [&](bool enabled) {
        if (!batchState || batchState->currentBlendEnabled != enabled) {
            if (enabled) {
                glEnable(GL_BLEND);
            } else {
                glDisable(GL_BLEND);
            }
            if (batchState) {
                batchState->currentBlendEnabled = enabled;
            }
        }
    };
    const auto setCullEnabled = [&](bool enabled) {
        if (!batchState || batchState->currentCullEnabled != enabled) {
            if (enabled) {
                glEnable(GL_CULL_FACE);
            } else {
                glDisable(GL_CULL_FACE);
            }
            if (batchState) {
                batchState->currentCullEnabled = enabled;
            }
        }
    };
    const auto setFrontFace = [&](GLenum frontFace) {
        if (!batchState || batchState->currentFrontFace != static_cast<int>(frontFace)) {
            glFrontFace(frontFace);
            if (batchState) {
                batchState->currentFrontFace = static_cast<int>(frontFace);
            }
        }
    };
    const auto setDepthMask = [&](bool enabled) {
        if (!batchState || batchState->currentDepthMask != enabled) {
            glDepthMask(enabled ? GL_TRUE : GL_FALSE);
            if (batchState) {
                batchState->currentDepthMask = enabled;
            }
        }
    };
    const auto setDepthFunc = [&](GLenum func) {
        if (!batchState || batchState->currentDepthFunc != static_cast<int>(func)) {
            glDepthFunc(func);
            if (batchState) {
                batchState->currentDepthFunc = static_cast<int>(func);
            }
        }
    };
    const auto setBlendEquationSeparate = [&](GLenum rgb, GLenum alpha) {
        if (!batchState ||
            batchState->currentBlendEqRgb != static_cast<int>(rgb) ||
            batchState->currentBlendEqAlpha != static_cast<int>(alpha)) {
            glBlendEquationSeparate(rgb, alpha);
            if (batchState) {
                batchState->currentBlendEqRgb = static_cast<int>(rgb);
                batchState->currentBlendEqAlpha = static_cast<int>(alpha);
            }
        }
    };
    const auto setBlendFuncSeparate = [&](GLenum srcRgb,
                                          GLenum dstRgb,
                                          GLenum srcAlpha,
                                          GLenum dstAlpha) {
        if (!batchState ||
            batchState->currentBlendSrcRgb != static_cast<int>(srcRgb) ||
            batchState->currentBlendDstRgb != static_cast<int>(dstRgb) ||
            batchState->currentBlendSrcAlpha != static_cast<int>(srcAlpha) ||
            batchState->currentBlendDstAlpha != static_cast<int>(dstAlpha)) {
            glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
            if (batchState) {
                batchState->currentBlendSrcRgb = static_cast<int>(srcRgb);
                batchState->currentBlendDstRgb = static_cast<int>(dstRgb);
                batchState->currentBlendSrcAlpha = static_cast<int>(srcAlpha);
                batchState->currentBlendDstAlpha = static_cast<int>(dstAlpha);
            }
        }
    };
    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevElementArrayBuffer = 0;
    GLint prevActiveTexture = 0;
    GLint prevTexture2DOnActive = 0;
    GLint prevTexture2DOnUnit[6] = {0, 0, 0, 0, 0, 0};
    GLboolean depthEnabled = GL_FALSE;
    GLboolean blendEnabled = GL_FALSE;
    GLboolean cullEnabled = GL_FALSE;
    GLint prevFrontFace = GL_CCW;
    GLboolean previousDepthMask = GL_TRUE;
    GLint previousDepthFunc = GL_LESS;
    GLint prevBlendSrcRgb = GL_SRC_ALPHA;
    GLint prevBlendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevBlendSrcAlpha = GL_ONE;
    GLint prevBlendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevBlendEqRgb = GL_FUNC_ADD;
    GLint prevBlendEqAlpha = GL_FUNC_ADD;
    if (preserveState) {
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementArrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnActive);
        for (int unit = 0; unit < 6; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnUnit[unit]);
        }
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));

        depthEnabled = glIsEnabled(GL_DEPTH_TEST);
        blendEnabled = glIsEnabled(GL_BLEND);
        cullEnabled = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_FRONT_FACE, &prevFrontFace);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
        glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
        glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEqRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEqAlpha);
    }

    glViewport(0, 0, std::max(1, surfaceWidth), std::max(1, surfaceHeight));
    setDepthEnabled(true);
    setFrontFace(engine::render::parity_contract::kWorldFrontFaceClockwise ? GL_CW : GL_CCW);
    setDepthFunc(engine::render::parity_contract::kWorldDepthFuncLessEqual ? GL_LEQUAL : GL_LESS);
    setCullEnabled(engine::render::parity_contract::kWorldCullEnabled);
    if (blendAlpha) {
        setBlendEnabled(true);
        setBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        switch (blendMode) {
        case 1u:
            setBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
            break;
        case 2u:
            setBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case 0u:
        default:
            setBlendFuncSeparate(
                GL_SRC_ALPHA,
                GL_ONE_MINUS_SRC_ALPHA,
                GL_SRC_ALPHA,
                GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
    } else {
        setBlendEnabled(false);
    }
    setDepthMask(!blendAlpha);

    bindProgram(worldProgram_);
    static constexpr float kIdentityModel[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    const float* modelMatrix = texture ? texture->modelMatrix.data() : kIdentityModel;
    const float cameraPosX = texture ? texture->cameraPosX : 0.0f;
    const float cameraPosY = texture ? texture->cameraPosY : 7.0f;
    const float cameraPosZ = texture ? texture->cameraPosZ : 9.0f;
    const float cameraForwardX = texture ? texture->cameraForwardX : 0.0f;
    const float cameraForwardY = texture ? texture->cameraForwardY : -0.6139406f;
    const float cameraForwardZ = texture ? texture->cameraForwardZ : -0.7893522f;
    const float cameraTargetX = texture ? texture->cameraTargetX : 0.0f;
    const float cameraTargetY = texture ? texture->cameraTargetY : -1.0f;
    const float cameraTargetZ = texture ? texture->cameraTargetZ : 0.0f;
    const float normalScale = texture ? std::max(0.0f, texture->normalScale) : 1.0f;
    const float metallicFactor = texture ? std::clamp(texture->metallicFactor, 0.0f, 1.0f) : 1.0f;
    const float roughnessFactor = texture ? std::clamp(texture->roughnessFactor, 0.0f, 1.0f) : 1.0f;
    const float occlusionStrength = texture ? std::clamp(texture->occlusionStrength, 0.0f, 1.0f) : 1.0f;
    const float emissiveFactorR = texture ? std::max(0.0f, texture->emissiveFactorR) : 0.0f;
    const float emissiveFactorG = texture ? std::max(0.0f, texture->emissiveFactorG) : 0.0f;
    const float emissiveFactorB = texture ? std::max(0.0f, texture->emissiveFactorB) : 0.0f;
    const float characterInkingEnabled =
        (texture && texture->characterInkingEnabled != 0u) ? 1.0f : 0.0f;
    const float materialTime = texture ? texture->materialTimeSec : 0.0f;
    const float materialFlags = texture ? texture->materialFlags : 0.0f;
    const float materialAtlasWidth = texture ? texture->materialAtlasWidth : 0.0f;
    const float materialAtlasHeight = texture ? texture->materialAtlasHeight : 0.0f;
    const float materialRect0U = texture ? texture->materialRect0U : 0.0f;
    const float materialRect0V = texture ? texture->materialRect0V : 0.0f;
    const float materialRect0W = texture ? texture->materialRect0W : 1.0f;
    const float materialRect0H = texture ? texture->materialRect0H : 1.0f;
    const float materialRect1U = texture ? texture->materialRect1U : 0.0f;
    const float materialRect1V = texture ? texture->materialRect1V : 0.0f;
    const float materialRect1W = texture ? texture->materialRect1W : 1.0f;
    const float materialRect1H = texture ? texture->materialRect1H : 1.0f;
    const float materialFlipbook0Cols = texture ? texture->materialFlipbook0Cols : 1.0f;
    const float materialFlipbook0Rows = texture ? texture->materialFlipbook0Rows : 1.0f;
    const float materialFlipbook0Frames = texture ? texture->materialFlipbook0Frames : 1.0f;
    const float materialFlipbook0Fps = texture ? texture->materialFlipbook0Fps : 0.0f;
    const float materialFlipbook1Cols = texture ? texture->materialFlipbook1Cols : 1.0f;
    const float materialFlipbook1Rows = texture ? texture->materialFlipbook1Rows : 1.0f;
    const float materialFlipbook1Frames = texture ? texture->materialFlipbook1Frames : 1.0f;
    const float materialFlipbook1Fps =
        (texture && materialMode >= 2u)
            ? static_cast<float>(pbrDebugViewMode())
            : (texture ? texture->materialFlipbook1Fps : 0.0f);
    constexpr int kMaxGpuSkinMatrices = 64;
    const bool gpuSkinningEnabled =
        texture &&
        texture->gpuSkinning != 0u &&
        texture->skinMatrices != nullptr &&
        texture->skinMatrixCount > 0u;
    const int gpuSkinMatrixCount = gpuSkinningEnabled
        ? std::min<int>(static_cast<int>(texture->skinMatrixCount), kMaxGpuSkinMatrices)
        : 0;
    const std::array<float, 16> viewProjection = [&]() {
        std::array<float, 16> value{};
        std::copy_n(viewProjectionMatrix4x4, value.size(), value.begin());
        return value;
    }();
    const std::array<float, 16> modelMatrixValues = [&]() {
        std::array<float, 16> value{};
        std::copy_n(modelMatrix, value.size(), value.begin());
        return value;
    }();
    const std::array<float, 3> cameraPos = {cameraPosX, cameraPosY, cameraPosZ};
    const std::array<float, 3> cameraForward = {cameraForwardX, cameraForwardY, cameraForwardZ};
    const std::array<float, 3> cameraTarget = {cameraTargetX, cameraTargetY, cameraTargetZ};
    const std::array<float, 4> vertexColorMul = {
        vertexColorMulR, vertexColorMulG, vertexColorMulB, vertexColorMulA};
    const std::array<float, 3> emissiveFactor = {
        emissiveFactorR, emissiveFactorG, emissiveFactorB};
    const std::array<float, 2> materialAtlasSize = {materialAtlasWidth, materialAtlasHeight};
    const std::array<float, 4> materialRect0 = {
        materialRect0U, materialRect0V, materialRect0W, materialRect0H};
    const std::array<float, 4> materialRect1 = {
        materialRect1U, materialRect1V, materialRect1W, materialRect1H};
    const std::array<float, 4> materialFlipbook0 = {
        materialFlipbook0Cols, materialFlipbook0Rows, materialFlipbook0Frames, materialFlipbook0Fps};
    const std::array<float, 4> materialFlipbook1 = {
        materialFlipbook1Cols, materialFlipbook1Rows, materialFlipbook1Frames, materialFlipbook1Fps};
    const float hasNormalTextureValue = hasNormalTexture ? 1.0f : 0.0f;
    const float hasMetallicRoughnessTextureValue = hasMetallicRoughnessTexture ? 1.0f : 0.0f;
    const float hasOcclusionTextureValue = hasOcclusionTexture ? 1.0f : 0.0f;
    const float hasEmissiveTextureValue = hasEmissiveTexture ? 1.0f : 0.0f;
    const float alphaModeValue = static_cast<float>(alphaMode);
    const float materialModeValue = static_cast<float>(materialMode);
    const float gpuSkinningEnabledValue = gpuSkinningEnabled ? 1.0f : 0.0f;

    const auto setUniform1fCached = [&](int location, float value, float* cachedValue) {
        if (!dynamicUniformState || !dynamicUniformState->valid || !cachedValue || *cachedValue != value) {
            glUniform1f(location, value);
            if (cachedValue) {
                *cachedValue = value;
            }
        }
    };
    const auto setUniform1iCached = [&](int location, int value, int* cachedValue) {
        if (!dynamicUniformState || !dynamicUniformState->valid || !cachedValue || *cachedValue != value) {
            glUniform1i(location, value);
            if (cachedValue) {
                *cachedValue = value;
            }
        }
    };
    const auto setUniform2fCached = [&](int location,
                                        const std::array<float, 2>& value,
                                        std::array<float, 2>* cachedValue) {
        if (!dynamicUniformState || !dynamicUniformState->valid || !cachedValue || *cachedValue != value) {
            glUniform2f(location, value[0], value[1]);
            if (cachedValue) {
                *cachedValue = value;
            }
        }
    };
    const auto setUniform3fCached = [&](int location,
                                        const std::array<float, 3>& value,
                                        std::array<float, 3>* cachedValue) {
        if (!dynamicUniformState || !dynamicUniformState->valid || !cachedValue || *cachedValue != value) {
            glUniform3f(location, value[0], value[1], value[2]);
            if (cachedValue) {
                *cachedValue = value;
            }
        }
    };
    const auto setUniform4fCached = [&](int location,
                                        const std::array<float, 4>& value,
                                        std::array<float, 4>* cachedValue) {
        if (!dynamicUniformState || !dynamicUniformState->valid || !cachedValue || *cachedValue != value) {
            glUniform4f(location, value[0], value[1], value[2], value[3]);
            if (cachedValue) {
                *cachedValue = value;
            }
        }
    };
    const auto setUniformMatrix4Cached = [&](int location,
                                             const std::array<float, 16>& value,
                                             std::array<float, 16>* cachedValue) {
        if (!dynamicUniformState || !dynamicUniformState->valid || !cachedValue || *cachedValue != value) {
            glUniformMatrix4fv(location, 1, GL_FALSE, value.data());
            if (cachedValue) {
                *cachedValue = value;
            }
        }
    };

    setUniformMatrix4Cached(
        worldViewProjLoc_,
        viewProjection,
        dynamicUniformState ? &dynamicUniformState->viewProjection : nullptr);
    setUniformMatrix4Cached(
        worldModelLoc_,
        modelMatrixValues,
        dynamicUniformState ? &dynamicUniformState->model : nullptr);
    setUniform3fCached(
        worldCameraPosLoc_,
        cameraPos,
        dynamicUniformState ? &dynamicUniformState->cameraPos : nullptr);
    setUniform3fCached(
        worldCameraForwardLoc_,
        cameraForward,
        dynamicUniformState ? &dynamicUniformState->cameraForward : nullptr);
    if (worldCameraTargetLoc_ >= 0) {
        setUniform3fCached(
            worldCameraTargetLoc_,
            cameraTarget,
            dynamicUniformState ? &dynamicUniformState->cameraTarget : nullptr);
    }
    setUniform1fCached(
        worldUseTextureLoc_,
        useTexture,
        dynamicUniformState ? &dynamicUniformState->useTexture : nullptr);
    setUniform4fCached(
        worldVertexColorMulLoc_,
        vertexColorMul,
        dynamicUniformState ? &dynamicUniformState->vertexColorMul : nullptr);
    if (worldUseNormalTextureLoc_ >= 0) {
        setUniform1fCached(
            worldUseNormalTextureLoc_,
            hasNormalTextureValue,
            dynamicUniformState ? &dynamicUniformState->useNormalTexture : nullptr);
    }
    if (worldUseMetallicRoughnessTextureLoc_ >= 0) {
        setUniform1fCached(
            worldUseMetallicRoughnessTextureLoc_,
            hasMetallicRoughnessTextureValue,
            dynamicUniformState ? &dynamicUniformState->useMetallicRoughnessTexture : nullptr);
    }
    if (worldUseOcclusionTextureLoc_ >= 0) {
        setUniform1fCached(
            worldUseOcclusionTextureLoc_,
            hasOcclusionTextureValue,
            dynamicUniformState ? &dynamicUniformState->useOcclusionTexture : nullptr);
    }
    if (worldUseEmissiveTextureLoc_ >= 0) {
        setUniform1fCached(
            worldUseEmissiveTextureLoc_,
            hasEmissiveTextureValue,
            dynamicUniformState ? &dynamicUniformState->useEmissiveTexture : nullptr);
    }
    setUniform1fCached(
        worldWrapSLoc_,
        wrapS,
        dynamicUniformState ? &dynamicUniformState->wrapS : nullptr);
    setUniform1fCached(
        worldWrapTLoc_,
        wrapT,
        dynamicUniformState ? &dynamicUniformState->wrapT : nullptr);
    setUniform1fCached(
        worldAlphaModeLoc_,
        alphaModeValue,
        dynamicUniformState ? &dynamicUniformState->alphaMode : nullptr);
    setUniform1fCached(
        worldAlphaCutoffLoc_,
        alphaCutoff,
        dynamicUniformState ? &dynamicUniformState->alphaCutoff : nullptr);
    if (worldNormalScaleLoc_ >= 0) {
        setUniform1fCached(
            worldNormalScaleLoc_,
            normalScale,
            dynamicUniformState ? &dynamicUniformState->normalScale : nullptr);
    }
    if (worldMetallicFactorLoc_ >= 0) {
        setUniform1fCached(
            worldMetallicFactorLoc_,
            metallicFactor,
            dynamicUniformState ? &dynamicUniformState->metallicFactor : nullptr);
    }
    if (worldRoughnessFactorLoc_ >= 0) {
        setUniform1fCached(
            worldRoughnessFactorLoc_,
            roughnessFactor,
            dynamicUniformState ? &dynamicUniformState->roughnessFactor : nullptr);
    }
    if (worldOcclusionStrengthLoc_ >= 0) {
        setUniform1fCached(
            worldOcclusionStrengthLoc_,
            occlusionStrength,
            dynamicUniformState ? &dynamicUniformState->occlusionStrength : nullptr);
    }
    if (worldEmissiveFactorLoc_ >= 0) {
        setUniform3fCached(
            worldEmissiveFactorLoc_,
            emissiveFactor,
            dynamicUniformState ? &dynamicUniformState->emissiveFactor : nullptr);
    }
    if (worldCharacterInkingEnabledLoc_ >= 0) {
        setUniform1fCached(
            worldCharacterInkingEnabledLoc_,
            characterInkingEnabled,
            dynamicUniformState ? &dynamicUniformState->characterInkingEnabled : nullptr);
    }
    setUniform1fCached(
        worldMaterialModeLoc_,
        materialModeValue,
        dynamicUniformState ? &dynamicUniformState->materialMode : nullptr);
    setUniform1fCached(
        worldMaterialTimeLoc_,
        materialTime,
        dynamicUniformState ? &dynamicUniformState->materialTime : nullptr);
    setUniform1fCached(
        worldMaterialFlagsLoc_,
        materialFlags,
        dynamicUniformState ? &dynamicUniformState->materialFlags : nullptr);
    setUniform2fCached(
        worldMaterialAtlasSizeLoc_,
        materialAtlasSize,
        dynamicUniformState ? &dynamicUniformState->materialAtlasSize : nullptr);
    setUniform4fCached(
        worldMaterialRect0Loc_,
        materialRect0,
        dynamicUniformState ? &dynamicUniformState->materialRect0 : nullptr);
    setUniform4fCached(
        worldMaterialRect1Loc_,
        materialRect1,
        dynamicUniformState ? &dynamicUniformState->materialRect1 : nullptr);
    setUniform4fCached(
        worldMaterialFlipbook0Loc_,
        materialFlipbook0,
        dynamicUniformState ? &dynamicUniformState->materialFlipbook0 : nullptr);
    setUniform4fCached(
        worldMaterialFlipbook1Loc_,
        materialFlipbook1,
        dynamicUniformState ? &dynamicUniformState->materialFlipbook1 : nullptr);
    setUniform1fCached(
        worldSkinningEnabledLoc_,
        gpuSkinningEnabledValue,
        dynamicUniformState ? &dynamicUniformState->skinningEnabled : nullptr);
    setUniform1iCached(
        worldSkinMatrixCountLoc_,
        gpuSkinMatrixCount,
        dynamicUniformState ? &dynamicUniformState->skinMatrixCount : nullptr);
    if (gpuSkinMatrixCount > 0) {
        const std::size_t gpuSkinMatrixFloatCount =
            static_cast<std::size_t>(gpuSkinMatrixCount) * 16u;
        const bool skinMatricesDirty =
            !dynamicUniformState ||
            !dynamicUniformState->valid ||
            !std::equal(dynamicUniformState->skinMatrices.begin(),
                        dynamicUniformState->skinMatrices.begin() + gpuSkinMatrixFloatCount,
                        texture->skinMatrices);
        if (skinMatricesDirty) {
            glUniformMatrix4fv(worldSkinMatricesLoc_, gpuSkinMatrixCount, GL_FALSE, texture->skinMatrices);
            if (dynamicUniformState) {
                std::copy_n(texture->skinMatrices,
                            gpuSkinMatrixFloatCount,
                            dynamicUniformState->skinMatrices.begin());
            }
        }
    }
    if (dynamicUniformState) {
        dynamicUniformState->valid = true;
    }
    if (!batchState || !batchState->worldProgramStaticUniformsApplied) {
        glUniform1i(worldTextureSamplerLoc_, 0);
        if (worldNormalTextureSamplerLoc_ >= 0) glUniform1i(worldNormalTextureSamplerLoc_, 1);
        if (worldMetallicRoughnessTextureSamplerLoc_ >= 0) glUniform1i(worldMetallicRoughnessTextureSamplerLoc_, 2);
        if (worldOcclusionTextureSamplerLoc_ >= 0) glUniform1i(worldOcclusionTextureSamplerLoc_, 3);
        if (worldEmissiveTextureSamplerLoc_ >= 0) glUniform1i(worldEmissiveTextureSamplerLoc_, 4);
        if (worldEnvTextureSamplerLoc_ >= 0) glUniform1i(worldEnvTextureSamplerLoc_, 5);
        if (worldEnvTexelSizeLoc_ >= 0) {
            glUniform2f(
                worldEnvTexelSizeLoc_,
                neutralPmremAtlas.texelWidth,
                neutralPmremAtlas.texelHeight);
        }
        if (worldEnvMaxMipLoc_ >= 0) {
            glUniform1f(worldEnvMaxMipLoc_, neutralPmremAtlas.maxMip);
        }
        if (worldEnvRgbmRangeLoc_ >= 0) {
            glUniform1f(worldEnvRgbmRangeLoc_, neutralPmremAtlas.rgbmRange);
        }
        if (batchState) {
            batchState->worldProgramStaticUniformsApplied = true;
        }
    }

    bindTextureUnit2D(0, boundTexture);
    bindTextureUnit2D(1, boundNormalTexture);
    bindTextureUnit2D(2, boundMetallicRoughnessTexture);
    bindTextureUnit2D(3, boundOcclusionTexture);
    bindTextureUnit2D(4, boundEmissiveTexture);
    bindTextureUnit2D(5, boundEnvTexture);

    const std::size_t effectiveInstanceCount =
        (instances && instanceCount > 0u) ? instanceCount : 1u;
    if (effectiveInstanceCount > static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)())) {
        if (preserveState) {
            glBindVertexArray(static_cast<GLuint>(prevVao));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElementArrayBuffer));
            glUseProgram(static_cast<GLuint>(prevProgram));
            for (int unit = 0; unit < 6; ++unit) {
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnUnit[unit]));
            }
            glActiveTexture(static_cast<GLenum>(prevActiveTexture));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnActive));
            glDepthMask(previousDepthMask);
            glDepthFunc(static_cast<GLenum>(previousDepthFunc));
            glBlendEquationSeparate(static_cast<GLenum>(prevBlendEqRgb), static_cast<GLenum>(prevBlendEqAlpha));
            glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb),
                                static_cast<GLenum>(prevBlendDstRgb),
                                static_cast<GLenum>(prevBlendSrcAlpha),
                                static_cast<GLenum>(prevBlendDstAlpha));
            if (blendEnabled) {
                glEnable(GL_BLEND);
            } else {
                glDisable(GL_BLEND);
            }
            glFrontFace(static_cast<GLenum>(prevFrontFace));
            if (cullEnabled) {
                glEnable(GL_CULL_FACE);
            } else {
                glDisable(GL_CULL_FACE);
            }
            if (depthEnabled) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
        }
        return;
    }

    static thread_local std::vector<OpenGLWorldInstanceVertexData> instanceData;
    instanceData.resize(effectiveInstanceCount);
    if (instances && instanceCount > 0u) {
        for (std::size_t i = 0; i < effectiveInstanceCount; ++i) {
            packWorldInstanceVertexData(instances[i], instanceData[i]);
        }
    } else {
        instanceData[0] = makeIdentityInstanceData();
    }

    bindArrayBuffer(worldInstanceVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(instanceData.size() * sizeof(OpenGLWorldInstanceVertexData)),
                 instanceData.data(),
                 GL_STREAM_DRAW);

    bindVertexArray(vao);
    if (uploadGeometry) {
        bindArrayBuffer(vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(safeVertexCount * sizeof(WorldMeshVertex)),
                     vertices,
                     GL_STREAM_DRAW);
        bindElementArrayBuffer(indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(safeIndexCount * sizeof(std::uint32_t)),
                     indices,
                     GL_STREAM_DRAW);
    }
    glDrawElementsInstanced(GL_TRIANGLES,
                            static_cast<GLsizei>(safeIndexCount),
                            GL_UNSIGNED_INT,
                            nullptr,
                            static_cast<GLsizei>(effectiveInstanceCount));
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u) *
                       static_cast<std::uint64_t>(effectiveInstanceCount);

    const bool drawCharacterOutline =
        texture &&
        texture->characterInkingEnabled != 0u &&
        materialMode >= 2u &&
        safeVertexCount > 0u;
    if (drawCharacterOutline) {
        static thread_local std::vector<WorldMeshVertex> outlineVertices;
        outlineVertices.resize(safeVertexCount);
        std::copy(vertices, vertices + safeVertexCount, outlineVertices.begin());

        // Keep this subtle: thin silhouette ring instead of heavy toon border.
        const float kOutlineExtrude = 0.001f;
        for (WorldMeshVertex& v : outlineVertices) {
            const float lenSq = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
            if (lenSq > 1e-10f) {
                const float invLen = 1.0f / std::sqrt(lenSq);
                v.x += v.nx * invLen * kOutlineExtrude;
                v.y += v.ny * invLen * kOutlineExtrude;
                v.z += v.nz * invLen * kOutlineExtrude;
            }
            v.r = 0.0f;
            v.g = 0.0f;
            v.b = 0.0f;
            v.a = 1.0f;
        }

        glUniform1f(worldUseTextureLoc_, 0.0f);
        glUniform1f(worldMaterialModeLoc_, 3.0f);
        if (dynamicUniformState) {
            dynamicUniformState->valid = false;
        }
        setDepthMask(false);

        bindVertexArray(worldVao_);
        bindArrayBuffer(worldVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(safeVertexCount * sizeof(WorldMeshVertex)),
                     outlineVertices.data(),
                     GL_STREAM_DRAW);
        bindElementArrayBuffer(worldIbo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(safeIndexCount * sizeof(std::uint32_t)),
                     indices,
                     GL_STREAM_DRAW);
        glDrawElementsInstanced(GL_TRIANGLES,
                                static_cast<GLsizei>(safeIndexCount),
                                GL_UNSIGNED_INT,
                                nullptr,
                                static_cast<GLsizei>(effectiveInstanceCount));
        ++frameDrawCalls_;
        frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u) *
                           static_cast<std::uint64_t>(effectiveInstanceCount);
    }

    if (preserveState) {
        glBindVertexArray(static_cast<GLuint>(prevVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElementArrayBuffer));
        glUseProgram(static_cast<GLuint>(prevProgram));

        for (int unit = 0; unit < 6; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnUnit[unit]));
        }
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnActive));

        glDepthMask(previousDepthMask);
        glDepthFunc(static_cast<GLenum>(previousDepthFunc));
        glBlendEquationSeparate(static_cast<GLenum>(prevBlendEqRgb), static_cast<GLenum>(prevBlendEqAlpha));
        glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb),
                            static_cast<GLenum>(prevBlendDstRgb),
                            static_cast<GLenum>(prevBlendSrcAlpha),
                            static_cast<GLenum>(prevBlendDstAlpha));
        if (blendEnabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        glFrontFace(static_cast<GLenum>(prevFrontFace));
        if (cullEnabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
        if (depthEnabled) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
    }
}
