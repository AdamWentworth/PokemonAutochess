// tests/TestRenderSmoke.cpp
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/core/Paths.h"
#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"
#include "engine/utils/ShaderCache.h"

namespace {
bool envFlagEnabled(const char* name) {
    if (!name) return false;
    const char* v = std::getenv(name);
    if (!v) return false;
    std::string s(v);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return (s == "1" || s == "true" || s == "yes" || s == "on");
}

bool isFilesystemAbsolute(const std::string& path) {
    if (!path.empty() && (path[0] == '/' || path[0] == '\\')) return true;
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) return true;
    return false;
}

bool isProjectAbsolute(const std::string& path) {
    return path.rfind("assets/", 0) == 0;
}

std::string getDirectory(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    return path.substr(0, slash + 1);
}

std::string resolveIncludePath(const std::string& includeName, const std::string& parentFilePath) {
    if (isFilesystemAbsolute(includeName) || isProjectAbsolute(includeName)) {
        return includeName;
    }
    return getDirectory(parentFilePath) + includeName;
}

std::string resolveSmokeModelPath() {
    const char* env = std::getenv("PAC_TEST_MODEL");
    std::string rel = (env && *env) ? std::string(env) : "models/0016_Pidgey.glb";
    if (isFilesystemAbsolute(rel) || isProjectAbsolute(rel)) return rel;
    return engine::paths::asset(rel);
}

bool readText(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

bool scanIncludesRecursive(const std::string& path,
                           std::unordered_set<std::string>& visited,
                           std::string& outFail) {
    if (visited.count(path)) return true;
    visited.insert(path);

    std::string text;
    if (!readText(path, text)) {
        outFail = "Failed to read shader file: " + path;
        return false;
    }

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) trimmed.erase(trimmed.begin());
        if (trimmed.rfind("#include", 0) == 0) {
            const std::size_t q1 = trimmed.find('"');
            const std::size_t q2 = trimmed.find_last_of('"');
            if (q1 == std::string::npos || q2 == std::string::npos || q2 <= q1) {
                outFail = "Malformed #include in " + path;
                return false;
            }
            const std::string inc = trimmed.substr(q1 + 1, q2 - q1 - 1);
            const std::string resolved = resolveIncludePath(inc, path);
            if (!std::filesystem::exists(resolved)) {
                outFail = "Missing shader include: " + resolved + " (from " + path + ")";
                return false;
            }
            if (!scanIncludesRecursive(resolved, visited, outFail)) return false;
        }
    }
    return true;
}

bool checkShaderTree(const std::string& rootDir, std::string& outFail) {
    if (!std::filesystem::exists(rootDir)) {
        outFail = "Shader root not found: " + rootDir;
        return false;
    }

    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootDir)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext == ".vert" || ext == ".frag" || ext == ".glsl") {
            files.push_back(entry.path().string());
        }
    }

    if (files.empty()) {
        outFail = "No shader files found under " + rootDir;
        return false;
    }

    for (const auto& file : files) {
        std::unordered_set<std::string> visited;
        if (!scanIncludesRecursive(file, visited, outFail)) return false;
    }

    return true;
}

struct SDLInitGuard {
    bool active = false;
    ~SDLInitGuard() {
        if (active) SDL_Quit();
    }
};

struct SDLGLGuard {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    ~SDLGLGuard() {
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
    }
};

bool initGLContext(SDLInitGuard& sdl, SDLGLGuard& gl, std::string& outFail) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        outFail = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }
    sdl.active = true;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    gl.window = SDL_CreateWindow(
        "PAC_RenderSmoke",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        64, 64,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN
    );
    if (!gl.window) {
        outFail = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        return false;
    }

    gl.context = SDL_GL_CreateContext(gl.window);
    if (!gl.context) {
        outFail = std::string("SDL_GL_CreateContext failed: ") + SDL_GetError();
        return false;
    }

    SDL_GL_MakeCurrent(gl.window, gl.context);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        outFail = "gladLoadGLLoader failed.";
        return false;
    }

    return true;
}

bool drawMinimalFrame(ShaderCache& cache, std::string& outFail) {
    Camera3D camera(45.0f, 1.0f, 0.1f, 100.0f);
    camera.lookAt(glm::vec3(0.0f));

    BoardRenderer board(8, 8, 1.2f, &cache);
    const std::string modelPath = resolveSmokeModelPath();
    if (!std::filesystem::exists(modelPath)) {
        outFail = "Model not found for GL smoke test: " + modelPath;
        return false;
    }
    Model model(modelPath, &cache);
    const int animIndex = (model.getAnimationCount() > 0) ? 0 : -1;
    const float scaleFactor = model.getScaleFactor();
    const glm::mat4 instanceTransform = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));

    glViewport(0, 0, 64, 64);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    board.draw(camera);
    board.drawBench(camera);
    model.drawAnimated(camera, instanceTransform, 0.0f, animIndex);

    glFinish();
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::ostringstream ss;
        ss << "OpenGL error after draw: 0x" << std::hex << err;
        outFail = ss.str();
        return false;
    }

    return true;
}

bool runGLSmokeIfEnabled(std::string& outFail) {
    if (!envFlagEnabled("PAC_TEST_GL")) {
        return true; // Skip GL checks unless explicitly enabled.
    }

    SDLInitGuard sdl;
    SDLGLGuard gl;
    if (!initGLContext(sdl, gl, outFail)) return false;

    const std::string modelVert = engine::paths::asset("shaders/model/model.vert");
    const std::string modelFrag = engine::paths::asset("shaders/model/model.frag");
    const std::string uiVert = engine::paths::asset("shaders/ui/text.vert");
    const std::string uiFrag = engine::paths::asset("shaders/ui/text.frag");

    try {
        ShaderCache cache;
        cache.get(modelVert, modelFrag);
        cache.get(uiVert, uiFrag);

        if (!drawMinimalFrame(cache, outFail)) return false;
    } catch (const std::exception& e) {
        outFail = std::string("GL smoke failed: ") + e.what();
        return false;
    }

    return true;
}
} // namespace

bool test_render_pipeline_smoke(std::string& outFail) {
    const std::string shaderRoot = engine::paths::asset("shaders");
    if (!checkShaderTree(shaderRoot, outFail)) return false;
    if (!runGLSmokeIfEnabled(outFail)) return false;
    return true;
}
