// Card.cpp

#include "Card.h"
#include "../utils/Shader.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stb_image.h>  // Do NOT define STB_IMAGE_IMPLEMENTATION here!

// Constructor: loads texture.
Card::Card(const SDL_Rect& rect, const std::string& imagePath)
    : rect(rect), imagePath(imagePath), textureID(0), imgWidth(0), imgHeight(0), imgChannels(0)
{
    // Disable flipping to display the image as is.
    stbi_set_flip_vertically_on_load(false);
    textureID = loadTexture(imagePath);
    if (textureID == 0) {
        std::cerr << "[Card] Failed to load texture: " << imagePath << "\n";
    }
}

// Move constructor: transfer ownership of textureID.
Card::Card(Card&& other) noexcept 
    : rect(other.rect), imagePath(std::move(other.imagePath)),
      textureID(other.textureID), imgWidth(other.imgWidth), imgHeight(other.imgHeight), imgChannels(other.imgChannels)
{
    other.textureID = 0;  // Prevent double deletion.
}

// Move assignment operator.
Card& Card::operator=(Card&& other) noexcept {
    if (this != &other) {
        // Delete our existing texture.
        if (textureID != 0) {
            glDeleteTextures(1, &textureID);
        }
        rect = other.rect;
        imagePath = std::move(other.imagePath);
        textureID = other.textureID;
        imgWidth = other.imgWidth;
        imgHeight = other.imgHeight;
        imgChannels = other.imgChannels;
        other.textureID = 0;  // Prevent double deletion.
    }
    return *this;
}

// Destructor: cleanup the texture.
Card::~Card() {
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
    }
}

unsigned int Card::loadTexture(const std::string& path) {
    unsigned int texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    // Set texture wrapping/filtering options.
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

    // Compute scale to preserve aspect ratio.
    float cardW = static_cast<float>(rect.w);
    float cardH = static_cast<float>(rect.h);
    float cardAspect = cardW / cardH;
    float imageAspect = static_cast<float>(imgWidth) / static_cast<float>(imgHeight);
    
    float scale = 1.0f;
    if (imageAspect > cardAspect) {
        // Fit to card width.
        scale = cardW / static_cast<float>(imgWidth);
    } else {
        // Fit to card height.
        scale = cardH / static_cast<float>(imgHeight);
    }
    
    // Compute drawn image size.
    float drawnW = static_cast<float>(imgWidth) * scale;
    float drawnH = static_cast<float>(imgHeight) * scale;
    
    // Compute offsets to center image within card.
    float offsetX = (cardW - drawnW) * 0.5f;
    float offsetY = (cardH - drawnH) * 0.5f;
    
    // Build a model matrix:
    // 1. Scale from normalized (0,1) to drawn image size.
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(drawnW, drawnH, 1.0f));
    // 2. Translate so that the image is centered within the card.
    glm::mat4 translateMat = glm::translate(glm::mat4(1.0f), glm::vec3(offsetX, offsetY, 0.0f));
    // 3. Translate the card to its screen position.
    glm::mat4 cardTranslate = glm::translate(glm::mat4(1.0f), glm::vec3(rect.x, rect.y, 0.0f));
    glm::mat4 model = cardTranslate * translateMat * scaleMat;

    GLint modelLoc = glGetUniformLocation(uiShader->getID(), "u_Model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    // Bind the texture.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    GLint texLoc = glGetUniformLocation(uiShader->getID(), "u_Texture");
    glUniform1i(texLoc, 0);

    // Define geometry in normalized card space (0,0) to (1,1).
    float vertices[] = {
        // Positions      // Tex Coords
         0.0f, 0.0f,      0.0f, 0.0f,
         1.0f, 0.0f,      1.0f, 0.0f,
         1.0f, 1.0f,      1.0f, 1.0f,
         0.0f, 1.0f,      0.0f, 1.0f
    };
    unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

    GLuint VAO_local, VBO_local, EBO_local;
    glGenVertexArrays(1, &VAO_local);
    glGenBuffers(1, &VBO_local);
    glGenBuffers(1, &EBO_local);

    glBindVertexArray(VAO_local);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_local);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_local);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    // Clean up.
    glBindVertexArray(0);
    glDeleteBuffers(1, &VBO_local);
    glDeleteBuffers(1, &EBO_local);
    glDeleteVertexArrays(1, &VAO_local);

    // Unbind texture.
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Card::isPointInside(int x, int y) const {
    return (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h);
}
