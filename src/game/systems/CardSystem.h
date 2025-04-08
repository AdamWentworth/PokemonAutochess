// CardSystem.h

#pragma once

#include <vector>
#include <optional>
#include <SDL2/SDL.h>
#include "../../engine/ui/Card.h"

class Shader;

class CardSystem {
public:
    CardSystem();
    ~CardSystem();

    void init();
    void addCard(Card&& card);
    void update(float deltaTime);
    void render(int screenWidth, int screenHeight);
    std::optional<CardData> handleMouseClick(int mouseX, int mouseY);
    void clearCards();

    void spawnCardRow(const std::vector<CardData>& cardDatas, int screenWidth, int yOffset);

private:
    Shader* cardShader = nullptr;
    std::vector<Card> cards;
};
