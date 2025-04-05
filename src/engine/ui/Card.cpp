// Card.cpp

#include "Card.h"
#include "../utils/Shader.h" // Make sure your Shader class is in an accessible location
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

Card::Card(const SDL_Rect& rect, const glm::vec3& color)
    : rect(rect), color(color)
{
}

Card::~Card() {
    // No persistent GPU objects; we create and delete VAO/VBO in draw().
}

void Card::draw(Shader* uiShader) const {
    // Activate the shader.
    uiShader->use();

    // Set up a model matrix; for now we use identity as the card is defined in screen space.
    glm::mat4 model = glm::mat4(1.0f);
    GLint modelLoc = glGetUniformLocation(uiShader->getID(), "u_Model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    // Set the color uniform.
    GLint colorLoc = glGetUniformLocation(uiShader->getID(), "u_Color");
    glUniform3fv(colorLoc, 1, glm::value_ptr(color));

    // Define the rectangle vertices.
    float x = static_cast<float>(rect.x);
    float y = static_cast<float>(rect.y);
    float w = static_cast<float>(rect.w);
    float h = static_cast<float>(rect.h);
    float vertices[] = {
         x,   y,
         x+w, y,
         x+w, y+h,
         x,   y+h
    };
    unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

    // Create temporary VAO/VBO/EBO for this card.
    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Set up vertex attributes (we assume 2D positions).
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Draw the card.
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    // Clean up temporary objects.
    glBindVertexArray(0);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteVertexArrays(1, &VAO);
}

bool Card::isPointInside(int x, int y) const {
    return (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h);
}
