// CardSystem.h

#pragma once

#include <vector>
#include <SDL2/SDL.h>
#include "../../engine/ui/Card.h"

/**
 * Forward declare Shader so we don't force CardSystem users
 * to include GL headers if they don't need them.
 */
class Shader;

/**
 * CardSystem is responsible for:
 *   - Creating and storing UI Cards
 *   - Rendering them with a shared UI shader
 *   - Possibly handling interaction logic (picking, hovering, etc.)
 *
 * Note: This is just one example; you may do ECS or other patterns differently.
 */
class CardSystem {
public:
    // Constructor / Destructor
    CardSystem();
    ~CardSystem();

    // Typically, you'd call this once at startup, or do it in the constructor
    void init();

    // Adds a new card for the system to track; accepts a Card as an rvalue.
    void addCard(Card&& card);

    // An update step if needed, e.g. for animations or hover detection
    void update(float deltaTime);

    // Renders all the cards in the system
    void render(int screenWidth, int screenHeight);

    // Optionally, you can have methods to handle input or do
    // collision checks if you want that logic in here:
    bool handleMouseClick(int mouseX, int mouseY);

    // Clears out all cards
    void clearCards();

private:
    // The shared UI shader we’ll use for drawing
    Shader* cardShader = nullptr;

    // Simple container of Card objects
    std::vector<Card> cards;
};
