#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

#include <glad/glad.h>
#include <stb_image.h>

#include "engine/render/Renderer.h"
#include "engine/render/DebugGeometry.h"

namespace {

std::string toLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool containsCi(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

unsigned int compileShader(GLenum type, const char* source) {
    const unsigned int shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned int linkProgram(unsigned int vs, unsigned int fs) {
    if (vs == 0 || fs == 0) return 0;
    const unsigned int program = glCreateProgram();
    if (program == 0) return 0;
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

GLint sanitizeWrapMode(int wrap) {
    switch (wrap) {
    case 33071: return GL_CLAMP_TO_EDGE;
    case 33648: return GL_MIRRORED_REPEAT;
    case 10497: return GL_REPEAT;
    default: return GL_REPEAT;
    }
}

float resolveVertexChannel(float channel, float fallback) {
    return (channel >= 0.0f) ? channel : fallback;
}

constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";

} // namespace

OpenGLRenderBackend::OpenGLRenderBackend()
    : renderer_(std::make_unique<Renderer>()) {
    glEnable(GL_DEPTH_TEST);
}

OpenGLRenderBackend::~OpenGLRenderBackend() {
    shutdown();
}

void OpenGLRenderBackend::beginFrame(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderBackend::onResize(int width, int height) {
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

std::string OpenGLRenderBackend::activeGpuName() const {
    const GLubyte* renderer = glGetString(GL_RENDERER);
    return renderer ? reinterpret_cast<const char*>(renderer) : std::string{};
}

bool OpenGLRenderBackend::activeGpuIsDiscrete() const {
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const std::string vendorStr = vendor ? reinterpret_cast<const char*>(vendor) : "";
    const std::string rendererStr = renderer ? reinterpret_cast<const char*>(renderer) : "";
    return !containsCi(vendorStr, "intel") && !containsCi(rendererStr, "intel");
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
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0) {
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

void OpenGLRenderBackend::shutdown() {
    destroyDebugPipeline();
    destroyWorldPipeline();
    destroySpritePipeline();
    clearTextureCaches();
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
}

void OpenGLRenderBackend::ensureDebugPipeline() {
    if (debugProgram_ != 0 && debugVao_ != 0 && debugVbo_ != 0 && debugSurfaceSizeLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) {
        return;
    }

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform vec2 uSurfaceSize;
        out vec4 vColor;
        void main() {
            vec2 ndc;
            ndc.x = (aPos.x / max(uSurfaceSize.x, 1.0)) * 2.0 - 1.0;
            ndc.y = 1.0 - (aPos.y / max(uSurfaceSize.y, 1.0)) * 2.0;
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vColor;
        }
    )GLSL";

    const unsigned int vs = compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }

    debugProgram_ = glCreateProgram();
    if (debugProgram_ == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }

    glAttachShader(debugProgram_, vs);
    glAttachShader(debugProgram_, fs);
    glLinkProgram(debugProgram_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkOk = 0;
    glGetProgramiv(debugProgram_, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        glDeleteProgram(debugProgram_);
        debugProgram_ = 0;
        return;
    }

    debugSurfaceSizeLoc_ = glGetUniformLocation(debugProgram_, "uSurfaceSize");
    if (debugSurfaceSizeLoc_ < 0) {
        destroyDebugPipeline();
        return;
    }

    glGenVertexArrays(1, &debugVao_);
    glGenBuffers(1, &debugVbo_);
    if (debugVao_ == 0 || debugVbo_ == 0) {
        destroyDebugPipeline();
        return;
    }

    glBindVertexArray(debugVao_);
    glBindBuffer(GL_ARRAY_BUFFER, debugVbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(float) * 6);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 2));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void OpenGLRenderBackend::destroyDebugPipeline() {
    if (debugVbo_ != 0) {
        glDeleteBuffers(1, &debugVbo_);
        debugVbo_ = 0;
    }
    if (debugVao_ != 0) {
        glDeleteVertexArrays(1, &debugVao_);
        debugVao_ = 0;
    }
    if (debugProgram_ != 0) {
        glDeleteProgram(debugProgram_);
        debugProgram_ = 0;
    }
    debugSurfaceSizeLoc_ = -1;
}

void OpenGLRenderBackend::ensureWorldPipeline() {
    if (worldProgram_ != 0 && worldVao_ != 0 && worldVbo_ != 0 && worldIbo_ != 0 &&
        worldViewProjLoc_ >= 0 && worldUseTextureLoc_ >= 0 && worldTextureSamplerLoc_ >= 0 &&
        worldWrapSLoc_ >= 0 && worldWrapTLoc_ >= 0 && worldAlphaModeLoc_ >= 0 && worldAlphaCutoffLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) return;

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in vec4 aColor;
        uniform mat4 uViewProj;
        out vec2 vUv;
        out vec4 vColor;
        void main() {
            gl_Position = uViewProj * vec4(aPos, 1.0);
            vUv = aUv;
            vColor = aColor;
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec2 vUv;
        in vec4 vColor;
        uniform float uUseTexture;
        uniform float uWrapS;
        uniform float uWrapT;
        uniform float uAlphaMode;
        uniform float uAlphaCutoff;
        uniform sampler2D uTexture;
        out vec4 FragColor;

        float applyWrap(float coord, float mode) {
            if (abs(mode - 33071.0) < 0.5) return clamp(coord, 0.0, 1.0);
            if (abs(mode - 33648.0) < 0.5) {
                float i = floor(coord);
                float f = fract(coord);
                float odd = mod(abs(i), 2.0);
                return (odd >= 1.0) ? (1.0 - f) : f;
            }
            return fract(coord);
        }

        void main() {
            vec4 tex = vec4(1.0);
            vec3 outSrgb = clamp(vColor.rgb, 0.0, 1.0);
            if (uUseTexture > 0.5) {
                vec2 uv = vec2(applyWrap(vUv.x, uWrapS), applyWrap(vUv.y, uWrapT));
                tex = texture(uTexture, uv);
                outSrgb = clamp(tex.rgb * vColor.rgb, 0.0, 1.0);
            }
            float outA = clamp(vColor.a * tex.a, 0.0, 1.0);
            if (uAlphaMode < 0.5) {
                outA = clamp(vColor.a, 0.0, 1.0);
            } else if (uAlphaMode < 1.5) {
                if (outA < clamp(uAlphaCutoff, 0.0, 1.0)) discard;
                outA = clamp(vColor.a, 0.0, 1.0);
            }
            FragColor = vec4(outSrgb, outA);
        }
    )GLSL";

    const unsigned int vs = compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }

    worldProgram_ = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (worldProgram_ == 0) return;

    worldViewProjLoc_ = glGetUniformLocation(worldProgram_, "uViewProj");
    worldUseTextureLoc_ = glGetUniformLocation(worldProgram_, "uUseTexture");
    worldTextureSamplerLoc_ = glGetUniformLocation(worldProgram_, "uTexture");
    worldWrapSLoc_ = glGetUniformLocation(worldProgram_, "uWrapS");
    worldWrapTLoc_ = glGetUniformLocation(worldProgram_, "uWrapT");
    worldAlphaModeLoc_ = glGetUniformLocation(worldProgram_, "uAlphaMode");
    worldAlphaCutoffLoc_ = glGetUniformLocation(worldProgram_, "uAlphaCutoff");
    if (worldViewProjLoc_ < 0 || worldUseTextureLoc_ < 0 || worldTextureSamplerLoc_ < 0 ||
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0) {
        destroyWorldPipeline();
        return;
    }

    glGenVertexArrays(1, &worldVao_);
    glGenBuffers(1, &worldVbo_);
    glGenBuffers(1, &worldIbo_);
    if (worldVao_ == 0 || worldVbo_ == 0 || worldIbo_ == 0) {
        destroyWorldPipeline();
        return;
    }

    glBindVertexArray(worldVao_);
    glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(WorldMeshVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 5));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLRenderBackend::destroyWorldPipeline() {
    if (worldIbo_ != 0) {
        glDeleteBuffers(1, &worldIbo_);
        worldIbo_ = 0;
    }
    if (worldVbo_ != 0) {
        glDeleteBuffers(1, &worldVbo_);
        worldVbo_ = 0;
    }
    if (worldVao_ != 0) {
        glDeleteVertexArrays(1, &worldVao_);
        worldVao_ = 0;
    }
    if (worldProgram_ != 0) {
        glDeleteProgram(worldProgram_);
        worldProgram_ = 0;
    }
    worldViewProjLoc_ = -1;
    worldUseTextureLoc_ = -1;
    worldTextureSamplerLoc_ = -1;
    worldWrapSLoc_ = -1;
    worldWrapTLoc_ = -1;
    worldAlphaModeLoc_ = -1;
    worldAlphaCutoffLoc_ = -1;
}

void OpenGLRenderBackend::ensureSpritePipeline() {
    if (spriteProgram_ != 0 && spriteVao_ != 0 && spriteVbo_ != 0 &&
        spriteSurfaceSizeLoc_ >= 0 && spriteSamplerLoc_ >= 0) {
        return;
    }
    if (!GLAD_GL_VERSION_3_3) return;

    static constexpr const char* kVs = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in vec4 aColor;
        uniform vec2 uSurfaceSize;
        out vec2 vUv;
        out vec4 vColor;
        void main() {
            vec2 ndc;
            ndc.x = (aPos.x / max(uSurfaceSize.x, 1.0)) * 2.0 - 1.0;
            ndc.y = 1.0 - (aPos.y / max(uSurfaceSize.y, 1.0)) * 2.0;
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUv = aUv;
            vColor = aColor;
        }
    )GLSL";

    static constexpr const char* kFs = R"GLSL(
        #version 330 core
        in vec2 vUv;
        in vec4 vColor;
        uniform sampler2D uTexture;
        out vec4 FragColor;
        void main() {
            FragColor = texture(uTexture, vUv) * vColor;
        }
    )GLSL";

    const unsigned int vs = compileShader(GL_VERTEX_SHADER, kVs);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFs);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return;
    }
    spriteProgram_ = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (spriteProgram_ == 0) return;

    spriteSurfaceSizeLoc_ = glGetUniformLocation(spriteProgram_, "uSurfaceSize");
    spriteSamplerLoc_ = glGetUniformLocation(spriteProgram_, "uTexture");
    if (spriteSurfaceSizeLoc_ < 0 || spriteSamplerLoc_ < 0) {
        destroySpritePipeline();
        return;
    }

    glGenVertexArrays(1, &spriteVao_);
    glGenBuffers(1, &spriteVbo_);
    if (spriteVao_ == 0 || spriteVbo_ == 0) {
        destroySpritePipeline();
        return;
    }

    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_STREAM_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(float) * 8);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 2));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(sizeof(float) * 4));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (spriteFallbackTexture_ == 0) {
        static const unsigned char kFallbackRgba[16] = {
            72,  90, 108, 255,
            56,  70,  84, 255,
            56,  70,  84, 255,
            72,  90, 108, 255
        };
        glGenTextures(1, &spriteFallbackTexture_);
        if (spriteFallbackTexture_ != 0) {
            glBindTexture(GL_TEXTURE_2D, spriteFallbackTexture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA8,
                         2,
                         2,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         kFallbackRgba);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        spriteTextures_[kFallbackSpriteTextureKey] = spriteFallbackTexture_;
    }
}

