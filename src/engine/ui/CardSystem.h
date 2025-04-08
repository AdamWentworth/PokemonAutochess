// CardSystem.h

#pragma once

#include "Card.h"
#include <vector>
#include <string>
#include <functional>

class Shader;

class CardSystem {
public:
    void createCards(const std::vector<std::string>& imagePaths, int startX, int startY, int cardWidth, int cardHeight, int spacing);
    void drawAll(Shader* shader);
    int getCardIndexAt(int x, int y) const; // Returns -1 if none
    void clear();

    const std::vector<Card>& getCards() const;

private:
    std::vector<Card> cards;
};
