// Card.cpp

#include "Card.h"
#include "../utils/Shader.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stb_image.h>

// Static members for the card frame texture remain unchanged.
std::string Card::framePath = "assets/ui/frame_gold.png";
unsigned int Card::frameTextureID = 0;
bool Card::frameLoaded = false;

// -----------------------------------------------------------------------------
// NEW: Static buffers for Card drawing
// -----------------------------------------------------------------------------
namespace {
    GLuint cardVAO = 0, cardVBO = 0, cardEBO = 0;
    bool cardBuffersInitialized = false;

    void initCardBuffers() {
        if (!cardBuffersInitialized) {
            float vertices[] = {
                0.0f, 0.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 1.0f, 0.0f,
                1.0f, 1.0f, 1.0f, 1.0f,
                0.0f, 1.0f, 0.0f, 1.0f
            };
            unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

            glGenVertexArrays(1, &cardVAO);
            glGenBuffers(1, &cardVBO);
            glGenBuffers(1, &cardEBO);

            glBindVertexArray(cardVAO);
            glBindBuffer(GL_ARRAY_BUFFER, cardVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cardEBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

            // Set up vertex attributes.
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(1);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            cardBuffersInitialized = true;
        }
    }
}

Card::Card(const SDL_Rect& rect, const std::string& imagePath)
    : rect(rect), imagePath(imagePath), textureID(0), imgWidth(0), imgHeight(0), imgChannels(0)
{
    stbi_set_flip_vertically_on_load(false);
    textureID = loadTexture(imagePath);
    if (textureID == 0) {
        std::cerr << "[Card] Failed to load texture: " << imagePath << "\n";
    }

    if (!frameLoaded) {
        loadFrameTexture();
    }
}

Card::Card(Card&& other) noexcept
    : rect(other.rect), 
      imagePath(std::move(other.imagePath)),
      textureID(other.textureID), 
      imgWidth(other.imgWidth),
      imgHeight(other.imgHeight), 
      imgChannels(other.imgChannels),
      cardData(std::move(other.cardData))
{
    other.textureID = 0;
}

Card& Card::operator=(Card&& other) noexcept {
    if (this != &other) {
        if (textureID != 0) glDeleteTextures(1, &textureID);
        rect = other.rect;
        imagePath = std::move(other.imagePath);
        textureID = other.textureID;
        imgWidth = other.imgWidth;
        imgHeight = other.imgHeight;
        imgChannels = other.imgChannels;
        cardData = std::move(other.cardData);
        other.textureID = 0;
    }
    return *this;
}

Card::~Card() {
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
    }
}

unsigned int Card::loadTexture(const std::string& path) {
    unsigned int texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned char* data = stbi_load(path.c_str(), &imgWidth, &imgHeight, &imgChannels, 0);
    if (data) {
        GLenum format = (imgChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, imgWidth, imgHeight, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        std::cerr << "[Card] Failed to load image: " << path << "\n";
        texID = 0;
    }
    stbi_image_free(data);
    return texID;
}

void Card::draw(Shader* uiShader) const {
    uiShader->use();

    // Ensure our static buffers are initialized.
    initCardBuffers();

    // 🔥 Enable transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 🔶 Draw Pokémon image first (slightly smaller to fit inside the frame)
    const float padding = 6.0f;
    float imgW = rect.w - 2 * padding;
    float imgH = rect.h - 2 * padding;

    glm::mat4 imgModel = glm::translate(glm::mat4(1.0f), glm::vec3(rect.x + padding, rect.y + padding, 0.0f));
    imgModel = glm::scale(imgModel, glm::vec3(imgW, imgH, 1.0f));

    glUniformMatrix4fv(glGetUniformLocation(uiShader->getID(), "u_Model"), 1, GL_FALSE, glm::value_ptr(imgModel));
    glBindTexture(GL_TEXTURE_2D, textureID);
    glUniform1i(glGetUniformLocation(uiShader->getID(), "u_Texture"), 0);
    
    // Use our shared VAO for drawing.
    glBindVertexArray(cardVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    // 🟡 Draw the frame second (overlaid on top)
    glm::mat4 frameModel = glm::translate(glm::mat4(1.0f), glm::vec3(rect.x, rect.y, 0.0f));
    frameModel = glm::scale(frameModel, glm::vec3(rect.w, rect.h, 1.0f));

    glUniformMatrix4fv(glGetUniformLocation(uiShader->getID(), "u_Model"), 1, GL_FALSE, glm::value_ptr(frameModel));
    glBindTexture(GL_TEXTURE_2D, frameTextureID);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glDisable(GL_BLEND);

    // Unbind the VAO.
    glBindVertexArray(0);
}

bool Card::isPointInside(int x, int y) const {
    return (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h);
}

void Card::loadFrameTexture() {
    int w, h, c;
    unsigned char* data = stbi_load(framePath.c_str(), &w, &h, &c, 0);
    if (data) {
        glGenTextures(1, &frameTextureID);
        glBindTexture(GL_TEXTURE_2D, frameTextureID);
        GLenum format = (c == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        frameLoaded = true;
        stbi_image_free(data);
    } else {
        std::cerr << "[Card] Failed to load frame texture: " << framePath << "\n";
    }
}

void Card::setGlobalFramePath(const std::string& path) {
    framePath = path;
}
