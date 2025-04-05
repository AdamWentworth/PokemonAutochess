// Card.h

#pragma once

#include <SDL2/SDL.h>
#include <glm/glm.hpp>

class Card {
public:
    // Constructor takes a rectangle and a color.
    Card(const SDL_Rect& rect, const glm::vec3& color);
    ~Card();

    // Draw the card using a provided UI shader.
    // Assumes the shader has uniforms "u_Model" and "u_Color".
    void draw(class Shader* uiShader) const;

    // Check if a point (x, y) is inside the card.
    bool isPointInside(int x, int y) const;

    // Accessors.
    void setRect(const SDL_Rect& r) { rect = r; }
    SDL_Rect getRect() const { return rect; }
    void setColor(const glm::vec3& col) { color = col; }
    glm::vec3 getColor() const { return color; }

private:
    SDL_Rect rect;
    glm::vec3 color;
};
