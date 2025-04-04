// Model.h

#pragma once

#pragma once

#include <string>
#include <vector>
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

    void loadGLTF(const std::string& filepath);
    unsigned int compileShader(const char* source, unsigned int type);
    unsigned int createShaderProgram(const char* vertSrc, const char* fragSrc);
};