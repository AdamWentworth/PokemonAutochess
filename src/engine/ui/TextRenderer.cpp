// TextRenderer.cpp

#include "TextRenderer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../utils/Shader.h"
#include <cmath>

// Constructor: load the font and the text shader.
TextRenderer::TextRenderer(const std::string& fontPath, int fontSize) {
    font = TTF_OpenFont(fontPath.c_str(), fontSize);
    if (!font) {
        std::cerr << "[TextRenderer] Failed to load font: " << TTF_GetError() << "\n";
    }
    // Load the text shader (create assets/shaders/ui/text.vert and text.frag as described below)
    textShader = new Shader("assets/shaders/ui/text.vert", "assets/shaders/ui/text.frag");
}

TextRenderer::~TextRenderer() {
    if (font) {
        TTF_CloseFont(font);
    }
    if (textShader) {
        delete textShader;
    }
}

// Helper: Convert an SDL_Surface to an OpenGL texture using nearest filtering.
unsigned int TextRenderer::createTextureFromSurface(SDL_Surface* surface) {
    // Convert surface to 32-bit RGBA format
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (!converted) {
        std::cerr << "Failed to convert surface: " << SDL_GetError() << "\n";
        return 0;
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,                      // Internal format
                 converted->w,
                 converted->h,
                 0,
                 GL_RGBA,                      // Format matches converted surface
                 GL_UNSIGNED_BYTE,
                 converted->pixels);

    glBindTexture(GL_TEXTURE_2D, 0);
    SDL_FreeSurface(converted);

    return textureID;
}

void TextRenderer::renderText(const std::string& text, float x, float y, const glm::vec3& color, float scale) {
    if (!font) return;

    float posX = x;

    int windowWidth = 1280;
    int windowHeight = 720;

    glm::mat4 projection = glm::ortho(0.0f, (float)windowWidth, (float)windowHeight, 0.0f);
    textShader->use();
    textShader->setUniform("u_Projection", projection);
    textShader->setUniform("u_TextColor", color);

    for (char c : text) {
        if (!TTF_GlyphIsProvided(font, c)) continue;

        SDL_Color sdlColor = {255, 255, 255, 255};
        SDL_Surface* glyphSurface = TTF_RenderGlyph_Blended(font, c, sdlColor);
        if (!glyphSurface) continue;

        unsigned int textureID = createTextureFromSurface(glyphSurface);
        int w = glyphSurface->w;
        int h = glyphSurface->h;
        SDL_FreeSurface(glyphSurface);

        // Define screen-space quad from posX/y to (posX + w, y + h)
        float xpos = posX;
        float ypos = y;

        float quadW = w * scale;
        float quadH = h * scale;

        float vertices[] = {
            // pos         // tex
            xpos,     ypos + quadH, 0.0f, 1.0f,
            xpos,     ypos,         0.0f, 0.0f,
            xpos + quadW, ypos,     1.0f, 0.0f,
            xpos + quadW, ypos + quadH, 1.0f, 1.0f
        };

        unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

        GLuint VAO, VBO, EBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glUniform1i(glGetUniformLocation(textShader->getID(), "u_Texture"), 0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glDisable(GL_BLEND);

        glBindVertexArray(0);
        glDeleteBuffers(1, &VBO);
        glDeleteBuffers(1, &EBO);
        glDeleteVertexArrays(1, &VAO);
        glDeleteTextures(1, &textureID);

        // Advance using font metrics
        int minx, maxx, miny, maxy, advance;
        if (TTF_GlyphMetrics(font, c, &minx, &maxx, &miny, &maxy, &advance) == 0)
            posX += static_cast<float>(advance) * scale;
        else
            posX += w * scale;
    }
}
