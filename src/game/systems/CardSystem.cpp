// CardSystem.cpp

#include "CardSystem.h"

// Include your UIManager (which sets up the card shader)
#include "../../engine/ui/UIManager.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

CardSystem::CardSystem() {
    // Optionally call init() here or do nothing.
    // It's often better to do minimal in constructors that rely on OpenGL states.
}

CardSystem::~CardSystem() {
    // We don’t delete cardShader ourselves—UIManager owns it and
    // we call UIManager::shutdown() once at the end of the application.
}

void CardSystem::init() {
    // Ensure the UIManager has been initialized (this is safe to call repeatedly).
    UIManager::init();
    cardShader = UIManager::getCardShader();
}

void CardSystem::addCard(Card&& card) {
    // Move the card into the vector.
    cards.push_back(std::move(card));
}

void CardSystem::update(float deltaTime) {
    // If your cards have any animations, you could update them here.
    // For now, do nothing.
    (void)deltaTime; // Silence unused param warning if not used
}

void CardSystem::render(int screenWidth, int screenHeight) {
    if (!cardShader) {
        // If this pointer is still null, we never called init().
        return;
    }

    // Use the orthographic matrix same as your StarterSelectionState was using
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

    // You might reset or unbind here if you want:
    glUseProgram(0);
}

bool CardSystem::handleMouseClick(int mouseX, int mouseY) {
    // Simple example: check each card for a hit
    for (auto& card : cards) {
        if (card.isPointInside(mouseX, mouseY)) {
            // For now, just print. In your real code, you'd do something else
            SDL_Log("Card clicked: %s", card.getImagePath().c_str());
            // Return true if you want to indicate that you consumed the click
            return true;
        }
    }
    return false;
}

void CardSystem::clearCards() {
    cards.clear();
}

