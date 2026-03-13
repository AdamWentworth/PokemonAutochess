#pragma once

#include "engine/render/IRenderBackend.h"
#include "engine/ui/Card.h"
#include "game/runtime/backend_ui/CardVisuals.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace game::runtime::ui_card_renderer {

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

inline std::string resolveCardImagePath(const CardData& card, bool forceItemRow = false) {
    const bool itemCard = forceItemRow || card.type == CardType::Item;
    return runtime::ui_cards::resolveCardImagePath(
        card.imagePath,
        card.pokemonName.empty() ? card.label : card.pokemonName,
        itemCard);
}

inline void prepareCardDataForBackendRender(std::vector<CardData>& cards, bool forceItemRow = false) {
    for (CardData& card : cards) {
        const std::string resolved = resolveCardImagePath(card, forceItemRow);
        if (!resolved.empty()) {
            card.imagePath = resolved;
        }
    }
}

inline void prewarmCardDataTextures(IRenderBackend* renderer,
                                    const std::vector<CardData>& cards,
                                    bool forceItemRow = false) {
    if (!renderer) return;

    std::vector<std::string> paths;
    paths.reserve(cards.size() + 1u);
    paths.push_back("assets/ui/frame_gold.png");
    std::unordered_set<std::string> seenPaths;
    seenPaths.reserve(cards.size());
    for (const CardData& card : cards) {
        const std::string resolved = resolveCardImagePath(card, forceItemRow);
        if (resolved.empty()) continue;
        if (!seenPaths.insert(resolved).second) continue;
        paths.push_back(resolved);
    }

    std::vector<const char*> rawPaths;
    rawPaths.reserve(paths.size());
    for (const std::string& path : paths) {
        rawPaths.push_back(path.c_str());
    }
    if (!rawPaths.empty()) {
        renderer->prewarmDebugSpriteTextures(rawPaths.data(), rawPaths.size());
    }
}

inline void appendCardLayered(std::vector<IRenderBackend::DebugQuad>& baseQuads,
                              std::vector<IRenderBackend::DebugQuad>* textQuads,
                              std::vector<IRenderBackend::DebugSprite>* sprites,
                              const CardRenderInput& input,
                              std::vector<IRenderBackend::DebugLine>* textLines = nullptr) {
    runtime::ui_cards::CardVisualInput visual;
    visual.x = input.x;
    visual.y = input.y;
    visual.w = input.w;
    visual.h = input.h;
    visual.title = input.displayName;
    visual.subtitle = input.subtitle;
    visual.keyboardSlot = input.keyboardSlot;
    visual.item = input.item;
    runtime::ui_cards::appendStylizedCardLayered(baseQuads, textQuads, visual, input.textScale, textLines);

    if (!sprites) return;
    const std::string imagePath = runtime::ui_cards::resolveCardImagePath(
        input.explicitImagePath,
        input.speciesName.empty() ? input.displayName : input.speciesName,
        input.item);
    IRenderBackend::DebugSprite sprite =
        runtime::ui_cards::makeCardArtSprite(
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

    IRenderBackend::DebugSprite frameSprite =
        runtime::ui_cards::makeCardFrameSprite(visual, 1.0f);
    if (!frameSprite.texturePath.empty()) {
        sprites->push_back(std::move(frameSprite));
    }
}

inline void appendCard(std::vector<IRenderBackend::DebugQuad>& quads,
                       std::vector<IRenderBackend::DebugSprite>* sprites,
                       const CardRenderInput& input) {
    appendCardLayered(quads, &quads, sprites, input);
}

} // namespace game::runtime::ui_card_renderer



