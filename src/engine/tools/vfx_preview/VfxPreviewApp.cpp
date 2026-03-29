#include "engine/tools/vfx_preview/VfxPreviewApp.h"

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Paths.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"
#include "engine/tools/vfx_preview/IVfxPreviewProject.h"
#include "engine/ui/TextRenderer.h"

namespace engine::tools::vfx_preview {

namespace {

constexpr int kInitialWindowWidth = 1600;
constexpr int kInitialWindowHeight = 900;
constexpr int kPreviewWindowMarginPx = 80;
constexpr float kTargetMoveSpeed = 2.4f;
constexpr float kEmitterMoveSpeed = 2.0f;
constexpr float kEmitterHeightSpeed = 1.25f;
constexpr float kMouseOrbitScale = 0.005f;
constexpr float kMousePanScale = 0.02f;
constexpr float kMouseWheelZoomStep = 0.5f;
constexpr float kKeyboardPanSpeed = 4.2f;

glm::vec3 safeForwardXZ(const glm::vec3& value) {
    glm::vec3 forward(value.x, 0.0f, value.z);
    const float lenSq = glm::dot(forward, forward);
    if (lenSq <= 0.000001f) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return forward / std::sqrt(lenSq);
}

std::string formatVec3(const glm::vec3& v) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    return out.str();
}

void fitWindowToDisplay(SDL_Window* window) {
    if (window == nullptr) return;

    const int displayIndex = SDL_GetWindowDisplayIndex(window);
    if (displayIndex < 0) return;

    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(displayIndex, &usable) != 0) return;
    if (usable.w <= 0 || usable.h <= 0) return;

    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(window, &windowW, &windowH);
    if (windowW <= 0 || windowH <= 0) return;

    const int maxW = std::max(640, usable.w - kPreviewWindowMarginPx);
    const int maxH = std::max(480, usable.h - kPreviewWindowMarginPx);
    const int targetW = std::min(windowW, maxW);
    const int targetH = std::min(windowH, maxH);
    if (targetW == windowW && targetH == windowH) return;

    SDL_SetWindowSize(window, targetW, targetH);
    SDL_SetWindowPosition(window,
                          usable.x + std::max(0, (usable.w - targetW) / 2),
                          usable.y + std::max(0, (usable.h - targetH) / 2));
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;

    GLint logLen = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
    std::string log(static_cast<size_t>(std::max(1, logLen)), '\0');
    glGetShaderInfoLog(shader, logLen, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Preview shader compile failed: " + log);
}

GLuint linkProgram(GLuint vert, GLuint frag) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) return program;

    GLint logLen = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
    std::string log(static_cast<size_t>(std::max(1, logLen)), '\0');
    glGetProgramInfoLog(program, logLen, nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("Preview shader link failed: " + log);
}

void printControls() {
    std::cout
        << "[VfxPreviewer] Controls\n"
        << "  Space: replay current effect\n"
        << "  P: pause/resume\n"
        << "  . : single-step one frame while paused\n"
        << "  , : single-step five frames while paused\n"
        << "  R or F5: reload current effect and replay\n"
        << "  L: toggle auto-loop\n"
        << "  G: toggle primary backdrop\n"
        << "  B: toggle secondary backdrop\n"
        << "  Tab: switch preview effect\n"
        << "  [ / ]: previous/next preview mode\n"
        << "  /: cycle preview mode\n"
        << "  1 / 2 / 3: set sim speed to 0.5x / 1.0x / 2.0x\n"
        << "  WASD: move target marker\n"
        << "  IJKL: move emitter marker\n"
        << "  U / O: move emitter down/up\n"
        << "  Arrow keys: pan camera\n"
        << "  Left mouse drag: pan camera\n"
        << "  Right mouse drag: orbit camera\n"
        << "  Mouse wheel: zoom\n"
        << "  H: toggle help overlay\n"
        << "  F1: print controls again\n"
        << "  Esc: quit\n";
}

