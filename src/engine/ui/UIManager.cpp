// UIManager.cpp

#include "engine/ui/UIManager.h"

#include <iostream>
#include <memory>
#include <mutex>

#include "engine/utils/Shader.h"
#include "engine/ui/Card.h" // for Card::shutdownSharedGL()

namespace {
    std::once_flag s_initOnce;
    std::mutex s_mutex;

    std::unique_ptr<Shader> s_cardShader;
    bool s_initialized = false;
}

void UIManager::init() {
    std::call_once(s_initOnce, []() {
        std::lock_guard<std::mutex> lock(s_mutex);
        // Create shader once. Any failures should throw from Shader ctor, surfacing early.
        s_cardShader = std::make_unique<Shader>(
            "assets/shaders/ui/card.vert",
            "assets/shaders/ui/card.frag"
        );
        s_initialized = true;
        std::cout << "[UIManager] Card shader initialized.\n";
    });
}

Shader* UIManager::getCardShader() {
    // Preserve old behavior: return nullptr if init() wasn't called.
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_cardShader.get();
}

void UIManager::drawCard(const ui::Rect& /*rect*/, const std::string& /*imagePath*/, Shader* /*shader*/) {
    // Stub: implement textured quad draw as needed.
}

void UIManager::shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);

    // Cleanup shared Card GL resources while the GL context is still alive.
    // Safe to call multiple times if Card implementation is idempotent.
    Card::shutdownSharedGL();

    if (s_cardShader) {
        s_cardShader.reset();
        std::cout << "[UIManager] Card shader destroyed.\n";
    }
    s_initialized = false;

    // NOTE:
    // std::once_flag cannot be reset, so init() won't re-run after shutdown().
    // If you need re-init in the same process (hot-reload), replace call_once
    // with an explicit state machine and allow re-creation.
}