void OpenGLRenderBackend::destroySpritePipeline() {
    if (spriteVbo_ != 0) {
        glDeleteBuffers(1, &spriteVbo_);
        spriteVbo_ = 0;
    }
    if (spriteVao_ != 0) {
        glDeleteVertexArrays(1, &spriteVao_);
        spriteVao_ = 0;
    }
    if (spriteProgram_ != 0) {
        glDeleteProgram(spriteProgram_);
        spriteProgram_ = 0;
    }
    spriteSurfaceSizeLoc_ = -1;
    spriteSamplerLoc_ = -1;
}

unsigned int OpenGLRenderBackend::ensureWorldTexture(const WorldTextureData* textureData) {
    if (!textureData || !textureData->rgba || textureData->width <= 0 || textureData->height <= 0 ||
        !textureData->key || textureData->key[0] == '\0') {
        return 0;
    }

    const std::string key(textureData->key);
    auto existing = worldTextures_.find(key);
    if (existing != worldTextures_.end()) {
        return existing->second.textureId;
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) return 0;

    const GLint wrapS = sanitizeWrapMode(textureData->wrapS);
    const GLint wrapT = sanitizeWrapMode(textureData->wrapT);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 textureData->width,
                 textureData->height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 textureData->rgba);

    TextureCacheEntry entry;
    entry.textureId = textureId;
    entry.width = textureData->width;
    entry.height = textureData->height;
    entry.wrapS = textureData->wrapS;
    entry.wrapT = textureData->wrapT;
    worldTextures_.emplace(key, entry);
    return textureId;
}