std::vector<std::string> wrapTextToWidth(const TextRenderer& text,
                                         const std::string& input,
                                         float maxWidth,
                                         float scale) {
    if (input.empty()) return {""};
    if (maxWidth <= 0.0f || text.measureTextWidth(input, scale) <= maxWidth) {
        return {input};
    }

    std::vector<std::string> out;
    std::string current;
    std::string word;

    const auto flushCurrent = [&]() {
        if (!current.empty()) {
            out.push_back(current);
            current.clear();
        }
    };

    for (size_t i = 0; i <= input.size(); ++i) {
        const char c = (i < input.size()) ? input[i] : ' ';
        if (c == ' ' || c == '\n' || i == input.size()) {
            if (!word.empty()) {
                const std::string candidate = current.empty() ? word : (current + " " + word);
                if (!current.empty() && text.measureTextWidth(candidate, scale) > maxWidth) {
                    flushCurrent();
                    current = word;
                } else if (current.empty() && text.measureTextWidth(word, scale) > maxWidth) {
                    out.push_back(word);
                } else {
                    current = candidate;
                }
                word.clear();
            }
            if (c == '\n') flushCurrent();
            if (i == input.size()) flushCurrent();
        } else {
            word.push_back(c);
        }
    }

    if (out.empty()) out.push_back(input);
    return out;
}

struct DebugVertex {
    glm::vec3 pos{0.0f};
    glm::vec3 color{1.0f};
};

class DebugLineRenderer final : public IPreviewDebugDraw {
public:
    DebugLineRenderer() {
        static constexpr const char* kVert = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 u_MVP;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = u_MVP * vec4(aPos, 1.0);
}
)";

        static constexpr const char* kFrag = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

        const GLuint vert = compileShader(GL_VERTEX_SHADER, kVert);
        const GLuint frag = compileShader(GL_FRAGMENT_SHADER, kFrag);
        program_ = linkProgram(vert, frag);
        glDeleteShader(vert);
        glDeleteShader(frag);

        uMvp_ = glGetUniformLocation(program_, "u_MVP");

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), (void*)offsetof(DebugVertex, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), (void*)offsetof(DebugVertex, color));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    ~DebugLineRenderer() override {
        if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
        if (program_ != 0) glDeleteProgram(program_);
    }

    void clear() {
        vertices_.clear();
    }

    void addLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) override {
        vertices_.push_back(DebugVertex{a, color});
        vertices_.push_back(DebugVertex{b, color});
    }

    void addCross(const glm::vec3& center, float radius, const glm::vec3& color) override {
        addLine(center + glm::vec3(-radius, 0.0f, 0.0f), center + glm::vec3(radius, 0.0f, 0.0f), color);
        addLine(center + glm::vec3(0.0f, 0.0f, -radius), center + glm::vec3(0.0f, 0.0f, radius), color);
        addLine(center, center + glm::vec3(0.0f, radius * 1.6f, 0.0f), color);
    }

    void addCircleXZ(const glm::vec3& center,
                     float radius,
                     const glm::vec3& color,
                     int segments = 32) override {
        if (radius <= 0.0f || segments < 3) return;
        const float step = glm::two_pi<float>() / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i) {
            const float a0 = step * static_cast<float>(i);
            const float a1 = step * static_cast<float>(i + 1);
            const glm::vec3 p0 = center + glm::vec3(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
            const glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
            addLine(p0, p1, color);
        }
    }

    void addArrow(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color) override {
        addLine(from, to, color);

        const glm::vec3 forward = safeForwardXZ(to - from);
        glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward);
        const float rightLenSq = glm::dot(right, right);
        if (rightLenSq <= 0.000001f) {
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            right /= std::sqrt(rightLenSq);
        }

        const glm::vec3 headBase = to - forward * 0.18f;
        addLine(to, headBase + right * 0.08f, color);
        addLine(to, headBase - right * 0.08f, color);
    }

    void draw(const Camera3D& camera) {
        if (vertices_.empty()) return;

        const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
        const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);

        GLint prevProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

        GLint prevVao = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);

        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glLineWidth(2.0f);

        glUseProgram(program_);
        const glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::mat4(1.0f);
        glUniformMatrix4fv(uMvp_, 1, GL_FALSE, &mvp[0][0]);

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices_.size() * sizeof(DebugVertex)),
                     vertices_.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices_.size()));

        glBindVertexArray(static_cast<GLuint>(prevVao));
        glUseProgram(static_cast<GLuint>(prevProgram));

        if (blendWasEnabled) glEnable(GL_BLEND);
        if (cullWasEnabled) glEnable(GL_CULL_FACE);
    }

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint uMvp_ = -1;
    std::vector<DebugVertex> vertices_;
};

