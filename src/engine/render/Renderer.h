// Renderer.h

#pragma once
#include <string>
#include "Camera3D.h"
#include "../utils/Shader.h" // Ensure this is the complete definition of Shader
#include <memory>  // For std::unique_ptr

class Renderer {
public:
    Renderer();
    ~Renderer();

    void shutdown();
    void render();

private:
    unsigned int VAO, VBO;
    std::unique_ptr<Shader> shader;  // Uses unique_ptr for automatic cleanup
    int mvpLocation;
};

