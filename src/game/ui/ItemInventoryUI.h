// src/game/ui/ItemInventoryUI.h
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "game/GameWorld.h"
#include "game/systems/CardSystem.h"

class ItemInventoryUI {
public:
    ItemInventoryUI() = default;

    void init(const std::string& fontPath, int fontSize);
    void setVisible(bool v) { visible = v; }
    bool isVisible() const { return visible; }

    void updateFromWorld(const GameWorld& world, int screenW, int screenH);
    void render(int screenW, int screenH);

    std::optional<std::string> handleMouseClick(int mouseX, int mouseY);
    void handleScroll(int wheelY, int screenH);
    void setLayoutInsets(int top, int right, int bottom, int left);

private:
    void rebuildCards(int screenW, int screenH);
    float computeRowPitch() const;
    float computeVisibleHeight(int screenH) const;
    int topAnchor() const;
    int rightAnchor(int screenW) const;
    glm::vec2 atlasUvMin(int row, int col) const;
    glm::vec2 atlasUvMax(int row, int col) const;

    CardSystem cardSystem;
    std::vector<CardData> cards;

    bool initialized = false;
    bool visible = true;
    float scrollOffset = 0.0f;
    int lastHash = 0;
    int lastScreenW = 0;
    int lastScreenH = 0;
    int labelFontSize = 12;

    // Layout
    int cardW = 72;
    int cardH = 72;
    int spacing = 12;
    int insetTop = 16;
    int insetRight = 16;
    int insetBottom = 16;
    int insetLeft = 16;

    // Atlas (keep in sync with shop script)
    std::string atlasPath = "assets/images/items_atlas.png";
    int atlasCols = 13;
    int atlasRows = 14;
    float atlasPadUFrac = 0.08f;
    float atlasPadVFrac = 0.08f;
    float atlasPadURightFrac = 0.06f;
    float atlasPadVBottomFrac = 0.06f;
};
