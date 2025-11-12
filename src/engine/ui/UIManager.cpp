// UIManager.cpp

#include "UIManager.h"
#include <iostream>
#include "../utils/Shader.h"

// File-local state (not exposed via header)
namespace {
    Shader* s_cardShader = nullptr;
}

void UIManager::init() {
    if (!s_cardShader) {
        s_cardShader = new Shader("assets/shaders/ui/card.vert", "assets/shaders/ui/card.frag");
        std::cout << "[UIManager] Card shader initialized.\n";
    }
}

Shader* UIManager::getCardShader() {
    return s_cardShader;
}

void UIManager::drawCard(const SDL_Rect& /*rect*/, const std::string& /*imagePath*/, Shader* /*shader*/) {
    // Stub: implement textured quad draw as needed
}

void UIManager::shutdown() {
    if (s_cardShader) {
        delete s_cardShader;
        s_cardShader = nullptr;
        std::cout << "[UIManager] Card shader destroyed.\n";
    }
}
