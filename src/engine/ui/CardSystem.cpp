// CardSystem.cpp

#include "CardSystem.h"

void CardSystem::createCards(const std::vector<std::string>& imagePaths, int startX, int startY, int cardWidth, int cardHeight, int spacing) {
    cards.clear();

    int x = startX;
    for (const auto& path : imagePaths) {
        SDL_Rect rect = { x, startY, cardWidth, cardHeight };
        cards.emplace_back(rect, path);
        x += cardWidth + spacing;
    }
}

void CardSystem::drawAll(Shader* shader) {
    for (const auto& card : cards) {
        card.draw(shader);
    }
}

int CardSystem::getCardIndexAt(int x, int y) const {
    for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
        if (cards[i].isPointInside(x, y)) {
            return i;
        }
    }
    return -1;
}

void CardSystem::clear() {
    cards.clear();
}

const std::vector<Card>& CardSystem::getCards() const {
    return cards;
}
