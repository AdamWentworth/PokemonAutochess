#pragma once

#include "engine/render/IRenderBackend.h"
#include "engine/render/SpriteTextureCardArt.h"
#include "game/runtime/backend_ui/DebugText.h"
#include "game/runtime/backend_ui/ImagePath.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace game::runtime::ui_cards {

struct CardVisualInput {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::string title;
    std::string subtitle;
    int keyboardSlot = 0;
    bool item = false;
};

struct CardVisualLayout {
    float imagePad = 0.0f;
    float artX = 0.0f;
    float artY = 0.0f;
    float artW = 0.0f;
    float artH = 0.0f;
};

inline std::uint32_t fnv1aHash(const std::string& text) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char c : text) {
        hash ^= static_cast<std::uint32_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

inline std::string resolveCardImagePath(const std::string& explicitImagePath,
                                        const std::string& cardName,
                                        bool itemCard) {
    if (engine::render::sprite_card_art::isProxyPath(explicitImagePath)) {
        return explicitImagePath;
    }
    const std::string fallback = "assets/images/item_placeholder.png";
    const std::string resolved = runtime::ui_images::resolvePokemonPortraitPath(
        explicitImagePath,
        cardName,
        fallback);
    if (itemCard || resolved.empty() || resolved == fallback) {
        return resolved;
    }
    return engine::render::sprite_card_art::makeProxyPath(resolved);
}

inline CardVisualLayout computeCardVisualLayout(const CardVisualInput& input) {
    CardVisualLayout layout;
    if (input.w <= 0.0f || input.h <= 0.0f) return layout;

    layout.imagePad = 6.0f;
    layout.artX = input.x + layout.imagePad;
    layout.artY = input.y + layout.imagePad;
    layout.artW = std::max(0.0f, input.w - layout.imagePad * 2.0f);
    layout.artH = std::max(0.0f, input.h - layout.imagePad * 2.0f);
    return layout;
}

inline IRenderBackend::DebugSprite makeCardArtSprite(const CardVisualInput& input,
                                                     const std::string& imagePath,
                                                     float alpha = 1.0f,
                                                     float u0 = 0.0f,
                                                     float v0 = 0.0f,
                                                     float u1 = 1.0f,
                                                     float v1 = 1.0f) {
    const CardVisualLayout layout = computeCardVisualLayout(input);
    const float clampedU0 = std::clamp(u0, 0.0f, 1.0f);
    const float clampedV0 = std::clamp(v0, 0.0f, 1.0f);
    const float clampedU1 = std::clamp(u1, 0.0f, 1.0f);
    const float clampedV1 = std::clamp(v1, 0.0f, 1.0f);
    IRenderBackend::DebugSprite sprite;
    sprite.x = layout.artX;
    sprite.y = layout.artY;
    sprite.w = layout.artW;
    sprite.h = layout.artH;
    sprite.u0 = std::min(clampedU0, clampedU1);
    sprite.v0 = std::min(clampedV0, clampedV1);
    sprite.u1 = std::max(clampedU0, clampedU1);
    sprite.v1 = std::max(clampedV0, clampedV1);
    sprite.r = 1.0f;
    sprite.g = 1.0f;
    sprite.b = 1.0f;
    sprite.a = std::clamp(alpha, 0.0f, 1.0f);
    sprite.texturePath = imagePath;
    return sprite;
}

inline IRenderBackend::DebugSprite makeCardFrameSprite(const CardVisualInput& input,
                                                       float alpha = 1.0f) {
    IRenderBackend::DebugSprite sprite;
    if (input.w <= 0.0f || input.h <= 0.0f) return sprite;
    sprite.x = input.x;
    sprite.y = input.y;
    sprite.w = input.w;
    sprite.h = input.h;
    sprite.u0 = 0.0f;
    sprite.v0 = 0.0f;
    sprite.u1 = 1.0f;
    sprite.v1 = 1.0f;
    sprite.r = 1.0f;
    sprite.g = 1.0f;
    sprite.b = 1.0f;
    sprite.a = std::clamp(alpha, 0.0f, 1.0f);
    sprite.texturePath = "assets/ui/frame_gold.png";
    return sprite;
}

inline void appendStylizedCardLayered(std::vector<IRenderBackend::DebugQuad>& baseQuads,
                                      std::vector<IRenderBackend::DebugQuad>* textQuads,
                                      const CardVisualInput& input,
                                      float textScale = 1.0f,
                                      std::vector<IRenderBackend::DebugLine>* textLines = nullptr) {
    if (input.w <= 0.0f || input.h <= 0.0f) return;

    // OpenGL-style fallback card backing: image + frame are rendered as sprites;
    // this quad only prevents "holes" if texture streaming lags.
    IRenderBackend::DebugQuad backing;
    backing.x = input.x;
    backing.y = input.y;
    backing.w = input.w;
    backing.h = input.h;
    backing.r = 0.10f;
    backing.g = 0.10f;
    backing.b = 0.10f;
    backing.a = 0.90f;
    baseQuads.push_back(backing);

    // Draw an explicit gold stroke so card framing remains visible even if
    // sprite alpha sampling differs across backends.
    const float border = std::clamp(std::min(input.w, input.h) * 0.022f, 2.0f, 4.5f);
    const float innerX = input.x + border;
    const float innerY = input.y + border;
    const float innerW = std::max(0.0f, input.w - border * 2.0f);
    const float innerH = std::max(0.0f, input.h - border * 2.0f);
    const float goldR = 0.95f;
    const float goldG = 0.78f;
    const float goldB = 0.33f;
    const float goldA = 0.96f;

    IRenderBackend::DebugQuad topBorder;
    topBorder.x = input.x;
    topBorder.y = input.y;
    topBorder.w = input.w;
    topBorder.h = border;
    topBorder.r = goldR;
    topBorder.g = goldG;
    topBorder.b = goldB;
    topBorder.a = goldA;
    baseQuads.push_back(topBorder);

    IRenderBackend::DebugQuad bottomBorder = topBorder;
    bottomBorder.y = input.y + std::max(0.0f, input.h - border);
    baseQuads.push_back(bottomBorder);

    IRenderBackend::DebugQuad leftBorder;
    leftBorder.x = input.x;
    leftBorder.y = innerY;
    leftBorder.w = border;
    leftBorder.h = innerH;
    leftBorder.r = goldR;
    leftBorder.g = goldG;
    leftBorder.b = goldB;
    leftBorder.a = goldA;
    baseQuads.push_back(leftBorder);

    IRenderBackend::DebugQuad rightBorder = leftBorder;
    rightBorder.x = innerX + innerW;
    baseQuads.push_back(rightBorder);

    const auto appendText = [&](float x,
                                float y,
                                const std::string& text,
                                float scale,
                                float r,
                                float g,
                                float b,
                                float a) {
        if (text.empty()) return;
        if (textLines) {
            runtime::ui_text::appendTextLines(
                *textLines, x, y, text, scale, r, g, b, a, 0.88f);
            return;
        }
        if (textQuads) {
            runtime::ui_text::appendTextQuads(
                *textQuads, x, y, text, scale, r, g, b, a);
        }
    };

    if (!input.subtitle.empty()) {
        const float lvlScale = std::max(0.1f, textScale);
        const float x = input.x + 6.0f;
        const float y = input.y + 6.0f;
        appendText(x + 1.5f, y + 1.5f, input.subtitle, lvlScale, 0.0f, 0.0f, 0.0f, 0.75f);
        appendText(x, y, input.subtitle, lvlScale, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (!input.title.empty()) {
        const float labelScale = std::max(0.1f, textScale * 0.80f);
        const float labelW = runtime::ui_text::measureTextWidth(input.title, labelScale);
        const float x = input.x + (input.w - labelW) * 0.5f;
        const float y = input.y + input.h + 6.0f;
        appendText(x, y, input.title, labelScale, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

inline void appendStylizedCard(std::vector<IRenderBackend::DebugQuad>& quads,
                               const CardVisualInput& input,
                               float textScale = 1.0f) {
    appendStylizedCardLayered(quads, &quads, input, textScale);
}

} // namespace game::runtime::ui_cards



