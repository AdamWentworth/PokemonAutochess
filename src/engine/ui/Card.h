// Card.h

#pragma once

#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <string>

class Shader; // Forward declaration

class Card {
public:
    // Constructor takes the card rectangle and image path.
    Card(const SDL_Rect& rect, const std::string& imagePath);

    // Delete copy constructor and assignment operator to avoid accidental copies.
    Card(const Card&) = delete;
    Card& operator=(const Card&) = delete;

    // Define move constructor and move assignment operator.
    Card(Card&& other) noexcept;
    Card& operator=(Card&& other) noexcept;

    ~Card();

    // Draw the card using the provided UI shader.
    // The shader must define uniforms "u_Model" and "u_Texture".
    void draw(Shader* uiShader) const;

    // Check if a point (x, y) is inside the card.
    bool isPointInside(int x, int y) const;

    // Accessors.
    void setRect(const SDL_Rect& r) { rect = r; }
    SDL_Rect getRect() const { return rect; }
    void setImagePath(const std::string& path) { imagePath = path; }
    std::string getImagePath() const { return imagePath; }

private:
    SDL_Rect rect;
    std::string imagePath;
    unsigned int textureID;  // OpenGL texture handle.
    int imgWidth, imgHeight, imgChannels; // Image dimensions

    // Helper function to load the texture.
    unsigned int loadTexture(const std::string& path);
};
