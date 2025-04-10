// Shader.cpp

#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

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
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Shader] Error opening shader file: " << filePath << "\n";
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
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
    // Check if already cached:
    auto it = uniformLocationCache.find(name);
    if (it != uniformLocationCache.end()) {
        return it->second;
    }
    // Not found? Get and cache it.
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