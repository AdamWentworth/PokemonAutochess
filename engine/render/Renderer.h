// Renderer.h

#include <string>
#include "Camera3D.h"

#pragma once

class Renderer {
public:
    Renderer();
    ~Renderer();

    void shutdown();

private:
    unsigned int VAO, VBO;
    unsigned int shaderProgram;
    int mvpLocation;

    unsigned int compileShader(const char* source, unsigned int type);
    unsigned int createShaderProgram(const char* vertexPath, const char* fragmentPath);
    std::string loadShaderSource(const char* filePath);
};
