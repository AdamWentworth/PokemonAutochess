// CardSystem.cpp

#include "CardSystem.h"
#include "engine/ui/UIManager.h"
#include "engine/utils/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

CardSystem::CardSystem() {}

CardSystem::~CardSystem() {}

void CardSystem::init() {
    UIManager::init();
    cardShader = UIManager::getCardShader();
}

void CardSystem::initOverlayText(const std::string& fontPath, int fontSize) {
    overlayText = std::make_unique<TextRenderer>(fontPath, fontSize);
    overlayScale = 1.0f;
}

void CardSystem::addCard(Card&& card) {
    cards.push_back(std::move(card));
}

void CardSystem::update(float deltaTime) {
    (void)deltaTime;
}

void CardSystem::render(int screenWidth, int screenHeight) {
    if (!cardShader) return;

    // --- Ensure UI renders on top of the 3D scene ---
    // We temporarily disable depth testing for 2D overlay draw.
    GLboolean wasDepthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (wasDepthTestEnabled) glDisable(GL_DEPTH_TEST);

    glm::mat4 ortho = glm::ortho(
        0.0f, static_cast<float>(screenWidth),
        static_cast<float>(screenHeight), 0.0f
    );

    cardShader->use();
    GLint projLoc = glGetUniformLocation(cardShader->getID(), "u_Projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(ortho));

    for (const auto& card : cards) {
        card.draw(cardShader);
    }

    glUseProgram(0);

    // Restore previous depth state
    if (wasDepthTestEnabled) glEnable(GL_DEPTH_TEST);

    if (overlayText) {
        for (const auto& card : cards) {
            const CardData& d = card.getData();
            const ui::Rect rect = card.getRect();
            const float pad = 6.0f;
            if (d.level > 0) {
                const std::string lvl = "Lv" + std::to_string(d.level);
                overlayText->renderText(lvl, rect.x + pad, rect.y + pad, glm::vec3(1.0f, 1.0f, 1.0f), overlayScale);
            } else if (!d.label.empty()) {
                overlayText->renderText(d.label, rect.x + pad, rect.y + pad, glm::vec3(1.0f, 1.0f, 1.0f), overlayScale);
            }
        }
    }
}

std::optional<CardData> CardSystem::handleMouseClick(int mouseX, int mouseY) {
    for (auto& card : cards) {
        if (card.isPointInside(mouseX, mouseY)) {
            const auto& data = card.getData();
            std::cout << "[Card Clicked] Pokemon: " << data.pokemonName
                      << " | Cost: " << data.cost
                      << " | Type: " << static_cast<int>(data.type)
                      << "\n";
            return data;
        }
    }
    return std::nullopt;
}

void CardSystem::clearCards() {
    cards.clear();
}

// 🔄 Match the stable build’s look: 220×150 cards, 50px spacing, centered row
void CardSystem::spawnCardRow(const std::vector<CardData>& cardDatas, int screenWidth, int yOffset) {
    spawnCardRowLayout(cardDatas, screenWidth, yOffset, 220, 150, 50);
}

void CardSystem::spawnCardRowLayout(const std::vector<CardData>& cardDatas,
                                    int screenWidth,
                                    int yOffset,
                                    int cardWidth,
                                    int cardHeight,
                                    int spacing) {
    clearCards();

    int totalWidth = static_cast<int>(cardDatas.size()) * (cardWidth + spacing) - spacing;
    int startX = (screenWidth - totalWidth) / 2;

    for (size_t i = 0; i < cardDatas.size(); ++i) {
        const CardData& data = cardDatas[i];
        std::string imagePath = data.imagePath;
        if (imagePath.empty()) {
            imagePath = "assets/images/" + data.pokemonName + ".png";
        }

        ui::Rect rect = {
            startX + static_cast<int>(i) * (cardWidth + spacing),
            yOffset,
            cardWidth,
            cardHeight
        };

        Card card(rect, imagePath);
        card.setData(data);
        addCard(std::move(card));
    }
}
