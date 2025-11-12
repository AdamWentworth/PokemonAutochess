// src/game/systems/CardSystem.h
#pragma once
#include <vector>
#include <optional>
#include <string>
#include <SDL2/SDL.h>
#include "../../engine/ui/Card.h"   // Card + CardData used by the UI system

class Shader;

class CardSystem {
public:
    CardSystem();
    ~CardSystem();

    void init();
    void addCard(Card&& card);
    void update(float deltaTime);
    void render(int screenWidth, int screenHeight);

    // Hit-test mouse click; returns selected card data if any.
    std::optional<CardData> handleMouseClick(int mouseX, int mouseY);

    // Lay out a centered row of cards across the screen at the given Y.
    void spawnCardRow(const std::vector<CardData>& cardDatas, int screenWidth, int yOffset);

    void clearCards();

private:
    std::vector<Card> cards;
    Shader* cardShader = nullptr;
};
