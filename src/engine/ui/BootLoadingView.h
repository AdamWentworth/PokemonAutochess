// BootLoadingView.h
#pragma once

#include <memory>
#include <glm/glm.hpp>

class Shader;

class BootLoadingView {
public:
    void init();
    void render(float progress01, int screenW, int screenH);

private:
    std::shared_ptr<Shader> shader;
    unsigned int vao = 0;
    unsigned int vbo = 0;

    void ensureQuad();
    void drawRect(float x, float y, float w, float h, const glm::vec3& rgb,
                  const glm::mat4& projection);
};
