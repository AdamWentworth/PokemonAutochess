// BoardRenderer.h

#pragma once

#include "Camera3D.h"
#include <vector>
#include <string>

class BoardRenderer {
public:
    BoardRenderer(int rows = 8, int cols = 8, float cellSize = 1.0f);
    ~BoardRenderer();

    void draw(const Camera3D& camera);
    void shutdown();

private:
    void initGrid();
    std::string loadShaderSource(const char* path);
    unsigned int compileShader(const char* src, unsigned int type);
    unsigned int createShaderProgram(const char* vertPath, const char* fragPath);

    unsigned int vao, vbo;
    unsigned int shaderProgram;
    int mvpLocation;

    int rows, cols;
    float cellSize;
    std::vector<float> gridVertices;
};
