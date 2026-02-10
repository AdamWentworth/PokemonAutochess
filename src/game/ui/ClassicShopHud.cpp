#include "game/ui/ClassicShopHud.h"
#include "game/ui/ShopLayout.h"

#include "engine/ui/TextRenderer.h"
#include "engine/ui/UIManager.h"
#include "engine/utils/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace game::ui {

ClassicShopHud::~ClassicShopHud() {
    shutdown();
}

void ClassicShopHud::init(const std::string& fontPath, int fontSize) {
    if (!text_) {
        text_ = std::make_unique<TextRenderer>(fontPath, fontSize);
    }
}

void ClassicShopHud::shutdown() {
    releaseResources();
    text_.reset();
}

void ClassicShopHud::ensureResources(const std::string& gameMode) {
    const std::string desiredIconPath = (gameMode == "adventure")
        ? "assets/images/pokedollar.png"
        : "assets/images/pokegold.png";
    if (desiredIconPath != iconPath_) {
        if (iconTexture_ != 0) {
            glDeleteTextures(1, &iconTexture_);
            iconTexture_ = 0;
        }
        iconPath_ = desiredIconPath;
        iconTexture_ = loadCurrencyTexture(iconPath_);
    }

    if (vao_ != 0) return;

    const float vertices[] = {
        // pos      // uv
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f
    };
    const unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void ClassicShopHud::releaseResources() {
    if (ebo_ != 0) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (iconTexture_ != 0) {
        glDeleteTextures(1, &iconTexture_);
        iconTexture_ = 0;
    }
    iconPath_.clear();
}

unsigned int ClassicShopHud::loadCurrencyTexture(const std::string& path) const {
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "[ClassicShopHud] Failed to load currency icon: " << path << "\n";
        return 0;
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
}

ClassicShopHudOutput ClassicShopHud::draw(const ClassicShopHudInput& in) {
    ClassicShopHudOutput out;
    if (!text_ || in.shopCardsH <= 0 || in.uiW <= 0 || in.uiH <= 0) return out;

    ensureResources(in.gameMode);

    const std::string moneyText = std::to_string(std::max(0, in.money));
    const float moneyH = text_->measureTextHeight(moneyText, in.moneyScale);
    const float moneyW = text_->measureTextWidth(moneyText, in.moneyScale);
    const float rerollH = text_->measureTextHeight(in.rerollLabel, in.rerollScale);
    const float rerollW = in.showReroll ? text_->measureTextWidth(in.rerollLabel, in.rerollScale) : 0.0f;

    const float iconSize = (in.gameMode == "adventure") ? 34.0f : 30.0f;
    const bool iconVisible = (iconTexture_ != 0);

    ClassicHudLayoutInput layoutIn;
    layoutIn.uiW = in.uiW;
    layoutIn.uiH = in.uiH;
    layoutIn.shopCardsX = in.shopCardsX;
    layoutIn.shopCardsY = in.shopCardsY;
    layoutIn.shopCardsH = in.shopCardsH;
    layoutIn.moneyTextW = moneyW;
    layoutIn.moneyTextH = moneyH;
    layoutIn.rerollTextW = rerollW;
    layoutIn.rerollTextH = rerollH;
    layoutIn.showReroll = in.showReroll;
    layoutIn.iconVisible = iconVisible;
    layoutIn.iconSize = iconSize;

    const ClassicHudLayout layout = computeClassicHudLayout(layoutIn);

    if (iconTexture_ != 0 && vao_ != 0) {
        Shader* shader = UIManager::getCardShader();
        if (shader) {
            const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
            if (depthWasEnabled) glDisable(GL_DEPTH_TEST);
            const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
            if (!blendWasEnabled) glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            const glm::mat4 projection = glm::ortho(
                0.0f, static_cast<float>(in.uiW),
                static_cast<float>(in.uiH), 0.0f);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(layout.iconX, layout.iconY, 0.0f));
            model = glm::scale(model, glm::vec3(layout.iconSize, layout.iconSize, 1.0f));

            shader->use();
            shader->setUniform("u_Projection", projection);
            shader->setUniform("u_Model", model);
            shader->setUniform("u_UVMin", glm::vec2(0.0f, 0.0f));
            shader->setUniform("u_UVMax", glm::vec2(1.0f, 1.0f));
            shader->setUniform("u_Texture", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, iconTexture_);
            glBindVertexArray(vao_);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);

            if (!blendWasEnabled) glDisable(GL_BLEND);
            if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
        }
    }

    const glm::vec3 goldColor(1.0f, 0.92f, 0.10f);
    text_->renderText(moneyText, layout.textX, layout.textY, goldColor, in.moneyScale);
    text_->renderText(moneyText, layout.textX + 0.75f, layout.textY, goldColor, in.moneyScale);
    text_->renderText(moneyText, layout.textX, layout.textY + 0.75f, goldColor, in.moneyScale);

    if (in.showReroll) {
        out.rerollX = layout.rerollX;
        out.rerollY = layout.rerollY;
        out.rerollW = layout.rerollW;
        out.rerollH = layout.rerollH;
        text_->renderText(in.rerollLabel, out.rerollX, out.rerollY, glm::vec3(1.0f), in.rerollScale);
    }

    return out;
}

} // namespace game::ui
