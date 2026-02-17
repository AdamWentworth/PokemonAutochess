#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/BackendDebugText.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace game::runtime::backend_cards {

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
    float outerPad = 0.0f;
    float innerPad = 0.0f;
    float artX = 0.0f;
    float artY = 0.0f;
    float artW = 0.0f;
    float artH = 0.0f;
    float footerY = 0.0f;
};

inline std::uint32_t fnv1aHash(const std::string& text) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char c : text) {
        hash ^= static_cast<std::uint32_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

inline std::string normalizeCardNameForImage(std::string name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_') {
            out.push_back(static_cast<char>(std::tolower(uc)));
            continue;
        }
        if (std::isspace(uc)) {
            out.push_back('_');
        }
    }
    return out;
}

inline std::string resolveCardImagePath(const std::string& explicitImagePath,
                                        const std::string& cardName,
                                        bool itemCard) {
    if (!explicitImagePath.empty()) return explicitImagePath;
    if (itemCard) return "assets/images/item_placeholder.png";
    const std::string normalizedName = normalizeCardNameForImage(cardName);
    if (normalizedName.empty()) return "assets/images/item_placeholder.png";
    return "assets/images/" + normalizedName + ".png";
}

inline CardVisualLayout computeCardVisualLayout(const CardVisualInput& input) {
    CardVisualLayout layout;
    if (input.w <= 0.0f || input.h <= 0.0f) return layout;

    const float artHeight = std::max(14.0f, input.h * 0.58f);
    layout.outerPad = std::max(1.0f, std::min(input.w, input.h) * 0.016f);
    layout.innerPad = std::max(2.0f, std::min(input.w, input.h) * 0.028f);
    layout.artX = input.x + layout.innerPad;
    layout.artY = input.y + layout.innerPad;
    layout.artW = std::max(0.0f, input.w - layout.innerPad * 2.0f);
    layout.artH = std::max(0.0f, artHeight - layout.innerPad * 1.7f);
    layout.footerY = input.y + artHeight;
    return layout;
}

inline IRenderBackend::DebugSprite makeCardArtSprite(const CardVisualInput& input,
                                                     const std::string& imagePath,
                                                     float alpha = 1.0f) {
    const CardVisualLayout layout = computeCardVisualLayout(input);
    IRenderBackend::DebugSprite sprite;
    sprite.x = layout.artX;
    sprite.y = layout.artY;
    sprite.w = layout.artW;
    sprite.h = layout.artH;
    sprite.u0 = 0.0f;
    sprite.v0 = 0.0f;
    sprite.u1 = 1.0f;
    sprite.v1 = 1.0f;
    sprite.r = 1.0f;
    sprite.g = 1.0f;
    sprite.b = 1.0f;
    sprite.a = std::clamp(alpha, 0.0f, 1.0f);
    sprite.texturePath = imagePath;
    return sprite;
}

