// UIManager.cpp

#include "UIManager.h"
#include <iostream>

// Initialize the static member.
Shader* UIManager::cardShader = nullptr;

void UIManager::init() {
    if (!cardShader) {
        cardShader = new Shader("assets/shaders/ui/card.vert", "assets/shaders/ui/card.frag");
        std::cout << "[UIManager] Card shader initialized.\n";
    }
}

Shader* UIManager::getCardShader() {
    return cardShader;
}

void UIManager::shutdown() {
    if (cardShader) {
        delete cardShader;
        cardShader = nullptr;
    }
}
