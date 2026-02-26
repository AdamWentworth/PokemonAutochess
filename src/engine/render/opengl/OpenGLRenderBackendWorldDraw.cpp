#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <glad/glad.h>

namespace {

float resolveVertexChannel(float channel, float fallback) {
    return (channel >= 0.0f) ? channel : fallback;
}

} // namespace

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
    (void)geometryKey;
    drawWorldIndexedMesh(
        vertices,
        vertexCount,
        indices,
        indexCount,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

void OpenGLRenderBackend::prewarmWorldIndexedMeshCached(const char* geometryKey,
                                                        const WorldMeshVertex* vertices,
                                                        std::size_t vertexCount,
                                                        const std::uint32_t* indices,
                                                        std::size_t indexCount) {
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
}

void OpenGLRenderBackend::drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                                       std::size_t vertexCount,
                                                       const std::uint32_t* indices,
                                                       std::size_t indexCount,
                                                       const WorldTextureData* texture,
                                                       const float* viewProjectionMatrix4x4,
                                                       int surfaceWidth,
                                                       int surfaceHeight) {
    if (!vertices || !indices || vertexCount == 0 || indexCount < 3 || !viewProjectionMatrix4x4) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    ensureWorldPipeline();
    if (worldProgram_ == 0 || worldVao_ == 0 || worldVbo_ == 0 || worldIbo_ == 0 ||
        worldViewProjLoc_ < 0 || worldUseTextureLoc_ < 0 || worldTextureSamplerLoc_ < 0 ||
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0 ||
        worldMaterialModeLoc_ < 0 || worldMaterialTimeLoc_ < 0 || worldMaterialFlagsLoc_ < 0 ||
        worldMaterialAtlasSizeLoc_ < 0 || worldMaterialRect0Loc_ < 0 || worldMaterialRect1Loc_ < 0 ||
        worldMaterialFlipbook0Loc_ < 0 || worldMaterialFlipbook1Loc_ < 0) {
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
    const GLuint worldTexture = ensureWorldTexture(texture);
    const bool hasTexture = (worldTexture != 0u);
    const GLuint boundTexture = hasTexture ? worldTexture : worldFallbackTexture_;
    const float useTexture = hasTexture ? 1.0f : 0.0f;
    const GLfloat wrapS = static_cast<GLfloat>(texture ? texture->wrapS : 10497);
    const GLfloat wrapT = static_cast<GLfloat>(texture ? texture->wrapT : 10497);
    const bool blendAlpha = (alphaMode == 2u);

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevElementArrayBuffer = 0;
    GLint prevActiveTexture = 0;
    GLint prevTexture2DOnActive = 0;
    GLint prevTexture2DOnUnit0 = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnUnit0);
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    GLint prevBlendSrcRgb = GL_SRC_ALPHA;
    GLint prevBlendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevBlendSrcAlpha = GL_ONE;
    GLint prevBlendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevBlendEqRgb = GL_FUNC_ADD;
    GLint prevBlendEqAlpha = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEqAlpha);

    glViewport(0, 0, std::max(1, surfaceWidth), std::max(1, surfaceHeight));
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    switch (blendMode) {
    case 1u:
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
        break;
    case 2u:
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case 0u:
    default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
    glDepthMask(blendAlpha ? GL_FALSE : GL_TRUE);

    glUseProgram(worldProgram_);
    glUniformMatrix4fv(worldViewProjLoc_, 1, GL_FALSE, viewProjectionMatrix4x4);
    glUniform1f(worldUseTextureLoc_, useTexture);
    glUniform1f(worldWrapSLoc_, wrapS);
    glUniform1f(worldWrapTLoc_, wrapT);
    glUniform1f(worldAlphaModeLoc_, static_cast<GLfloat>(alphaMode));
    glUniform1f(worldAlphaCutoffLoc_, alphaCutoff);
    glUniform1f(worldMaterialModeLoc_, static_cast<GLfloat>(materialMode));
    glUniform1f(worldMaterialTimeLoc_, texture ? texture->materialTimeSec : 0.0f);
    glUniform1f(worldMaterialFlagsLoc_, texture ? texture->materialFlags : 0.0f);
    glUniform2f(worldMaterialAtlasSizeLoc_,
                texture ? texture->materialAtlasWidth : 0.0f,
                texture ? texture->materialAtlasHeight : 0.0f);
    glUniform4f(worldMaterialRect0Loc_,
                texture ? texture->materialRect0U : 0.0f,
                texture ? texture->materialRect0V : 0.0f,
                texture ? texture->materialRect0W : 1.0f,
                texture ? texture->materialRect0H : 1.0f);
    glUniform4f(worldMaterialRect1Loc_,
                texture ? texture->materialRect1U : 0.0f,
                texture ? texture->materialRect1V : 0.0f,
                texture ? texture->materialRect1W : 1.0f,
                texture ? texture->materialRect1H : 1.0f);
    glUniform4f(worldMaterialFlipbook0Loc_,
                texture ? texture->materialFlipbook0Cols : 1.0f,
                texture ? texture->materialFlipbook0Rows : 1.0f,
                texture ? texture->materialFlipbook0Frames : 1.0f,
                texture ? texture->materialFlipbook0Fps : 0.0f);
    glUniform4f(worldMaterialFlipbook1Loc_,
                texture ? texture->materialFlipbook1Cols : 1.0f,
                texture ? texture->materialFlipbook1Rows : 1.0f,
                texture ? texture->materialFlipbook1Frames : 1.0f,
                texture ? texture->materialFlipbook1Fps : 0.0f);
    glUniform1i(worldTextureSamplerLoc_, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, boundTexture);

    glBindVertexArray(worldVao_);
    glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(safeVertexCount * sizeof(WorldMeshVertex)),
                 vertices,
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(safeIndexCount * sizeof(std::uint32_t)),
                 indices,
                 GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(safeIndexCount), GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElementArrayBuffer));
    glUseProgram(static_cast<GLuint>(prevProgram));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnUnit0));
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnActive));

    glDepthMask(previousDepthMask);
    glBlendEquationSeparate(static_cast<GLenum>(prevBlendEqRgb), static_cast<GLenum>(prevBlendEqAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb),
                        static_cast<GLenum>(prevBlendDstRgb),
                        static_cast<GLenum>(prevBlendSrcAlpha),
                        static_cast<GLenum>(prevBlendDstAlpha));
    if (!blendEnabled) glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE);
    if (!depthEnabled) glDisable(GL_DEPTH_TEST);
}
