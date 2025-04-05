// Renderer.h

#pragma once
#include <string>
#include "Camera3D.h"
#include "../utils/Shader.h" // Include our custom Shader class

class Renderer {
public:
    Renderer();
    ~Renderer();

    void shutdown();
    void render();

private:
    unsigned int VAO, VBO;
    Shader* shader = nullptr;  // Use our Shader class
    int mvpLocation;
};

