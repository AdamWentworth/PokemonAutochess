#pragma once

#include "game/runtime/ui/HudPrimitives.h"
#include "game/runtime/ui/HudFormatting.h"
#include <array>
#include <initializer_list>
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

// Small vector symbols are retained with the roster geometry. They remain
// sharp at editor viewport sizes and use exactly the same path on all APIs.
inline void icon(std::vector<IRenderBackend::DebugQuad> &quads,
                 std::vector<IRenderBackend::DebugLine> &lines,
                 std::string_view type, float x, float y, float size) {
    const auto *style = styleFor(type);
    hud_paint::panel(quads, x, y, size, size, size * .5f,
                     style ? style->color : glm::vec3(.48f), 1.0f);
    const auto path = [&](std::initializer_list<glm::vec2> points) {
        if (points.size() < 2) return;
        auto prev = points.begin();
        for (auto p = prev + 1; p != points.end(); ++p, ++prev) {
            IRenderBackend::DebugLine line;
            line.x1 = x + prev->x * size;
            line.y1 = y + prev->y * size;
            line.x2 = x + p->x * size;
            line.y2 = y + p->y * size;
            line.r = line.g = line.b = .99f;
            line.a = 1;
            line.thickness = std::max(1.0f, size * .065f);
            lines.push_back(line);
        }
    };
    const auto ring = [&](float cx, float cy, float radius) {
        for (int i = 0; i < 16; ++i) {
            const float a = i * 6.2831853f / 16, b = (i + 1) * 6.2831853f / 16;
            path({{cx + std::cos(a) * radius, cy + std::sin(a) * radius},
                  {cx + std::cos(b) * radius, cy + std::sin(b) * radius}});
        }
    };
    if (type == "fire") path({{.49f, .17f}, {.42f, .39f}, {.31f, .33f}, {.25f, .56f}, {.31f, .74f}, {.52f, .80f}, {.70f, .69f}, {.76f, .50f}, {.63f, .29f}, {.58f, .51f}, {.49f, .17f}});
    else if (type == "water") path({{.5f, .18f}, {.26f, .53f}, {.27f, .68f}, {.37f, .78f}, {.61f, .78f}, {.74f, .65f}, {.73f, .52f}, {.5f, .18f}});
    else if (type == "grass") {
        path({{.24f, .76f}, {.28f, .42f}, {.48f, .27f}, {.78f, .20f}, {.73f, .55f}, {.52f, .72f}, {.24f, .76f}});
        path({{.24f, .77f}, {.64f, .39f}});
    } else if (type == "electric") path({{.52f, .17f}, {.28f, .53f}, {.47f, .53f}, {.41f, .84f}, {.74f, .41f}, {.53f, .41f}, {.52f, .17f}});
    else if (type == "flying") {
        path({{.22f, .74f}, {.36f, .32f}, {.81f, .24f}, {.53f, .44f}, {.72f, .43f}, {.47f, .62f}, {.59f, .61f}, {.40f, .75f}, {.22f, .74f}});
    } else if (type == "ice") {
        for (int i = 0; i < 3; ++i) {
            float a = i * 3.14159265f / 3;
            path({{.5f - std::cos(a) * .31f, .5f - std::sin(a) * .31f}, {.5f + std::cos(a) * .31f, .5f + std::sin(a) * .31f}});
        }
        ring(.5f, .5f, .14f);
    } else if (type == "bug") {
        ring(.5f, .55f, .22f);
        path({{.5f, .35f}, {.5f, .77f}});
        path({{.36f, .35f}, {.28f, .23f}});
        path({{.64f, .35f}, {.72f, .23f}});
    } else if (type == "poison") {
        ring(.5f, .43f, .23f);
        ring(.42f, .43f, .035f);
        ring(.59f, .43f, .035f);
        path({{.37f, .64f}, {.37f, .76f}, {.63f, .76f}, {.63f, .64f}});
    } else if (type == "ground") {
        path({{.21f, .72f}, {.44f, .29f}, {.56f, .53f}, {.67f, .38f}, {.82f, .72f}, {.21f, .72f}});
        path({{.22f, .81f}, {.80f, .81f}});
    } else if (type == "rock") path({{.22f, .60f}, {.33f, .28f}, {.64f, .23f}, {.81f, .53f}, {.65f, .76f}, {.33f, .77f}, {.22f, .60f}, {.55f, .49f}, {.64f, .23f}, {.55f, .49f}, {.65f, .76f}});
    else if (type == "steel") {
        path({{.33f, .23f}, {.67f, .23f}, {.82f, .50f}, {.67f, .77f}, {.33f, .77f}, {.18f, .50f}, {.33f, .23f}});
        ring(.5f, .5f, .14f);
    } else if (type == "psychic") {
        path({{.49f, .51f}, {.57f, .48f}, {.61f, .56f}, {.54f, .65f}, {.40f, .64f}, {.32f, .51f}, {.36f, .35f}, {.54f, .29f}, {.72f, .38f}, {.76f, .58f}, {.64f, .76f}, {.42f, .79f}, {.23f, .66f}});
    } else if (type == "ghost") {
        path({{.24f, .75f}, {.25f, .43f}, {.33f, .26f}, {.52f, .20f}, {.70f, .29f}, {.76f, .47f}, {.77f, .77f}, {.62f, .67f}, {.51f, .79f}, {.39f, .67f}, {.24f, .75f}});
        ring(.40f, .46f, .025f);
        ring(.62f, .46f, .025f);
    } else if (type == "dark") path({{.63f, .22f}, {.38f, .26f}, {.24f, .46f}, {.28f, .66f}, {.48f, .79f}, {.72f, .70f}, {.54f, .65f}, {.45f, .47f}, {.50f, .31f}, {.63f, .22f}});
    else if (type == "fairy") {
        path({{.5f, .18f}, {.59f, .41f}, {.81f, .50f}, {.59f, .59f}, {.5f, .82f}, {.41f, .59f}, {.19f, .50f}, {.41f, .41f}, {.5f, .18f}});
    } else if (type == "dragon") {
        path({{.28f, .76f}, {.54f, .68f}, {.62f, .52f}, {.45f, .43f}, {.48f, .27f}, {.69f, .22f}, {.73f, .38f}, {.59f, .37f}});
        path({{.44f, .57f}, {.22f, .43f}, {.28f, .24f}, {.47f, .42f}});
    } else if (type == "fighting") path({{.29f, .75f}, {.24f, .46f}, {.32f, .37f}, {.38f, .45f}, {.38f, .27f}, {.47f, .25f}, {.50f, .43f}, {.51f, .23f}, {.60f, .25f}, {.62f, .45f}, {.65f, .30f}, {.74f, .35f}, {.74f, .62f}, {.64f, .77f}, {.29f, .75f}});
    else {
        ring(.5f, .5f, .25f);
    }
}