inline void appendStylizedCard(std::vector<IRenderBackend::DebugQuad>& quads,
                               const CardVisualInput& input,
                               float textScale = 1.0f) {
    if (input.w <= 0.0f || input.h <= 0.0f) return;

    const std::uint32_t hash = fnv1aHash(input.title);
    const float tintA = static_cast<float>((hash >> 0) & 0xFF) / 255.0f;
    const float tintB = static_cast<float>((hash >> 8) & 0xFF) / 255.0f;
    const float tintC = static_cast<float>((hash >> 16) & 0xFF) / 255.0f;

    const float accentR = input.item ? 0.78f : std::clamp(0.28f + tintA * 0.42f, 0.0f, 1.0f);
    const float accentG = input.item ? 0.58f : std::clamp(0.28f + tintB * 0.40f, 0.0f, 1.0f);
    const float accentB = input.item ? 0.24f : std::clamp(0.38f + tintC * 0.48f, 0.0f, 1.0f);

    const CardVisualLayout layout = computeCardVisualLayout(input);
    const float outerPad = layout.outerPad;
    const float innerPad = layout.innerPad;
    const float footerY = layout.footerY;
    const float badgeSize = std::max(12.0f, std::min(input.w, input.h) * 0.16f);

    IRenderBackend::DebugQuad shadow;
    shadow.x = input.x + 2.0f;
    shadow.y = input.y + 3.0f;
    shadow.w = input.w;
    shadow.h = input.h;
    shadow.r = 0.00f;
    shadow.g = 0.00f;
    shadow.b = 0.00f;
    shadow.a = 0.42f;
    quads.push_back(shadow);

    IRenderBackend::DebugQuad frame;
    frame.x = input.x;
    frame.y = input.y;
    frame.w = input.w;
    frame.h = input.h;
    frame.r = 0.06f;
    frame.g = 0.08f;
    frame.b = 0.11f;
    frame.a = 0.95f;
    quads.push_back(frame);

    IRenderBackend::DebugQuad inset;
    inset.x = input.x + outerPad;
    inset.y = input.y + outerPad;
    inset.w = std::max(0.0f, input.w - outerPad * 2.0f);
    inset.h = std::max(0.0f, input.h - outerPad * 2.0f);
    inset.r = std::clamp(accentR * 0.45f, 0.0f, 1.0f);
    inset.g = std::clamp(accentG * 0.45f, 0.0f, 1.0f);
    inset.b = std::clamp(accentB * 0.45f, 0.0f, 1.0f);
    inset.a = 0.90f;
    quads.push_back(inset);

    IRenderBackend::DebugQuad art;
    art.x = layout.artX;
    art.y = layout.artY;
    art.w = layout.artW;
    art.h = layout.artH;
    art.r = std::clamp(accentR * 0.90f, 0.0f, 1.0f);
    art.g = std::clamp(accentG * 0.90f, 0.0f, 1.0f);
    art.b = std::clamp(accentB * 0.92f, 0.0f, 1.0f);
    art.a = 0.34f;
    quads.push_back(art);

    IRenderBackend::DebugQuad shine;
    shine.x = art.x;
    shine.y = art.y;
    shine.w = art.w;
    shine.h = std::max(0.0f, art.h * 0.32f);
    shine.r = 1.0f;
    shine.g = 1.0f;
    shine.b = 1.0f;
    shine.a = 0.10f;
    quads.push_back(shine);

    IRenderBackend::DebugQuad footer;
    footer.x = input.x + innerPad;
    footer.y = footerY;
    footer.w = std::max(0.0f, input.w - innerPad * 2.0f);
    footer.h = std::max(0.0f, input.h - (footerY - input.y) - innerPad);
    footer.r = 0.08f;
    footer.g = 0.10f;
    footer.b = 0.14f;
    footer.a = 0.92f;
    quads.push_back(footer);

    if (input.keyboardSlot > 0) {
        IRenderBackend::DebugQuad badge;
        badge.x = input.x + innerPad;
        badge.y = input.y + innerPad;
        badge.w = badgeSize;
        badge.h = badgeSize;
        badge.r = 0.96f;
        badge.g = 0.84f;
        badge.b = 0.36f;
        badge.a = 0.96f;
        quads.push_back(badge);

        runtime::backend_text::appendTextQuads(quads,
                                               badge.x + 3.0f,
                                               badge.y + 2.0f,
                                               std::to_string(input.keyboardSlot),
                                               textScale * 0.75f,
                                               0.11f,
                                               0.11f,
                                               0.09f,
                                               1.0f);
    }

    runtime::backend_text::appendTextQuads(quads,
                                           input.x + innerPad,
                                           footerY + std::max(4.0f, innerPad * 0.65f),
                                           input.title,
                                           textScale,
                                           0.97f,
                                           0.97f,
                                           0.99f,
                                           1.0f);
    if (!input.subtitle.empty()) {
        runtime::backend_text::appendTextQuads(quads,
                                               input.x + innerPad,
                                               footerY + std::max(16.0f, innerPad * 0.65f + 14.0f),
                                               input.subtitle,
                                               textScale * 0.90f,
                                               0.86f,
                                               0.91f,
                                               0.97f,
                                               1.0f);
    }
}

} // namespace game::runtime::backend_cards
