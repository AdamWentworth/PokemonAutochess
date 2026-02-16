// src/game/ui/ItemInventoryUI.cpp

#include "ItemInventoryUI.h"

#include <algorithm>
#include <functional>

namespace {
struct ItemIcon {
    const char* id;
    int row;
    int col;
};

static const ItemIcon kIcons[] = {
    {"pokeball", 1, 4},
    {"potion", 2, 4},
    {"burn_heal", 2, 6},
    {"antidote", 2, 5},
    {"paralyze_heal", 2, 9}
};

int hashItems(const std::vector<std::pair<std::string, int>>& items) {
    std::size_t h = 0;
    for (const auto& kv : items) {
        std::size_t h1 = std::hash<std::string>{}(kv.first);
        std::size_t h2 = static_cast<std::size_t>(kv.second) * 1315423911u;
        h ^= (h1 + 0x9e3779b9 + (h << 6) + (h >> 2));
        h ^= (h2 + 0x9e3779b9 + (h << 6) + (h >> 2));
    }
    return static_cast<int>(h);
}

const ItemIcon* findIcon(const std::string& id) {
    for (const auto& icon : kIcons) {
        if (id == icon.id) return &icon;
    }
    return nullptr;
}
} // namespace

void ItemInventoryUI::init(const std::string& fontPath, int fontSize) {
    if (initialized) return;
    labelFontSize = std::max(12, fontSize / 3);
    cardSystem.init();
    cardSystem.initOverlayText(fontPath, labelFontSize);
    initialized = true;
}

void ItemInventoryUI::setLayoutInsets(int top, int right, int bottom, int left) {
    const int clampedTop = std::max(0, top);
    const int clampedRight = std::max(0, right);
    const int clampedBottom = std::max(0, bottom);
    const int clampedLeft = std::max(0, left);
    if (clampedTop == insetTop &&
        clampedRight == insetRight &&
        clampedBottom == insetBottom &&
        clampedLeft == insetLeft) {
        return;
    }

    insetTop = clampedTop;
    insetRight = clampedRight;
    insetBottom = clampedBottom;
    insetLeft = clampedLeft;

    if (initialized && lastScreenW > 0 && lastScreenH > 0) {
        rebuildCards(lastScreenW, lastScreenH);
    }
}

float ItemInventoryUI::computeRowPitch() const {
    // Reserve vertical room for the per-card quantity label drawn below each icon.
    constexpr float kLabelScale = 0.8f;
    constexpr float kLabelPad = 6.0f;
    const float labelH = static_cast<float>(labelFontSize) * kLabelScale;
    const float labelReserve = std::max(10.0f, labelH + kLabelPad);
    return static_cast<float>(cardH + spacing) + labelReserve;
}

float ItemInventoryUI::computeVisibleHeight(int screenH) const {
    const int top = std::max(0, insetTop);
    const int bottom = std::max(0, insetBottom);
    return std::max(0.0f, static_cast<float>(screenH - top - bottom));
}

int ItemInventoryUI::topAnchor() const {
    return std::max(0, insetTop);
}

int ItemInventoryUI::rightAnchor(int screenW) const {
    const int right = std::max(0, insetRight);
    return std::max(0, screenW - cardW - right);
}

glm::vec2 ItemInventoryUI::atlasUvMin(int row, int col) const {
    const int c = std::max(1, col);
    const int r = std::max(1, row);
    float u0 = static_cast<float>(c - 1) / static_cast<float>(atlasCols);
    float v0 = static_cast<float>(r - 1) / static_cast<float>(atlasRows);
    u0 += (atlasPadUFrac / static_cast<float>(atlasCols));
    v0 += (atlasPadVFrac / static_cast<float>(atlasRows));
    return {u0, v0};
}

glm::vec2 ItemInventoryUI::atlasUvMax(int row, int col) const {
    const int c = std::max(1, col);
    const int r = std::max(1, row);
    float u1 = static_cast<float>(c) / static_cast<float>(atlasCols);
    float v1 = static_cast<float>(r) / static_cast<float>(atlasRows);
    u1 -= (atlasPadURightFrac / static_cast<float>(atlasCols));
    v1 -= (atlasPadVBottomFrac / static_cast<float>(atlasRows));
    return {u1, v1};
}

void ItemInventoryUI::updateFromWorld(const GameWorld& world, int screenW, int screenH) {
    if (!initialized) return;
    lastScreenW = screenW;
    lastScreenH = screenH;
    auto items = world.listItems();
    const int h = hashItems(items);
    if (h == lastHash) return;
    lastHash = h;

    cards.clear();
    for (const auto& kv : items) {
        const auto* icon = findIcon(kv.first);
        if (!icon) continue;
        CardData cd;
        cd.pokemonName = kv.first;
        cd.cost = 0;
        cd.type = CardType::Item;
        cd.label = "x" + std::to_string(kv.second);
        cd.imagePath = atlasPath;
        cd.uvMin = atlasUvMin(icon->row, icon->col);
        cd.uvMax = atlasUvMax(icon->row, icon->col);
        cards.push_back(cd);
    }

    rebuildCards(screenW, screenH);
}

void ItemInventoryUI::rebuildCards(int screenW, int screenH) {
    cardSystem.clearCards();
    if (cards.empty()) return;

    const float rowPitch = computeRowPitch();
    const float totalHeight = static_cast<float>(cards.size()) * rowPitch;
    const float available = computeVisibleHeight(screenH);
    const float maxScroll = std::max(0.0f, totalHeight - available);
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScroll);

    const int x = rightAnchor(screenW);
    float y = static_cast<float>(topAnchor()) - scrollOffset;

    for (const auto& cd : cards) {
        ui::Rect rect = {
            x,
            static_cast<int>(std::round(y)),
            cardW,
            cardH
        };
        Card card(rect, cd.imagePath);
        card.setData(cd);
        cardSystem.addCard(std::move(card));
        y += rowPitch;
    }
}

void ItemInventoryUI::render(int screenW, int screenH) {
    if (!initialized || !visible) return;
    if (screenW != lastScreenW || screenH != lastScreenH) {
        rebuildCards(screenW, screenH);
        lastScreenW = screenW;
        lastScreenH = screenH;
    }
    cardSystem.render(screenW, screenH);
}

std::optional<std::string> ItemInventoryUI::handleMouseClick(int mouseX, int mouseY) {
    if (!initialized || !visible) return std::nullopt;
    auto clicked = cardSystem.handleMouseClick(mouseX, mouseY);
    if (!clicked) return std::nullopt;
    return clicked->pokemonName;
}

void ItemInventoryUI::handleScroll(int wheelY, int screenH) {
    if (!initialized || !visible) return;
    if (cards.empty()) return;

    const float rowPitch = computeRowPitch();
    const float totalHeight = static_cast<float>(cards.size()) * rowPitch;
    const float available = computeVisibleHeight(screenH);
    if (totalHeight <= available) return;

    scrollOffset -= static_cast<float>(wheelY) * 18.0f;
    scrollOffset = std::clamp(scrollOffset, 0.0f, totalHeight - available);
    rebuildCards(lastScreenW, screenH);
}