unsigned int OpenGLRenderBackend::ensureSpriteTexture(const std::string& texturePath) {
    ensureSpritePipeline();
    if (texturePath.empty()) return spriteFallbackTexture_;

    auto existing = spriteTextures_.find(texturePath);
    if (existing != spriteTextures_.end()) return existing->second;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        std::string altPath = texturePath;
        std::replace(altPath.begin(), altPath.end(), '\\', '/');
        if (altPath != texturePath) {
            pixels = stbi_load(altPath.c_str(), &width, &height, &channels, 4);
        }
    }

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return spriteFallbackTexture_;
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) {
        stbi_image_free(pixels);
        return spriteFallbackTexture_;
    }

    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
#if defined(GL_TEXTURE_MAX_ANISOTROPY_EXT) && defined(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)
    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        float maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        const float requested = std::min(8.0f, std::max(1.0f, maxAniso));
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, requested);
    }
#endif
    stbi_image_free(pixels);

    spriteTextures_[texturePath] = textureId;
    return textureId;
}

void OpenGLRenderBackend::clearTextureCaches() {
    std::vector<unsigned int> texturesToDelete;
    texturesToDelete.reserve(worldTextures_.size() + spriteTextures_.size() + 2u);
    for (const auto& [_, entry] : worldTextures_) {
        if (entry.textureId != 0) texturesToDelete.push_back(entry.textureId);
    }
    for (const auto& [_, textureId] : spriteTextures_) {
        if (textureId != 0) texturesToDelete.push_back(textureId);
    }
    if (worldFallbackTexture_ != 0) texturesToDelete.push_back(worldFallbackTexture_);
    if (spriteFallbackTexture_ != 0) texturesToDelete.push_back(spriteFallbackTexture_);
    if (!texturesToDelete.empty()) {
        std::sort(texturesToDelete.begin(), texturesToDelete.end());
        texturesToDelete.erase(std::unique(texturesToDelete.begin(), texturesToDelete.end()), texturesToDelete.end());
        glDeleteTextures(static_cast<GLsizei>(texturesToDelete.size()), texturesToDelete.data());
    }
    worldTextures_.clear();
    spriteTextures_.clear();
    worldFallbackTexture_ = 0;
    spriteFallbackTexture_ = 0;
}
