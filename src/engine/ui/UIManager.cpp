// UIManager.cpp

#include "UIManager.h"
#include <iostream>

#include "engine/utils/Shader.h"
#include "engine/ui/Card.h" // NEW: for Card::shutdownSharedGL()

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

void UIManager::drawCard(const ui::Rect& /*rect*/, const std::string& /*imagePath*/, Shader* /*shader*/) {
    // Stub: implement textured quad draw as needed
}

void UIManager::shutdown() {
    // NEW: cleanup shared Card GL resources while the GL context is still alive.
    Card::shutdownSharedGL();

    if (s_cardShader) {
        delete s_cardShader;
        s_cardShader = nullptr;
        std::cout << "[UIManager] Card shader destroyed.\n";
    }
}
