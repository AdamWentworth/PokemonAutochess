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

    // Render text at screen-space (x, y) in pixels.
    // 'color' is RGB (1,1,1 = white).
    // 'scale' scales glyph quads relative to the font size (1.0 = natural size).
    // 'alpha' (NEW, default 1.0) multiplies glyph opacity for fading.
    void renderText(const std::string& text,
                    float x,
                    float y,
                    const glm::vec3& color,
                    float scale,
                    float alpha = 1.0f);

    TTF_Font* getFont() const { return font; }

    float measureTextWidth(const std::string& text, float scale = 1.0f) const;

private:
    TTF_Font* font = nullptr;
    Shader* textShader = nullptr;

    // Helper: Converts an SDL_Surface to an OpenGL texture.
    unsigned int createTextureFromSurface(SDL_Surface* surface);
};
