#pragma once

#include "game/runtime/ui/HudPrimitives.h"
#include "game/runtime/ui/HudFormatting.h"
#include <array>
#include <algorithm>
#include <string_view>

namespace game::runtime::type_roster_hud {

struct TypeStyle {
    std::string_view id;
    glm::vec3 color;
};
inline const std::array<TypeStyle, 18> styles{{{"normal", {.60f, .64f, .64f}}, {"fire", {.92f, .37f, .22f}}, {"water", {.25f, .57f, .88f}}, {"electric", {.88f, .70f, .18f}}, {"grass", {.36f, .69f, .31f}}, {"ice", {.36f, .76f, .76f}}, {"fighting", {.79f, .33f, .40f}}, {"poison", {.65f, .40f, .75f}}, {"ground", {.80f, .57f, .33f}}, {"flying", {.52f, .66f, .86f}}, {"psychic", {.89f, .40f, .53f}}, {"bug", {.59f, .69f, .24f}}, {"rock", {.70f, .63f, .40f}}, {"ghost", {.40f, .44f, .68f}}, {"dragon", {.31f, .43f, .83f}}, {"dark", {.39f, .37f, .42f}}, {"steel", {.40f, .61f, .67f}}, {"fairy", {.82f, .55f, .78f}}}};

inline const TypeStyle *styleFor(std::string_view type) {
    for (const auto &style : styles)
        if (style.id == type) return &style;
    return nullptr;
}

// Unmodified Pokemon HOME icons; provenance lives in config/ui/pokemon_type_icons.json.
inline void icon(std::vector<IRenderBackend::DebugSprite> &sprites,
                 std::string_view type, float x, float y, float size) {
    if (!styleFor(type)) return;
    IRenderBackend::DebugSprite sprite;
    sprite.texturePath = "assets/ui/types/home/" + std::string(type) + ".png";
    sprite.x = x;
    sprite.y = y;
    sprite.w = size;
    sprite.h = size;
    sprites.push_back(std::move(sprite));
}

struct Layout {
    float x = 0, y = 0, w = 0, h = 0, scale = 1, rowH = 0, columnW = 0;
    int rowsPerColumn = 1, columns = 1;
};
inline Layout layout(int width, int height, int count, bool inspecting = false) {
    Layout out;
    out.scale = std::clamp(std::min(width / 1280.0f, height / 720.0f), .6f, 1.4f);
    out.x = std::round(18 * out.scale);
    out.y = std::round((inspecting ? 288 : 104) * out.scale);
    out.rowH = (inspecting ? 22 : 30) * out.scale;
    out.columnW = 172 * out.scale;
    const float chromeH = inspecting ? 61.0f : 73.0f;
    const float available = std::max(out.rowH, height * .76f - out.y - chromeH * out.scale);
    out.rowsPerColumn = std::max(1, static_cast<int>(available / out.rowH));
    out.columns = std::max(1, (count + out.rowsPerColumn - 1) / out.rowsPerColumn);
    out.rowsPerColumn = std::max(1, (count + out.columns - 1) / out.columns);
    if (inspecting && out.columns == 1) out.columnW = 240 * out.scale;
    out.w = std::max(out.columnW * out.columns + 20 * out.scale, inspecting ? 260 * out.scale : 0);
    out.h = out.rowsPerColumn * out.rowH + chromeH * out.scale;
    return out;
}

template <class Rows>
inline void append(std::vector<IRenderBackend::DebugQuad> &quads,
                   std::vector<IRenderBackend::DebugLine> &lines,
                   std::vector<IRenderBackend::DebugSprite> &sprites,
                   int width, int height, const Rows &types, int benchCount, bool inspecting = false) {
    if (types.empty()) return;
    const auto l = layout(width, height, static_cast<int>(types.size()), inspecting);
    const float s = l.scale;
    hud_paint::panel(quads, l.x + 2 * s, l.y + 3 * s, l.w, l.h, 9 * s, {.01f, .025f, .02f}, .25f);
    hud_paint::panel(quads, l.x, l.y, l.w, l.h, 9 * s);
    hud_paint::text(lines, l.x + 12 * s, l.y + 12 * s, "TEAM TYPES", std::max(1.0f, 1.13f * s), {.90f, .85f, .63f});
    hud_paint::text(lines, l.x + 12 * s, l.y + 29 * s, "Distinct evolution lines", std::max(.68f, .81f * s), {.59f, .70f, .66f});
    for (std::size_t i = 0; i < types.size(); ++i) {
        const auto &row = types[i];
        const float x = l.x + 10 * s + static_cast<int>(i / l.rowsPerColumn) * l.columnW;
        const float y = l.y + (inspecting ? 40 : 46) * s + static_cast<int>(i % l.rowsPerColumn) * l.rowH;
        const auto *style = styleFor(row.type);
        const glm::vec3 color = style ? style->color : glm::vec3(.5f);
        hud_paint::panel(quads, x, y, l.columnW - 2 * s, l.rowH - 3 * s, 4 * s, color * .16f + glm::vec3(.045f), 1);
        icon(sprites, row.type, x + 4 * s, y + 2 * s, (inspecting ? 16 : 21) * s);
        hud_paint::text(lines, x + 33 * s, y + (inspecting ? 5 : 8) * s, hud::humanizeToken(row.type), std::max(.95f, 1.06f * s));
        const std::string count = std::to_string(row.uniqueLineCount);
        const float countScale = std::max(1.0f, 1.16f * s);
        hud_paint::text(lines, x + l.columnW - 14 * s - ui_text::measureTextWidth(count, countScale), y + (inspecting ? 4 : 7) * s, count, countScale, color * .35f + glm::vec3(.65f));
    }
    hud_paint::text(lines, l.x + 12 * s, l.y + l.h - 17 * s,
                    "Board + bench  |  Bench: " + std::to_string(benchCount), std::max(.68f, .81f * s), {.59f, .70f, .66f});
}

} // namespace game::runtime::type_roster_hud