std::string makeWindowTitle(const IVfxPreviewProject& project,
                            const IVfxPreviewEffect& effect,
                            std::size_t rigIndex,
                            const PreviewSceneState& scene) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << "PAC_VfxPreviewer - " << project.projectName() << " - " << effect.name()
        << " | mode " << project.rigName(rigIndex)
        << " | " << (scene.paused ? "paused" : "running")
        << " | loop " << (scene.loopPlayback ? "on" : "off")
        << " | speed " << scene.timeScale << "x"
        << " | active " << effect.activeCount()
        << " | emitter " << formatVec3(scene.emitter)
        << " | target " << formatVec3(scene.target);
    return out.str();
}

void renderOverlay(TextRenderer* text,
                   const IVfxPreviewProject& project,
                   const IVfxPreviewEffect& effect,
                   std::size_t rigIndex,
                   const PreviewSceneState& scene,
                   bool showHelp,
                   bool primaryBackdropEnabled,
                   bool secondaryBackdropEnabled) {
    if (text == nullptr || !showHelp) return;

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthWasEnabled) glDisable(GL_DEPTH_TEST);

    int vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);

    const float x = 18.0f;
    float y = 18.0f;
    const float scale = 0.50f;
    const float lineH = std::max(15.0f, text->measureTextHeight(scale) + 3.0f);
    const float maxLineWidth = std::max(220.0f, static_cast<float>(vp[2]) - x * 2.0f - 12.0f);
    const float maxY = std::max(y + lineH, static_cast<float>(vp[3]) - 12.0f);

    const auto renderWrappedLine = [&](const std::string& line,
                                       const glm::vec3& color,
                                       float alpha) -> bool {
        for (const std::string& wrapped : wrapTextToWidth(*text, line, maxLineWidth, scale)) {
            if (y + lineH > maxY) return false;
            text->renderText(wrapped, x, y, color, scale, alpha);
            y += lineH;
        }
        return true;
    };

    if (!renderWrappedLine(std::string("Project: ") + std::string(project.projectName()) +
                               " | Effect: " + std::string(effect.name()),
                           glm::vec3(1.0f, 0.93f, 0.72f),
                           0.95f) ||
        !renderWrappedLine(std::string("Mode: ") + std::string(project.rigName(rigIndex)) +
                               " | Active: " + std::to_string(effect.activeCount()) +
                               " | Loop: " + (scene.loopPlayback ? "on" : "off") +
                               " | Speed: " +
                               (scene.timeScale == 0.5f ? "0.5x" : scene.timeScale == 2.0f ? "2.0x" : "1.0x"),
                           glm::vec3(0.82f, 0.90f, 1.0f),
                           0.92f) ||
        !renderWrappedLine("Space replay | Tab switch effect | [ ] prev/next mode | / cycle mode | R/F5 reload | P pause | . / , step",
                           glm::vec3(0.90f, 0.90f, 0.90f),
                           0.9f) ||
        !renderWrappedLine("WASD target | IJKL emitter | U/O height | Arrow keys pan camera",
                           glm::vec3(0.90f, 0.90f, 0.90f),
                           0.9f) ||
        !renderWrappedLine("Left drag pan | Right drag orbit | Wheel zoom | G/B backdrop | H hide help",
                           glm::vec3(0.90f, 0.90f, 0.90f),
                           0.9f) ||
        !renderWrappedLine(std::string("Backdrop A: ") + (primaryBackdropEnabled ? "on" : "off") +
                               " | Backdrop B: " + (secondaryBackdropEnabled ? "on" : "off"),
                           glm::vec3(0.90f, 0.90f, 0.90f),
                           0.9f) ||
        !renderWrappedLine(std::string("Emitter ") + formatVec3(scene.emitter) +
                               " | Target " + formatVec3(scene.target),
                           glm::vec3(0.78f, 1.0f, 0.82f),
                           0.9f)) {
        if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
        return;
    }

    for (const std::string& line : project.overlayLines(scene, rigIndex)) {
        if (!renderWrappedLine(line, glm::vec3(0.92f, 0.88f, 0.72f), 0.88f)) break;
    }
    for (const std::string& line : effect.overlayLines(scene)) {
        if (!renderWrappedLine(line, glm::vec3(0.95f, 0.78f, 0.78f), 0.88f)) break;
    }

    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
}

} // namespace

