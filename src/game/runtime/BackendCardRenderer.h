#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/BackendCardVisuals.h"

#include <string>
#include <utility>
#include <vector>

namespace game::runtime::backend_card_renderer {

struct CardRenderInput {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::string displayName;
    std::string speciesName;
    std::string subtitle;
    std::string explicitImagePath;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    int keyboardSlot = 0;
    bool item = false;
    float textScale = 0.74f;
    float spriteAlpha = 1.0f;
};

inline void appendCard(std::vector<IRenderBackend::DebugQuad>& quads,
                       std::vector<IRenderBackend::DebugSprite>* sprites,
                       const CardRenderInput& input) {
    runtime::backend_cards::CardVisualInput visual;
    visual.x = input.x;
    visual.y = input.y;
    visual.w = input.w;
    visual.h = input.h;
    visual.title = input.displayName;
    visual.subtitle = input.subtitle;
    visual.keyboardSlot = input.keyboardSlot;
    visual.item = input.item;
    runtime::backend_cards::appendStylizedCard(quads, visual, input.textScale);

    if (!sprites) return;
    const std::string imagePath = runtime::backend_cards::resolveCardImagePath(
        input.explicitImagePath,
        input.speciesName.empty() ? input.displayName : input.speciesName,
        input.item);
    IRenderBackend::DebugSprite sprite =
        runtime::backend_cards::makeCardArtSprite(
            visual,
            imagePath,
            input.spriteAlpha,
            input.u0,
            input.v0,
            input.u1,
            input.v1);
    if (!sprite.texturePath.empty()) {
        sprites->push_back(std::move(sprite));
    }
}

} // namespace game::runtime::backend_card_renderer
