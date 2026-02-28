#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <glad/glad.h>

#include "engine/render/DebugGeometry.h"

void OpenGLRenderBackend::drawDebugQuads(const DebugQuad* quads,
                                         std::size_t quadCount,
                                         int surfaceWidth,
                                         int surfaceHeight) {
    if (!quads || quadCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    ensureDebugPipeline();
    if (debugProgram_ == 0 || debugVao_ == 0 || debugVbo_ == 0 || debugSurfaceSizeLoc_ < 0) return;

    using GlDebugVertex = engine::render::debug::Vertex2D;

    constexpr std::size_t kMaxDebugQuads = 4096;
    const std::size_t safeCount = std::min(quadCount, kMaxDebugQuads);
    std::vector<GlDebugVertex> vertices;
    vertices.reserve(safeCount * 6);
    for (std::size_t i = 0; i < safeCount; ++i) {
        engine::render::debug::appendQuadAsTriangles(quads[i], vertices);
    }
    if (vertices.empty()) return;

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(debugProgram_);
    glUniform2f(debugSurfaceSizeLoc_, static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight));

    glBindVertexArray(debugVao_);
    glBindBuffer(GL_ARRAY_BUFFER, debugVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(GlDebugVertex)),
                 vertices.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(vertices.size() / 3u);

    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));

    if (!blendEnabled) glDisable(GL_BLEND);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
}

void OpenGLRenderBackend::drawDebugLines(const DebugLine* lines,
                                         std::size_t lineCount,
                                         int surfaceWidth,
                                         int surfaceHeight) {
    if (!lines || lineCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    ensureDebugPipeline();
    if (debugProgram_ == 0 || debugVao_ == 0 || debugVbo_ == 0 || debugSurfaceSizeLoc_ < 0) return;

    using GlDebugVertex = engine::render::debug::Vertex2D;
    constexpr std::size_t kMaxDebugLines = 4096;
    const std::size_t safeCount = std::min(lineCount, kMaxDebugLines);
    std::vector<GlDebugVertex> vertices;
    vertices.reserve(safeCount * 6);
    for (std::size_t i = 0; i < safeCount; ++i) {
        engine::render::debug::appendLineAsTriangles(lines[i], vertices);
    }
    if (vertices.empty()) return;

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(debugProgram_);
    glUniform2f(debugSurfaceSizeLoc_, static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight));

    glBindVertexArray(debugVao_);
    glBindBuffer(GL_ARRAY_BUFFER, debugVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(GlDebugVertex)),
                 vertices.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(vertices.size() / 3u);

    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));

    if (!blendEnabled) glDisable(GL_BLEND);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
}

void OpenGLRenderBackend::drawDebugTriangles(const DebugTriangle* triangles,
                                             std::size_t triangleCount,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (!triangles || triangleCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    ensureDebugPipeline();
    if (debugProgram_ == 0 || debugVao_ == 0 || debugVbo_ == 0 || debugSurfaceSizeLoc_ < 0) return;

    using GlDebugVertex = engine::render::debug::Vertex2D;
    constexpr std::size_t kMaxDebugTriangles = 4096;
    const std::size_t safeCount = std::min(triangleCount, kMaxDebugTriangles);
    std::vector<GlDebugVertex> vertices;
    vertices.reserve(safeCount * 3);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const DebugTriangle& t = triangles[i];
        vertices.push_back(GlDebugVertex{t.x1, t.y1, t.r, t.g, t.b, t.a});
        vertices.push_back(GlDebugVertex{t.x2, t.y2, t.r, t.g, t.b, t.a});
        vertices.push_back(GlDebugVertex{t.x3, t.y3, t.r, t.g, t.b, t.a});
    }
    if (vertices.empty()) return;

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(debugProgram_);
    glUniform2f(debugSurfaceSizeLoc_, static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight));

    glBindVertexArray(debugVao_);
    glBindBuffer(GL_ARRAY_BUFFER, debugVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(GlDebugVertex)),
                 vertices.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(vertices.size() / 3u);

    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));

    if (!blendEnabled) glDisable(GL_BLEND);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
}

void OpenGLRenderBackend::drawDebugSprites(const DebugSprite* sprites,
                                           std::size_t spriteCount,
                                           int surfaceWidth,
                                           int surfaceHeight) {
    if (!sprites || spriteCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    ensureSpritePipeline();
    if (spriteProgram_ == 0 || spriteVao_ == 0 || spriteVbo_ == 0 || spriteSurfaceSizeLoc_ < 0 ||
        spriteSamplerLoc_ < 0) {
        return;
    }

    struct SpriteVertex {
        float x;
        float y;
        float u;
        float v;
        float r;
        float g;
        float b;
        float a;
    };

    constexpr std::size_t kMaxSpriteQuads = 2048;
    const std::size_t safeCount = std::min(spriteCount, kMaxSpriteQuads);
    static thread_local std::vector<SpriteVertex> vertices;
    static thread_local std::vector<unsigned int> textureIds;
    vertices.clear();
    textureIds.clear();
    vertices.reserve(safeCount * 6u);
    textureIds.reserve(safeCount);

    for (std::size_t i = 0; i < safeCount; ++i) {
        const DebugSprite& sprite = sprites[i];
        if (sprite.w <= 0.0f || sprite.h <= 0.0f) continue;
        const unsigned int textureId = ensureSpriteTexture(sprite.texturePath);
        if (textureId == 0u) continue;

        const float x0 = sprite.x;
        const float y0 = sprite.y;
        const float x1 = sprite.x + sprite.w;
        const float y1 = sprite.y + sprite.h;

        vertices.push_back({x0, y0, sprite.u0, sprite.v0, sprite.r, sprite.g, sprite.b, sprite.a});
        vertices.push_back({x1, y0, sprite.u1, sprite.v0, sprite.r, sprite.g, sprite.b, sprite.a});
        vertices.push_back({x1, y1, sprite.u1, sprite.v1, sprite.r, sprite.g, sprite.b, sprite.a});
        vertices.push_back({x0, y0, sprite.u0, sprite.v0, sprite.r, sprite.g, sprite.b, sprite.a});
        vertices.push_back({x1, y1, sprite.u1, sprite.v1, sprite.r, sprite.g, sprite.b, sprite.a});
        vertices.push_back({x0, y1, sprite.u0, sprite.v1, sprite.r, sprite.g, sprite.b, sprite.a});
        textureIds.push_back(textureId);
    }
    if (vertices.empty() || textureIds.empty()) return;

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevActiveTexture = 0;
    GLint prevTexture2DOnActive = 0;
    GLint prevTexture2DOnUnit0 = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnUnit0);
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(spriteProgram_);
    glUniform2f(spriteSurfaceSizeLoc_, static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight));
    glUniform1i(spriteSamplerLoc_, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(SpriteVertex)),
                 vertices.data(),
                 GL_STREAM_DRAW);
    for (std::size_t i = 0; i < textureIds.size(); ++i) {
        glBindTexture(GL_TEXTURE_2D, textureIds[i]);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i * 6u), 6);
    }
    frameDrawCalls_ += static_cast<std::uint32_t>(textureIds.size());
    frameTriangles_ += static_cast<std::uint64_t>(textureIds.size() * 2u);

    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnUnit0));
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnActive));

    if (!blendEnabled) glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
}
