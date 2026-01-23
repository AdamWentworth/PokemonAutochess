// UIManager.h

#pragma once
#include <string>
#include "engine/ui/Rect.h"

class Shader;

namespace UIManager {
    // Initialize any UI state
    void init();

    // Return the shader used for drawing cards (owned by UIManager)
    Shader* getCardShader();

    // Draw a card rect with optional image (stub)
    void drawCard(const ui::Rect& rect, const std::string& imagePath, Shader* shader);

    // Tear down any UI resources created in init()
    void shutdown();
}
