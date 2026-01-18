// Shader.cpp

#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_set>

// --------------------
// Include expansion helpers (local to this .cpp)
// --------------------

static std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Shader] Error opening shader file: " << path << "\n";
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string getDirectory(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    return path.substr(0, slash + 1);
}

static bool isProjectAbsolutePath(const std::string& inc) {
    // Treat includes starting with "assets/" as "rooted from project run dir"
    // Example: #include "assets/shaders/lib/noise2d.glsl"
    return inc.rfind("assets/", 0) == 0;
}

static bool isFilesystemAbsolutePath(const std::string& inc) {
    // Very simple absolute-path check (POSIX + Windows drive letter)
    if (!inc.empty() && (inc[0] == '/' || inc[0] == '\\')) return true;
    if (inc.size() >= 3 && std::isalpha((unsigned char)inc[0]) && inc[1] == ':' &&
        (inc[2] == '\\' || inc[2] == '/')) return true;
    return false;
}

static bool parseInclude(const std::string& line, std::string& outPath) {
    // Supports:
    //   #include "path"
    // Optional whitespace is allowed.
    // Ignores angle-bracket includes.
    size_t pos = line.find("#include");
    if (pos == std::string::npos) return false;

    size_t q1 = line.find('"', pos);
    if (q1 == std::string::npos) return false;

    size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos || q2 <= q1 + 1) return false;

    outPath = line.substr(q1 + 1, q2 - (q1 + 1));
    return true;
}

static std::string expandIncludesRecursive(
    const std::string& filePath,
    std::unordered_set<std::string>& includedFiles,
    bool isTopLevel
) {
    // Prevent double-including the same file (acts like #pragma once).
    if (includedFiles.count(filePath) > 0) {
        return "";
    }
    includedFiles.insert(filePath);

    std::string src = readTextFile(filePath);
    if (src.empty()) return "";

    const std::string baseDir = getDirectory(filePath);

    std::stringstream in(src);
    std::stringstream out;

    std::string line;
    while (std::getline(in, line)) {
        std::string inc;
        if (parseInclude(line, inc)) {
            std::string resolved;

            if (isFilesystemAbsolutePath(inc) || isProjectAbsolutePath(inc)) {
                // use as-is
                resolved = inc;
            } else {
                // relative to the including file
                resolved = baseDir + inc;
            }

            out << expandIncludesRecursive(resolved, includedFiles, false);
            continue;
        }

        // Only the top-level file should keep a #version line.
        // Included helper files should NOT declare #version.
        if (!isTopLevel) {
            // trim leading spaces for the check
            size_t firstNonSpace = line.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos) {
                if (line.compare(firstNonSpace, 8, "#version") == 0) {
                    continue;
                }
            }
        }

        out << line << "\n";
    }

    return out.str();
}

// --------------------
// Shader implementation
// --------------------

Shader::Shader(const char* vertexPath, const char* fragmentPath) {
    std::string vertexCode = loadSource(vertexPath);
    std::string fragmentCode = loadSource(fragmentPath);

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexCode.c_str());
    if (vertexShader == 0) {
        std::cerr << "[Shader] Failed to compile vertex shader from: " << vertexPath << "\n";
    }
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentCode.c_str());
    if (fragmentShader == 0) {
        std::cerr << "[Shader] Failed to compile fragment shader from: " << fragmentPath << "\n";
    }

    ID = glCreateProgram();
    glAttachShader(ID, vertexShader);
    glAttachShader(ID, fragmentShader);
    glLinkProgram(ID);

    GLint success;
    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(ID, 512, nullptr, infoLog);
        std::cerr << "[Shader] Program linking error: " << infoLog << "\n";
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

Shader::~Shader() {
    glDeleteProgram(ID);
}

void Shader::use() const {
    glUseProgram(ID);
}

std::string Shader::loadSource(const char* filePath) {
    // Backwards-compatible:
    // - If no #include exists, output is identical to the old behavior.
    // - If #include exists, we inline them.
    std::unordered_set<std::string> includedFiles;
    return expandIncludesRecursive(std::string(filePath), includedFiles, true);
}

GLuint Shader::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "[Shader] " << (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment")
                  << " shader compilation error: " << infoLog << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLint Shader::getUniformLocation(const std::string &name) const {
    auto it = uniformLocationCache.find(name);
    if (it != uniformLocationCache.end()) {
        return it->second;
    }

    GLint location = glGetUniformLocation(ID, name.c_str());
    if (location == -1)
        std::cerr << "[Shader] Warning: Uniform '" << name << "' not found!\n";

    uniformLocationCache[name] = location;
    return location;
}

void Shader::setUniform(const std::string &name, float value) const {
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setUniform(const std::string &name, const glm::mat4 &matrix) const {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
}

void Shader::setUniform(const std::string &name, const glm::vec3 &vec) const {
    glUniform3f(getUniformLocation(name), vec.x, vec.y, vec.z);
}

void Shader::setUniform(const std::string &name, int value) const {
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setUniform(const std::string &name, const glm::vec2 &vec) const {
    glUniform2f(getUniformLocation(name), vec.x, vec.y);
}
