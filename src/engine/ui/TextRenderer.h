// TextRenderer.h

#pragma once
#include <string>
#include <glm/glm.hpp>
#include <SDL2/SDL_ttf.h>

// Forward declare our Shader class.
class Shader;

class TextRenderer {
public:
    // Constructor loads the font from a file with a given size.
    TextRenderer(const std::string& fontPath, int fontSize);
    ~TextRenderer();

    // Render the given text glyph-by-glyph at position (x, y).
    // Uses TTF metrics for spacing. 'color' is a glm::vec3 (1,1,1 = white).
    // 'scale' should generally be 1.0 to render at natural font size (defined in constructor).
    void renderText(const std::string& text, float x, float y, const glm::vec3& color, float scale);
    
    TTF_Font* getFont() const { return font; }

    float measureTextWidth(const std::string& text, float scale = 1.0f) const;

private:
    TTF_Font* font;
    Shader* textShader;

    // Helper: Converts an SDL_Surface to an OpenGL texture.
    unsigned int createTextureFromSurface(SDL_Surface* surface);
};
