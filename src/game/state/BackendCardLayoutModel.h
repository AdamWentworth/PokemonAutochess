#pragma once

#include "game/ui/ShopLayout.h"
#include "game/ui/legacy/Card.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace game::state::backend_cards {

enum class LayoutMode {
    Shop,
    Starter
};

struct BuildInput {
    std::vector<CardData> cards;
    int uiW = 1280;
    int uiH = 720;
    LayoutMode mode = LayoutMode::Shop;
    bool forceItemRow = false;
};

struct Button {
    CardData data;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool item = false;
};

inline bool allItemCards(const std::vector<CardData>& cards) {
    for (const auto& card : cards) {
        if (card.type != CardType::Item) return false;
    }
    return true;
}

inline std::vector<Button> buildButtons(const BuildInput& in) {
    std::vector<Button> out;
    if (in.cards.empty()) return out;

    const bool allItems = in.forceItemRow || allItemCards(in.cards);
    const game::ui::ShopRowLayout layout = game::ui::computeShopRowLayout(in.uiW, in.uiH, allItems);
    const int count = static_cast<int>(in.cards.size());
    int cardW = std::max(64, layout.cardW);
    int cardH = std::max(48, layout.cardH);
    int spacing = std::max(6, layout.spacing);

    int startX = layout.edgeMargin;
    int rowY = layout.edgeMargin;
    if (in.mode == LayoutMode::Shop) {
        const game::ui::ShopRowPlacement place =
            game::ui::computeShopRowPlacement(in.uiW, in.uiH, count, layout);
        startX = place.startX;
        rowY = place.y;
        if (in.forceItemRow) {
            rowY = std::max(layout.edgeMargin + 64,
                            static_cast<int>(std::round(static_cast<float>(in.uiH) * 0.16f)));
        }
    } else {
        // Leave the lab visible above the choices. Both rendering paths and
        // mouse hit testing use this geometry, including narrow editor panes.
        const float scale = std::clamp(std::min(in.uiW / 1000.0f, in.uiH / 600.0f), 0.35f, 1.0f);
        cardW = static_cast<int>(std::round(220 * scale));
        cardH = static_cast<int>(std::round(150 * scale));
        spacing = static_cast<int>(std::round(50 * scale));
        const int availableW = std::max(count, in.uiW - layout.edgeMargin * 2);
        if (count * cardW + (count - 1) * spacing > availableW) {
            spacing = std::min(spacing, availableW / (count * 8));
            cardW = std::max(1, (availableW - (count - 1) * spacing) / count);
            cardH = std::max(1, cardW * 150 / 220);
        }
        const int totalW = count * (cardW + spacing) - spacing;
        startX = std::max(layout.edgeMargin, (in.uiW - totalW) / 2);
        rowY = std::max(layout.edgeMargin, in.uiH - cardH - static_cast<int>(std::round(64 * scale)));
    }

    out.reserve(in.cards.size());
    for (int i = 0; i < count; ++i) {
        Button b;
        b.data = in.cards[static_cast<std::size_t>(i)];
        b.x = static_cast<float>(startX + i * (cardW + spacing));
        b.y = static_cast<float>(rowY);
        b.w = static_cast<float>(cardW);
        b.h = static_cast<float>(cardH);
        b.item = (b.data.type == CardType::Item) || in.forceItemRow;
        out.push_back(std::move(b));
    }
    return out;
}

} // namespace game::state::backend_cards
