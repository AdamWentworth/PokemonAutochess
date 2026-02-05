// UIManager.h
#pragma once

#include <string>
#include <mutex>
#include "engine/ui/Rect.h"

class Shader;

namespace UIManager {
    // Initialize any UI state (idempotent, thread-safe).
    void init();

    // Return the shader used for drawing cards (owned by UIManager).
    // Pointer is valid after init() and until shutdown().
    Shader* getCardShader();

    // Draw a card rect with optional image (stub).
    void drawCard(const ui::Rect& rect, const std::string& imagePath, Shader* shader);

    // Tear down any UI resources created in init().
    // Safe to call multiple times (no-op after first).
    void shutdown();
}
