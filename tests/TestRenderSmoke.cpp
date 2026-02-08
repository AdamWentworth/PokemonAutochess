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

#include "engine/core/Paths.h"
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

bool compileShadersIfEnabled(std::string& outFail) {
    if (!envFlagEnabled("PAC_TEST_GL")) {
        return true; // Skip GL compile unless explicitly enabled.
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        outFail = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "PAC_RenderSmoke",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        64, 64,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN
    );
    if (!window) {
        outFail = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        SDL_Quit();
        return false;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) {
        outFail = std::string("SDL_GL_CreateContext failed: ") + SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    SDL_GL_MakeCurrent(window, ctx);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        outFail = "gladLoadGLLoader failed.";
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    const std::string modelVert = engine::paths::asset("shaders/model/model.vert");
    const std::string modelFrag = engine::paths::asset("shaders/model/model.frag");
    const std::string uiVert = engine::paths::asset("shaders/ui/text.vert");
    const std::string uiFrag = engine::paths::asset("shaders/ui/text.frag");

    try {
        ShaderCache cache;
        cache.get(modelVert, modelFrag);
        cache.get(uiVert, uiFrag);
    } catch (const std::exception& e) {
        outFail = std::string("Shader compile failed: ") + e.what();
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return true;
}
} // namespace

bool test_render_pipeline_smoke(std::string& outFail) {
    const std::string shaderRoot = engine::paths::asset("shaders");
    if (!checkShaderTree(shaderRoot, outFail)) return false;
    if (!compileShadersIfEnabled(outFail)) return false;
    return true;
}
