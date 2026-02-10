#include "game/ui/AdventureHud.h"

#include "engine/ui/TextRenderer.h"

namespace game::ui {

void AdventureHud::init(const std::string& fontPath, int fontSize) {
    if (text_) return;
    text_ = std::make_unique<TextRenderer>(fontPath, fontSize);
}

void AdventureHud::shutdown() {
    text_.reset();
}

void AdventureHud::render(const AdventureHudRenderInput& in) {
    if (!text_ || !in.showDebugLabel) return;
    text_->renderText("Adventure HUD (placeholder)", 20.0f, 20.0f, glm::vec3(1.0f), 0.5f);
}

} // namespace game::ui
