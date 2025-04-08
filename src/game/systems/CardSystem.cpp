// CardSystem.cpp

#include "CardSystem.h"
#include "../../engine/ui/UIManager.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

CardSystem::CardSystem() {}

CardSystem::~CardSystem() {}

void CardSystem::init() {
    UIManager::init();
    cardShader = UIManager::getCardShader();
}

void CardSystem::addCard(Card&& card) {
    cards.push_back(std::move(card));
}

void CardSystem::update(float deltaTime) {
    (void)deltaTime;
}

void CardSystem::render(int screenWidth, int screenHeight) {
    if (!cardShader) return;

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
}

std::optional<CardData> CardSystem::handleMouseClick(int mouseX, int mouseY) {
    for (auto& card : cards) {
        if (card.isPointInside(mouseX, mouseY)) {
            const auto& data = card.getData();
            SDL_Log("[Card Clicked] Pokemon: %s | Cost: %d | Type: %d",
                    data.pokemonName.c_str(), data.cost, static_cast<int>(data.type));
            return data;
        }
    }
    return std::nullopt;
}

void CardSystem::clearCards() {
    cards.clear();
}

void CardSystem::spawnCardRow(const std::vector<CardData>& cardDatas, int screenWidth, int yOffset) {
    clearCards();

    const int cardWidth = 128;
    const int cardHeight = 128;
    const int spacing = 16;

    int totalWidth = static_cast<int>(cardDatas.size()) * (cardWidth + spacing) - spacing;
    int startX = (screenWidth - totalWidth) / 2;

    for (size_t i = 0; i < cardDatas.size(); ++i) {
        const CardData& data = cardDatas[i];
        std::string imagePath = "assets/images/" + data.pokemonName + ".png";

        SDL_Rect rect = {
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
