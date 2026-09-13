#pragma once

#include "game/runtime/ui/TypeRosterHud.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include <iomanip>
#include <sstream>

namespace game::runtime::unit_details_hud {

struct Layout {
    float x, y, w, h, scale;
    bool contains(int px, int py) const {
        const float sx = static_cast<float>(px), sy = static_cast<float>(py);
        return sx >= x && sy >= y && sx < x + w && sy < y + h;
    }
};
inline Layout layout(int width, int height) {
    const float s = std::clamp(std::min(width / 1280.0f, height / 720.0f), .5f, 1.4f);
    const auto types = type_roster_hud::layout(width, height, 0);
    return {types.x, std::round(14 * s), 520 * s, 80 * s, s};
}
inline std::string decimal(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}
inline float fraction(int value, int maximum) {
    return maximum > 0 ? std::clamp(static_cast<float>(value) / maximum, 0.0f, 1.0f) : 0.0f;
}

inline void append(std::vector<IRenderBackend::DebugQuad> &quads,
                   std::vector<IRenderBackend::DebugLine> &lines,
                   std::vector<IRenderBackend::DebugSprite> &sprites,
                   int width, int height, const PokemonInstance &unit,
                   const GameDataDb *data, bool onBench) {
    const auto l = layout(width, height);
    const float s = l.scale;
    hud_paint::panel(quads, l.x, l.y, l.w, l.h, 9 * s);
    const auto text = [&](float x, float y, const std::string &value, float scale = 1.0f,
                          glm::vec3 color = {.93f, .96f, .91f}, float space = 182) {
        const float requested = std::max(.78f, scale * s);
        const float fit = space * s / std::max(1.0f, ui_text::measureTextWidth(value, 1));
        hud_paint::text(lines, l.x + x * s, l.y + y * s, value, std::min(requested, fit), color);
    };
    for (float x : {154.0f, 314.0f})
        hud_paint::quad(quads, l.x + x * s, l.y + 11 * s, s, 58 * s, {.20f, .30f, .25f});

    text(12, 10, hud::humanizeToken(unit.name), 1.4f, {.97f, .91f, .69f}, 132);
    for (std::size_t i = 0; i < std::min<std::size_t>(2, unit.types.size()); ++i) {
        const float x = 12 + static_cast<float>(i) * 70;
        type_roster_hud::icon(sprites, unit.types[i], l.x + x * s, l.y + 29 * s, 16 * s);
        text(x + 20, 34, hud::humanizeToken(unit.types[i]), .82f, {.86f, .92f, .88f}, 46);
    }
    text(12, 51, "Lv " + std::to_string(unit.level) + " | XP " + std::to_string(unit.xp), .9f, {.97f, .91f, .69f}, 132);
    const std::string place = unit.side == PokemonSide::Enemy ? "Enemy" : (onBench ? "Your bench" : "Your board");
    text(12, 65, place + (unit.leechSeeded ? " | Seeded" : ""), .85f, {.62f, .76f, .70f}, 132);

    const auto bar = [&](float y, int value, int maximum, const std::string &label, glm::vec3 color) {
        hud_paint::quad(quads, l.x + 166 * s, l.y + y * s, 136 * s, 14 * s, {.12f, .17f, .16f});
        const float fill = fraction(value, maximum);
        if (fill > 0) hud_paint::quad(quads, l.x + 166 * s, l.y + y * s, 136 * s * fill, 14 * s, color);
        text(171, y + 3, label + " " + std::to_string(value) + " / " + std::to_string(maximum), .85f, {.93f, .96f, .91f}, 126);
    };
    bar(11, unit.hp, unit.maxHP, "HP", {.20f, .48f, .28f});
    bar(31, unit.energy, unit.maxEnergy, "Energy", {.17f, .38f, .63f});
    text(166, 53, "Attack " + std::to_string(unit.attack), .94f, {.94f, .89f, .70f}, 136);
    text(166, 66, "Speed " + decimal(unit.movementSpeed) + " tiles/s", .85f, {.84f, .91f, .86f}, 136);

    const auto move = [&](float y, const std::string &name, bool charged) {
        const auto *info = data ? data->moves.getMove(name) : nullptr;
        text(326, y, std::string(charged ? "Charged: " : "Fast: ") + (name.empty() ? "None" : hud::humanizeToken(name)), 1.0f);
        if (!info) return;
        const std::string details = "Power " + std::to_string(info->power) + " | Range " + decimal(info->range) +
                                    " | " + (charged ? "-" : "+") + std::to_string(charged ? info->energyCost : info->energyGain) + " energy";
        text(326, y + 15, details, .85f, {.62f, .76f, .70f});
    };
    move(10, unit.fastMove, false);
    move(46, unit.chargedMove, true);
}

} // namespace game::runtime::unit_details_hud
