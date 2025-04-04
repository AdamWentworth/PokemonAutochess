// Renderer.cpp

#include "Renderer.h"
#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <iostream>

// Utility: check for OpenGL errors and log them.
void checkGLError(const std::string& context) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "[OpenGL Error] " << context << ": " << err << "\n";
    }
}

Renderer::Renderer() {
    std::cout << "[Renderer] Constructing Renderer...\n";
    float vertices[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
    };

    std::cout << "[Renderer] Creating shader program...\n";
    shaderProgram = createShaderProgram("engine/render/shader.vert", "engine/render/shader.frag");
    std::cout << "[Renderer] Shader program created with ID: " << shaderProgram << "\n";
    checkGLError("After shader program creation");

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    checkGLError("After generating VAO and VBO");

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    checkGLError("After setting up vertex attributes");
}

Renderer::~Renderer() {
}

void Renderer::shutdown() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
}

std::string Renderer::loadShaderSource(const char* filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Renderer] Failed to open shader: " << filePath << "\n";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string src = ss.str();
    std::cout << "[Renderer] Loaded shader (" << filePath << ") with " << src.size() << " characters.\n";
    return src;
}

unsigned int Renderer::compileShader(const char* source, unsigned int type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    char log[512];
    glGetShaderInfoLog(shader, 512, nullptr, log);
    if (log[0] != '\0') {
        std::cout << "[Renderer] Shader Compile Log:\n" << log << std::endl;
    }
    checkGLError("After shader compilation");
    return shader;
}

unsigned int Renderer::createShaderProgram(const char* vertexPath, const char* fragmentPath) {
    std::string vertSrc = loadShaderSource(vertexPath);
    std::string fragSrc = loadShaderSource(fragmentPath);

    unsigned int vertexShader = compileShader(vertSrc.c_str(), GL_VERTEX_SHADER);
    unsigned int fragmentShader = compileShader(fragSrc.c_str(), GL_FRAGMENT_SHADER);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    // Check link status
    int linkSuccess;
    glGetProgramiv(program, GL_LINK_STATUS, &linkSuccess);
    if (!linkSuccess) {
        char linkLog[512];
        glGetProgramInfoLog(program, 512, nullptr, linkLog);
        std::cerr << "[Renderer] Shader Program Linking Error:\n" << linkLog << "\n";
        exit(EXIT_FAILURE);
    } else {
        std::cout << "[Renderer] Shader program linked successfully.\n";
    }
    checkGLError("After linking shader program");

    mvpLocation = glGetUniformLocation(program, "u_MVP");
    if (mvpLocation == -1) {
        std::cerr << "[Renderer] ERROR: u_MVP uniform not found in shader.\n";
    }
    std::cout << "[Renderer] u_MVP location = " << mvpLocation << "\n";

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}