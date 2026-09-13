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
    const float s = std::clamp(std::min(width / 1280.0f, height / 720.0f), .6f, 1.4f);
    return {std::round(18 * s), std::round(58 * s), 260 * s, 220 * s, s};
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
                          glm::vec3 color = {.93f, .96f, .91f}, float space = 236) {
        const float requested = std::max(.78f, scale * s);
        const float fit = space * s / std::max(1.0f, ui_text::measureTextWidth(value, 1));
        hud_paint::text(lines, l.x + x * s, l.y + y * s, value, std::min(requested, fit), color);
    };
    text(12, 11, hud::humanizeToken(unit.name), 1.6f, {.97f, .91f, .69f}, 178);
    text(201, 13, "Lv " + std::to_string(unit.level), 1.1f, {.97f, .91f, .69f}, 48);
    for (std::size_t i = 0; i < std::min<std::size_t>(2, unit.types.size()); ++i) {
        const float x = 12 + static_cast<float>(i) * 119;
        type_roster_hud::icon(sprites, unit.types[i], l.x + x * s, l.y + 32 * s, 18 * s);
        text(x + 23, 37, hud::humanizeToken(unit.types[i]), .98f, {.86f, .92f, .88f}, 89);
    }
    const auto bar = [&](float y, int value, int maximum, const std::string &label, glm::vec3 color) {
        hud_paint::quad(quads, l.x + 12 * s, l.y + y * s, 236 * s, 14 * s, {.12f, .17f, .16f});
        const float fill = fraction(value, maximum);
        if (fill > 0) hud_paint::quad(quads, l.x + 12 * s, l.y + y * s, 236 * s * fill, 14 * s, color);
        text(18, y + 3, label + " " + std::to_string(value) + " / " + std::to_string(maximum), .92f);
    };
    bar(58, unit.hp, unit.maxHP, "HP", {.20f, .48f, .28f});
    bar(78, unit.energy, unit.maxEnergy, "Energy", {.17f, .38f, .63f});
    text(12, 103, "Attack " + std::to_string(unit.attack), 1.05f, {.94f, .89f, .70f}, 95);
    text(112, 103, "Move " + decimal(unit.movementSpeed) + " tiles/s", .98f, {.84f, .91f, .86f}, 136);
    const auto move = [&](float y, const std::string &name, bool charged) {
        const auto *info = data ? data->moves.getMove(name) : nullptr;
        text(12, y, std::string(charged ? "Charged: " : "Fast: ") + (name.empty() ? "None" : hud::humanizeToken(name)), 1.02f);
        if (!info) return;
        const std::string details = "Power " + std::to_string(info->power) + " | Range " + decimal(info->range) +
                                    " | " + (charged ? "-" : "+") + std::to_string(charged ? info->energyCost : info->energyGain) + " energy";
        text(12, y + 15, details, .85f, {.62f, .76f, .70f});
    };
    move(124, unit.fastMove, false);
    move(160, unit.chargedMove, true);
    const std::string place = unit.side == PokemonSide::Enemy ? "Enemy" : (onBench ? "Your bench" : "Your board");
    text(12, 202, place + " | XP " + std::to_string(unit.xp) + (unit.leechSeeded ? " | Seeded" : ""), .87f, {.62f, .76f, .70f});
}

} // namespace game::runtime::unit_details_hud
