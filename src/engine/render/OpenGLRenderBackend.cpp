#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include <glad/glad.h>

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

void OpenGLRenderBackend::shutdown() {
    destroyDebugPipeline();
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
