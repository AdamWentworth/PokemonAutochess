// HealthBarRenderer.cpp

#include "HealthBarRenderer.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "../utils/ShaderLibrary.h"

void HealthBarRenderer::init() {
    // Initialize shader after OpenGL is ready
    shader = ShaderLibrary::get(
                "assets/shaders/ui/healthbar.vert",
                "assets/shaders/ui/healthbar.frag");
}

void HealthBarRenderer::render(const std::vector<HealthBarData>& healthBars) {
    if (!shader) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader->use();

    // Get viewport dimensions
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    float screenWidth = static_cast<float>(viewport[2]);
    float screenHeight = static_cast<float>(viewport[3]);
    glm::mat4 projection = glm::ortho(0.0f, screenWidth, screenHeight, 0.0f);
    shader->setUniform("u_Projection", projection);

    for (const auto& hb : healthBars) {
        const float width = 50.0f;
        const float hpH   = 5.0f;
        const float enH   = 4.0f;
        const float yOffset = 20.0f;      // top of HP bar
        const float gap     = 2.0f;       // space between HP and Energy bar

        glm::vec2 pos = hb.screenPosition;
        pos.x -= width/2.0f;
        pos.y -= yOffset;

        // Background
        glm::mat4 modelBg = glm::translate(glm::mat4(1.0f), glm::vec3(pos, 0.0f));
        modelBg = glm::scale(modelBg, glm::vec3(width, hpH, 1.0f));
        shader->setUniform("u_Model", modelBg);
        shader->setUniform("u_Color", glm::vec3(0.3f, 0.3f, 0.3f));
        renderQuad();

        // Foreground
        float percent = static_cast<float>(hb.currentHP)/hb.maxHP;
        glm::vec3 color;
        if (percent <= 0.2f) color = glm::vec3(1.0f, 0.0f, 0.0f);
        else if (percent <= 0.5f) color = glm::vec3(1.0f, 1.0f, 0.0f);
        else color = glm::vec3(0.0f, 1.0f, 0.0f);

        glm::mat4 modelFg = glm::translate(glm::mat4(1.0f), glm::vec3(pos, 0.0f));
        modelFg = glm::scale(modelFg, glm::vec3(width*percent, hpH, 1.0f));
        shader->setUniform("u_Model", modelFg);
        shader->setUniform("u_Color", color);
        renderQuad();

        // ----- Energy bar (blue) just below HP -----
        float eFrac = (hb.maxEnergy > 0) ? (static_cast<float>(hb.currentEnergy) / hb.maxEnergy) : 0.0f;
        glm::vec2 ePos = pos + glm::vec2(0.0f, hpH + gap);
    
        // Background (energy)
        glm::mat4 eBg = glm::translate(glm::mat4(1.0f), glm::vec3(ePos, 0.0f));
        eBg = glm::scale(eBg, glm::vec3(width, enH, 1.0f));
        shader->setUniform("u_Model", eBg);
        shader->setUniform("u_Color", glm::vec3(0.25f, 0.25f, 0.25f));
        renderQuad();
    
        // Foreground (energy)
        glm::mat4 eFg = glm::translate(glm::mat4(1.0f), glm::vec3(ePos, 0.0f));
        eFg = glm::scale(eFg, glm::vec3(width*eFrac, enH, 1.0f));
        shader->setUniform("u_Model", eFg);
        shader->setUniform("u_Color", glm::vec3(0.20f, 0.55f, 1.0f)); // blue
        renderQuad();
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void HealthBarRenderer::renderQuad() {
    static unsigned int VAO = 0, VBO;
    if (VAO == 0) {
        float vertices[] = {0.0f,0.0f, 1.0f,0.0f, 1.0f,1.0f, 0.0f,1.0f};
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}