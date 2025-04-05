// Model.cpp

#include "Model.h"
#include <glad/glad.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <limits>
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_JSON
#include "../../../third_party/nlohmann/json.hpp"
#include "../../../third_party/tinygltf/tiny_gltf.h"

static std::string loadShaderSource(const char* filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[ShaderUtils] Failed to open shader file: " << filePath << "\n";
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

Model::Model(const std::string& filepath) {
    loadGLTF(filepath);

    // Load shader source from assets.
    std::string vertSource = loadShaderSource("assets/shaders/model/default.vert");
    std::string fragSource = loadShaderSource("assets/shaders/model/default.frag");

    shaderProgram = createShaderProgram(vertSource.c_str(), fragSource.c_str());
    mvpLocation = glGetUniformLocation(shaderProgram, "u_MVP");
}

Model::~Model() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteTextures(1, &textureID);
    glDeleteProgram(shaderProgram);
}

void Model::loadGLTF(const std::string& filepath) {
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filepath); // For .glb
    if (!warn.empty()) std::cout << "[GLTF] Warning: " << warn << "\n";
    if (!err.empty()) std::cerr << "[GLTF] Error: " << err << "\n";
    if (!ret) {
        std::cerr << "[GLTF] Failed to load: " << filepath << "\n";
        return;
    }

    std::vector<float> vertexData;
    std::vector<unsigned short> indexData;
    size_t totalVertices = 0; // For offsetting indices

    // Iterate over all meshes and primitives.
    for (const auto &mesh : gltfModel.meshes) {
        for (const auto &primitive : mesh.primitives) {
            // Check required attributes.
            if (primitive.attributes.find("POSITION") == primitive.attributes.end()) {
                std::cerr << "[GLTF] Missing POSITION attribute in a primitive.\n";
                continue;
            }
            if (primitive.attributes.find("TEXCOORD_0") == primitive.attributes.end()) {
                std::cerr << "[GLTF] Missing TEXCOORD_0 attribute in a primitive.\n";
                continue;
            }

            const auto &posAccessor = gltfModel.accessors[primitive.attributes.at("POSITION")];
            const auto &texAccessor = gltfModel.accessors[primitive.attributes.at("TEXCOORD_0")];
            const auto &indexAccessor = gltfModel.accessors[primitive.indices];

            const auto &posView = gltfModel.bufferViews[posAccessor.bufferView];
            const auto &texView = gltfModel.bufferViews[texAccessor.bufferView];
            const auto &idxView = gltfModel.bufferViews[indexAccessor.bufferView];
            const auto &buffer = gltfModel.buffers[0];

            size_t vertexCount = posAccessor.count;
            const float* posData = reinterpret_cast<const float*>(&buffer.data[posView.byteOffset]);
            const float* texData = reinterpret_cast<const float*>(&buffer.data[texView.byteOffset]);

            // Record where this primitive's indices will start.
            size_t submeshIndexOffset = indexData.size();

            // Append vertex data (3 floats for POSITION, 2 for TEXCOORD).
            for (size_t i = 0; i < vertexCount; ++i) {
                vertexData.push_back(posData[i * 3 + 0]);
                vertexData.push_back(posData[i * 3 + 1]);
                vertexData.push_back(posData[i * 3 + 2]);
                vertexData.push_back(texData[i * 2 + 0]);
                vertexData.push_back(texData[i * 2 + 1]);
            }

            // Append indices, offsetting by the current total vertex count.
            size_t primIndexCount = indexAccessor.count;
            const unsigned short* primIndices = reinterpret_cast<const unsigned short*>(&buffer.data[idxView.byteOffset]);
            for (size_t i = 0; i < primIndexCount; ++i) {
                indexData.push_back(primIndices[i] + static_cast<unsigned short>(totalVertices));
            }

            // Update total vertex count.
            totalVertices += vertexCount;

            // Load the texture for this primitive.
            unsigned int primTextureID = 0;
            if (primitive.material >= 0 && primitive.material < gltfModel.materials.size()) {
                const auto &material = gltfModel.materials[primitive.material];
                if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                    int textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
                    if (textureIndex >= 0 && textureIndex < gltfModel.textures.size()) {
                        int imageIndex = gltfModel.textures[textureIndex].source;
                        if (imageIndex >= 0 && imageIndex < gltfModel.images.size()) {
                            const auto &image = gltfModel.images[imageIndex];
                            glGenTextures(1, &primTextureID);
                            glBindTexture(GL_TEXTURE_2D, primTextureID);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
                                         image.component == 4 ? GL_RGBA : GL_RGB,
                                         GL_UNSIGNED_BYTE, image.image.data());
                            glGenerateMipmap(GL_TEXTURE_2D);
                        }
                    }
                }
            }
            // If no texture was loaded, you might want to create a default white texture.
            if (primTextureID == 0) {
                unsigned char whitePixel[4] = { 255, 255, 255, 255 };
                glGenTextures(1, &primTextureID);
                glBindTexture(GL_TEXTURE_2D, primTextureID);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
            }

            // Store submesh info.
            Submesh sm;
            sm.indexOffset = submeshIndexOffset;
            sm.indexCount = primIndexCount;
            sm.textureID = primTextureID;
            submeshes.push_back(sm);
        }
    }

    // After merging all vertex data:
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();

    size_t numVertices = vertexData.size() / 5;
    for (size_t i = 0; i < numVertices; i++) {
        float x = vertexData[i * 5 + 0];
        float y = vertexData[i * 5 + 1];
        float z = vertexData[i * 5 + 2];
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (z < minZ) minZ = z;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
        if (z > maxZ) maxZ = z;
    }

    // Compute the model's height (you can choose height, width, or depth; here we use height).
    float modelHeight = maxY - minY;

    // Assume the board cell is 1.0 unit. If you want the model's height to be, say, 80% of a cell:
    float desiredHeight = 0.8f;
    modelScaleFactor = desiredHeight / (maxZ - minZ);

    // To center the model so that its base (lowest Y value) sits at Y=0,
    // compute the center in X and Z, and translate so that (centerX, minY, centerZ) goes to (0,0,0).
    float centerX = (minX + maxX) / 2.0f;
    float centerY = (minY + maxY) / 2.0f;
    modelOffset = glm::vec3(
        -modelScaleFactor * centerX,    // shift in x
         modelScaleFactor * maxZ,         // shift in y to bring base (from original maxZ) to y=0
        -modelScaleFactor * centerY     // shift in z
    );

    std::cout << "[Model] Total Vertex Count: " << totalVertices << "\n";
    std::cout << "[Model] Total Index Count: " << indexData.size() << "\n";

    // Generate OpenGL buffers.
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexData.size() * sizeof(unsigned short), indexData.data(), GL_STATIC_DRAW);

    // Define vertex layout: 3 floats for position, 2 for texture coordinates.
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

void Model::drawInstance() {
    // Bind the VAO and draw each submesh.
    glBindVertexArray(VAO);
    for (const auto &sm : submeshes) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sm.textureID);
        glDrawElements(GL_TRIANGLES, (GLsizei)sm.indexCount, GL_UNSIGNED_SHORT, (void*)(sm.indexOffset * sizeof(unsigned short)));
    }
}

void Model::setModelPosition(const glm::vec3& pos) {
    modelPosition = pos;
}

unsigned int Model::compileShader(const char* source, unsigned int type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

unsigned int Model::createShaderProgram(const char* vertSrc, const char* fragSrc) {
    unsigned int vert = compileShader(vertSrc, GL_VERTEX_SHADER);
    unsigned int frag = compileShader(fragSrc, GL_FRAGMENT_SHADER);
    unsigned int program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}
