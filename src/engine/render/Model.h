// Model.h

#pragma once

#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include "Camera3D.h"
#include "../utils/Shader.h"

struct Submesh {
    size_t indexOffset; // Where in the index buffer this submesh starts
    size_t indexCount;  // How many indices this submesh has
    unsigned int textureID; // Texture for this submesh
};

class Model {
public:
    Model(const std::string& filepath);
    ~Model();

    // Existing drawing method for static placement.
    void draw(const Camera3D& camera);

    // Setter for dynamic grid placement:
    void setModelPosition(const glm::vec3& pos);
    glm::vec3 getModelPosition() const { return modelPosition; }

    // New getters needed for per-instance drawing.
    float getScaleFactor() const { return modelScaleFactor; }
    int getMVPLocation() const { return mvpLocation; }

    // New method: Draw the model using an externally computed MVP.
    void drawInstance();
    void drawInstanceWithMVP(const glm::mat4& mvp);

private:
    unsigned int VAO, VBO, EBO;
    // Removed shaderProgram; we use our Shader object now.
    int mvpLocation;
    unsigned int textureID;
    size_t indexCount;

    std::vector<Submesh> submeshes; // One submesh per primitive

    // Members for scaling relative to board cell size:
    float modelScaleFactor;  // Computed from the bounding box.
    glm::vec3 modelOffset;   // Offset to bring the model's base to (0,0,0).

    // Member for dynamic grid placement:
    glm::vec3 modelPosition = glm::vec3(0.0f);

    void loadGLTF(const std::string& filepath);
    unsigned int compileShader(const char* source, unsigned int type);
    unsigned int createShaderProgram(const char* vertSrc, const char* fragSrc);

    // NEW: Pointer to our Shader object for the model.
    Shader* modelShader = nullptr;
};