struct Layout {
    float x = 0, y = 0, w = 0, h = 0, scale = 1, rowH = 0, columnW = 0;
    int rowsPerColumn = 1, columns = 1;
};
inline Layout layout(int width, int height, int count) {
    Layout out;
    out.scale = std::clamp(std::min(width / 1280.0f, height / 720.0f), .6f, 1.4f);
    out.x = std::round(18 * out.scale);
    out.y = std::round(104 * out.scale);
    out.rowH = 30 * out.scale;
    out.columnW = 172 * out.scale;
    const float available = std::max(out.rowH, height * .72f - out.y - 73 * out.scale);
    out.rowsPerColumn = std::max(1, static_cast<int>(available / out.rowH));
    out.columns = std::max(1, (count + out.rowsPerColumn - 1) / out.rowsPerColumn);
    out.rowsPerColumn = std::max(1, (count + out.columns - 1) / out.columns);
    out.w = out.columnW * out.columns + 20 * out.scale;
    out.h = (out.rowsPerColumn * 30 + 73) * out.scale;
    return out;
}

template <class Rows>
inline void append(std::vector<IRenderBackend::DebugQuad> &quads,
                   std::vector<IRenderBackend::DebugLine> &lines,
                   int width, int height, const Rows &types, int benchCount) {
    if (types.empty()) return;
    const auto l = layout(width, height, static_cast<int>(types.size()));
    const float s = l.scale;
    hud_paint::panel(quads, l.x + 2 * s, l.y + 3 * s, l.w, l.h, 9 * s, {.01f, .025f, .02f}, .25f);
    hud_paint::panel(quads, l.x, l.y, l.w, l.h, 9 * s);
    hud_paint::text(lines, l.x + 12 * s, l.y + 12 * s, "TEAM TYPES", std::max(1.0f, 1.13f * s), {.90f, .85f, .63f});
    hud_paint::text(lines, l.x + 12 * s, l.y + 29 * s, "Distinct evolution lines", std::max(.68f, .81f * s), {.59f, .70f, .66f});
    for (std::size_t i = 0; i < types.size(); ++i) {
        const auto &row = types[i];
        const float x = l.x + 10 * s + static_cast<int>(i / l.rowsPerColumn) * l.columnW;
        const float y = l.y + 46 * s + static_cast<int>(i % l.rowsPerColumn) * l.rowH;
        const auto *style = styleFor(row.type);
        const glm::vec3 color = style ? style->color : glm::vec3(.5f);
        hud_paint::panel(quads, x, y, l.columnW - 2 * s, 27 * s, 4 * s, color * .16f + glm::vec3(.045f), 1);
        icon(quads, lines, row.type, x + 4 * s, y + 3 * s, 21 * s);
        hud_paint::text(lines, x + 33 * s, y + 8 * s, hud::humanizeToken(row.type), std::max(.95f, 1.06f * s));
        const std::string count = std::to_string(row.uniqueLineCount);
        const float countScale = std::max(1.0f, 1.16f * s);
        hud_paint::text(lines, x + l.columnW - 14 * s - ui_text::measureTextWidth(count, countScale), y + 7 * s, count, countScale, color * .35f + glm::vec3(.65f));
    }
    hud_paint::text(lines, l.x + 12 * s, l.y + l.h - 17 * s,
                    "Board + bench  |  Bench: " + std::to_string(benchCount), std::max(.68f, .81f * s), {.59f, .70f, .66f});
}

} // namespace game::runtime::type_roster_hud