VfxPreviewApp::VfxPreviewApp(std::unique_ptr<IVfxPreviewProject> project)
    : project_(std::move(project)) {}

VfxPreviewApp::~VfxPreviewApp() = default;

int VfxPreviewApp::run() {
    if (!project_) {
        throw std::runtime_error("VfxPreviewApp requires a preview project.");
    }
    if (project_->effectCount() == 0u) {
        throw std::runtime_error("VfxPreviewApp requires at least one preview effect.");
    }

    std::unique_ptr<Window> window;
    std::unique_ptr<TextRenderer> overlayText;
    bool ttfReady = false;

    try {
        window = std::make_unique<Window>(
            "PAC_VfxPreviewer",
            kInitialWindowWidth,
            kInitialWindowHeight,
            Window::GraphicsApi::OpenGL,
            true);
        fitWindowToDisplay(window->getSDLWindow());

        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            throw std::runtime_error("Failed to initialize OpenGL functions via glad.");
        }

        SDL_GL_SetSwapInterval(1);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);
        glClearColor(0.05f, 0.06f, 0.09f, 1.0f);

        if (TTF_Init() == 0) {
            ttfReady = true;
            overlayText = std::make_unique<TextRenderer>(engine::paths::asset("fonts/GillSans.ttf"), 20);
        } else {
            std::cerr << "[VfxPreviewer] Warning: TTF_Init failed: " << TTF_GetError() << "\n";
        }

        int drawableW = 0;
        int drawableH = 0;
        SDL_GL_GetDrawableSize(window->getSDLWindow(), &drawableW, &drawableH);
        drawableW = std::max(1, drawableW);
        drawableH = std::max(1, drawableH);
        glViewport(0, 0, drawableW, drawableH);

        Camera3D camera(55.0f, static_cast<float>(drawableW) / static_cast<float>(drawableH), 0.05f, 100.0f);
        camera.setPosition(glm::vec3(0.0f, 4.9f, 7.1f));
        camera.lookAt(glm::vec3(0.0f, 0.45f, 2.0f));

        DebugLineRenderer debugLines;
        PreviewSceneState scene{};
        bool showHelpOverlay = true;
        bool showPrimaryBackdrop = true;
        bool showSecondaryBackdrop = false;
        std::size_t activeEffectIndex = 0u;
        std::size_t activeRigIndex = 0u;

        auto activateCurrentEffect = [&](bool applyRigDefaults) {
            if (applyRigDefaults) {
                project_->applyRigDefaults(activeRigIndex, scene);
                showPrimaryBackdrop = project_->defaultPrimaryBackdropEnabled(activeRigIndex);
                showSecondaryBackdrop = project_->defaultSecondaryBackdropEnabled(activeRigIndex);
            }
            project_->effectAt(activeEffectIndex).onActivated(scene);
            project_->onEffectActivated(activeEffectIndex);
            project_->constrainScene(activeRigIndex, scene);
            project_->effectAt(activeEffectIndex).replay(scene);
        };

        activateCurrentEffect(true);
        project_->effectAt(activeEffectIndex).onResize(drawableW, drawableH);

        printControls();

        bool running = true;
        bool orbiting = false;
        bool panning = false;
        std::uint64_t titleFrame = 0;
        std::uint64_t prevTicks = SDL_GetPerformanceCounter();

        while (running) {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                        event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        SDL_GL_GetDrawableSize(window->getSDLWindow(), &drawableW, &drawableH);
                        drawableW = std::max(1, drawableW);
                        drawableH = std::max(1, drawableH);
                        glViewport(0, 0, drawableW, drawableH);
                        camera.setAspectRatio(static_cast<float>(drawableW) / static_cast<float>(drawableH));
                        project_->effectAt(activeEffectIndex).onResize(drawableW, drawableH);
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) panning = true;
                    if (event.button.button == SDL_BUTTON_RIGHT) orbiting = true;
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) panning = false;
                    if (event.button.button == SDL_BUTTON_RIGHT) orbiting = false;
                    break;
                case SDL_MOUSEMOTION:
                    if (orbiting) {
                        camera.orbit(
                            -static_cast<float>(event.motion.xrel) * kMouseOrbitScale,
                            -static_cast<float>(event.motion.yrel) * kMouseOrbitScale);
                    }
                    if (panning) {
                        camera.panPlanar(
                            static_cast<float>(event.motion.xrel),
                            static_cast<float>(event.motion.yrel),
                            kMousePanScale);
                    }
                    break;
                case SDL_MOUSEWHEEL:
                    camera.zoom(static_cast<float>(event.wheel.y) * kMouseWheelZoomStep);
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE: project_->effectAt(activeEffectIndex).replay(scene); break;
                    case SDLK_p: scene.paused = !scene.paused; break;
                    case SDLK_PERIOD:
                        if (scene.paused) project_->effectAt(activeEffectIndex).stepFrames(1, scene);
                        break;
                    case SDLK_COMMA:
                        if (scene.paused) project_->effectAt(activeEffectIndex).stepFrames(5, scene);
                        break;
                    case SDLK_r:
                    case SDLK_F5:
                        project_->effectAt(activeEffectIndex).reload(scene);
                        break;
                    case SDLK_l: scene.loopPlayback = !scene.loopPlayback; break;
                    case SDLK_g: showPrimaryBackdrop = !showPrimaryBackdrop; break;
                    case SDLK_b: showSecondaryBackdrop = !showSecondaryBackdrop; break;
                    case SDLK_TAB:
                        activeEffectIndex = (activeEffectIndex + 1u) % project_->effectCount();
                        project_->effectAt(activeEffectIndex).onResize(drawableW, drawableH);
                        activateCurrentEffect(false);
                        break;
                    case SDLK_LEFTBRACKET:
                        if (project_->rigCount() > 0u) {
                            activeRigIndex =
                                (activeRigIndex + project_->rigCount() - 1u) % project_->rigCount();
                            activateCurrentEffect(true);
                        }
                        break;
                    case SDLK_RIGHTBRACKET:
                        if (project_->rigCount() > 0u) {
                            activeRigIndex = (activeRigIndex + 1u) % project_->rigCount();
                            activateCurrentEffect(true);
                        }
                        break;
                    case SDLK_SLASH:
                    case SDLK_KP_DIVIDE:
                        if (project_->rigCount() > 0u) {
                            activeRigIndex = (activeRigIndex + 1u) % project_->rigCount();
                            activateCurrentEffect(true);
                        }
                        break;
                    case SDLK_1: scene.timeScale = 0.5f; break;
                    case SDLK_2: scene.timeScale = 1.0f; break;
                    case SDLK_3: scene.timeScale = 2.0f; break;
                    case SDLK_h: showHelpOverlay = !showHelpOverlay; break;
                    case SDLK_F1: printControls(); break;
                    default: break;
                    }
                    break;
                default:
                    break;
                }
            }

            const std::uint64_t nowTicks = SDL_GetPerformanceCounter();
            double frameDt = 0.0;
            if (prevTicks != 0u) {
                frameDt = static_cast<double>(nowTicks - prevTicks) /
                          static_cast<double>(SDL_GetPerformanceFrequency());
            }
            prevTicks = nowTicks;
            frameDt = std::clamp(frameDt, 0.0, 0.1);

            glm::vec3 targetMove(0.0f);
            glm::vec3 emitterMove(0.0f);
            glm::vec3 cameraPan(0.0f);

            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_W]) targetMove.z -= 1.0f;
            if (keys[SDL_SCANCODE_S]) targetMove.z += 1.0f;
            if (keys[SDL_SCANCODE_A]) targetMove.x -= 1.0f;
            if (keys[SDL_SCANCODE_D]) targetMove.x += 1.0f;

            if (keys[SDL_SCANCODE_I]) emitterMove.z -= 1.0f;
            if (keys[SDL_SCANCODE_K]) emitterMove.z += 1.0f;
            if (keys[SDL_SCANCODE_J]) emitterMove.x -= 1.0f;
            if (keys[SDL_SCANCODE_L]) emitterMove.x += 1.0f;
            if (keys[SDL_SCANCODE_U]) emitterMove.y -= 1.0f;
            if (keys[SDL_SCANCODE_O]) emitterMove.y += 1.0f;

            if (keys[SDL_SCANCODE_UP]) cameraPan.z -= 1.0f;
            if (keys[SDL_SCANCODE_DOWN]) cameraPan.z += 1.0f;
            if (keys[SDL_SCANCODE_LEFT]) cameraPan.x -= 1.0f;
            if (keys[SDL_SCANCODE_RIGHT]) cameraPan.x += 1.0f;

            auto normalizeSafe = [](glm::vec3 v) -> glm::vec3 {
                const float lenSq = glm::dot(v, v);
                if (lenSq <= 0.000001f) return glm::vec3(0.0f);
                return v / std::sqrt(lenSq);
            };

            bool sceneEdited = false;
            if (glm::dot(targetMove, targetMove) > 0.0f) {
                scene.target += normalizeSafe(targetMove) * (kTargetMoveSpeed * static_cast<float>(frameDt));
                sceneEdited = true;
            }
            if (glm::dot(emitterMove, emitterMove) > 0.0f) {
                glm::vec3 delta = normalizeSafe(emitterMove);
                scene.emitter += glm::vec3(
                    delta.x * (kEmitterMoveSpeed * static_cast<float>(frameDt)),
                    delta.y * (kEmitterHeightSpeed * static_cast<float>(frameDt)),
                    delta.z * (kEmitterMoveSpeed * static_cast<float>(frameDt)));
                sceneEdited = true;
            }
            if (sceneEdited) {
                project_->constrainScene(activeRigIndex, scene);
            }

            if (glm::dot(cameraPan, cameraPan) > 0.0f) {
                camera.move(glm::vec3(
                    cameraPan.x * kKeyboardPanSpeed * static_cast<float>(frameDt),
                    0.0f,
                    cameraPan.z * kKeyboardPanSpeed * static_cast<float>(frameDt)));
            }

            if (!scene.paused) {
                project_->update(static_cast<float>(frameDt) * scene.timeScale, activeRigIndex, scene);
                project_->effectAt(activeEffectIndex).update(
                    static_cast<float>(frameDt) * scene.timeScale,
                    scene);
            }

            glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const PreviewFrameContext frameCtx{
                camera,
                drawableW,
                drawableH,
                overlayText.get(),
            };

            project_->renderBackdrop(frameCtx,
                                     activeRigIndex,
                                     scene,
                                     showPrimaryBackdrop,
                                     showSecondaryBackdrop);
            project_->effectAt(activeEffectIndex).render(frameCtx);

            debugLines.clear();
            project_->appendDebugMarkers(debugLines, scene);
            debugLines.draw(camera);

            renderOverlay(overlayText.get(),
                          *project_,
                          project_->effectAt(activeEffectIndex),
                          activeRigIndex,
                          scene,
                          showHelpOverlay,
                          showPrimaryBackdrop,
                          showSecondaryBackdrop);

            window->swapBuffers();

            if ((titleFrame++ % 8u) == 0u) {
                window->setTitle(
                    makeWindowTitle(*project_, project_->effectAt(activeEffectIndex), activeRigIndex, scene));
            }
        }

        overlayText.reset();
        window.reset();
        if (ttfReady) TTF_Quit();
        SDL_Quit();
        return 0;
    } catch (...) {
        overlayText.reset();
        window.reset();
        if (ttfReady && TTF_WasInit()) TTF_Quit();
        SDL_Quit();
        throw;
    }
}

} // namespace engine::tools::vfx_preview
