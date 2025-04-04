// Model.h

#pragma once

#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include "Camera3D.h"

struct Submesh {
    size_t indexOffset; // Where in the index buffer this submesh starts
    size_t indexCount;  // How many indices this submesh has
    unsigned int textureID; // Texture for this submesh
};

class Model {
public:
    Model(const std::string& filepath);
    ~Model();

    void draw(const Camera3D& camera);

private:
    unsigned int VAO, VBO, EBO;
    unsigned int shaderProgram;
    unsigned int textureID;
    int mvpLocation;
    size_t indexCount;

    std::vector<Submesh> submeshes; // One submesh per primitive

    // New members for scaling relative to board cell size:
    float modelScaleFactor;  // Uniform scale factor computed from the bounding box.
    glm::vec3 modelOffset;   // Translation offset to bring the model's base to (0,0,0).

    void loadGLTF(const std::string& filepath);
    unsigned int compileShader(const char* source, unsigned int type);
    unsigned int createShaderProgram(const char* vertSrc, const char* fragSrc);
};