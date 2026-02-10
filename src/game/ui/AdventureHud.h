#pragma once

#include <memory>
#include <string>

class TextRenderer;

namespace game::ui {

struct AdventureHudRenderInput {
    int uiW = 1280;
    int uiH = 720;
    bool showDebugLabel = false;
};

// Scaffold for mode-specific HUD divergence.
// This keeps adventure-specific UI entry points separated from classic shop HUD code.
class AdventureHud {
public:
    void init(const std::string& fontPath, int fontSize);
    void shutdown();
    void render(const AdventureHudRenderInput& in);

private:
    std::unique_ptr<TextRenderer> text_;
};

} // namespace game::ui
