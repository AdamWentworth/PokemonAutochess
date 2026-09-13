#include "game/runtime/ui/HudFormatting.h"
#include "game/runtime/ui/TypeRosterHud.h"
#include "game/runtime/ui/UnitDetailsHud.h"

#include <string>
#include <utility>
#include <vector>
#include <set>

bool test_ui_hud_formatting_contract(std::string& outFail) {
    namespace portraits = game::runtime::pokemon_portraits;
    std::set<int> portraitDex;
    std::set<std::string> portraitPaths;
    for (const auto &entry : portraits::entries) {
        std::vector<IRenderBackend::DebugSprite> sprites;
        portraits::append(sprites, entry.species, 10, 20, 52);
        if (!portraitDex.insert(entry.dex).second || entry.dex < 1 || entry.dex > 151 ||
            !portraitPaths.insert(portraits::path(entry)).second || portraits::find(entry.species) != &entry ||
            sprites.size() != 1 || sprites[0].w != sprites[0].h ||
            sprites[0].u0 < 0 || sprites[0].v0 < 0 || sprites[0].u1 > 1 || sprites[0].v1 > 1 ||
            sprites[0].u1 <= sprites[0].u0 || sprites[0].v1 <= sprites[0].v0 ||
            sprites[0].u1 - sprites[0].u0 != sprites[0].v1 - sprites[0].v0) {
            outFail = "Every Kanto species needs a unique HOME portrait with square, in-bounds face framing.";
            return false;
        }
    }
    if (portraitDex.size() != 151) {
        outFail = "HOME portrait coverage must include all 151 base Kanto species.";
        return false;
    }
    for (const auto &[name, dex] : {std::pair{"Bulbasaur", 1}, {"Nidoran-F", 29}, {"NidoranF", 29},
                                   {"Nidoran\xE2\x99\x80", 29}, {"Nidoran-M", 32}, {"Nidoran\xE2\x99\x82", 32},
                                   {"Farfetch'd", 83}, {"Mr. Mime", 122}, {"mr_mime", 122}, {"Mew", 151}}) {
        const auto *entry = portraits::find(name);
        if (!entry || entry->dex != dex) {
            outFail = "Portrait lookup must preserve gendered species and punctuation aliases.";
            return false;
        }
    }
    if (portraits::find("Nidoran") || portraits::find("missing") || portraits::find("")) {
        outFail = "Unknown or ambiguous names must not display a different Pokemon's portrait.";
        return false;
    }
    for (const auto &style : game::runtime::type_roster_hud::styles) {
        std::vector<IRenderBackend::DebugSprite> sprites;
        game::runtime::type_roster_hud::icon(sprites, style.id, 10, 20, 21);
        if (sprites.size() != 1 || sprites[0].texturePath != "assets/ui/types/home/" + std::string(style.id) + ".png") {
            outFail = "Every Pokemon type must use its original HOME icon asset.";
            return false;
        }
    }
    for (const auto size : {std::pair{640, 360}, std::pair{800, 600}, std::pair{845, 513}, std::pair{1920, 1080}}) {
        // The roster has no selection-dependent layout. The horizontal inspector
        // must fit above its original anchor, even at compact viewport sizes.
        const auto layout = game::runtime::type_roster_hud::layout(size.first, size.second, 18);
        const auto details = game::runtime::unit_details_hud::layout(size.first, size.second);
        if (layout.columns * layout.rowsPerColumn < 18 || layout.x + layout.w > size.first ||
            layout.y + layout.h > size.second * .80f || details.y + details.h >= layout.y ||
            layout.y != std::round(104 * layout.scale) || layout.rowH != 30 * layout.scale ||
            details.w < details.h * 5 || details.x != layout.x || details.x + details.w > size.first) {
            outFail = "The wide inspector must fit above Team Types without moving or compressing its fixed layout.";
            return false;
        }
        PokemonInstance unit;
        unit.name = "bulbasaur";
        unit.types = {"grass", "poison"};
        std::vector<IRenderBackend::DebugQuad> quads;
        std::vector<IRenderBackend::DebugLine> lines;
        std::vector<IRenderBackend::DebugSprite> sprites;
        game::runtime::unit_details_hud::append(quads, lines, sprites, size.first, size.second, unit, nullptr, false);
        if (sprites.size() != 3 || sprites[0].texturePath != "assets/ui/pokemon/home/001.png") {
            outFail = "The inspector must draw the selected portrait alongside both type icons.";
            return false;
        }
        for (const auto &sprite : sprites) {
            if (sprite.x < details.x || sprite.y < details.y || sprite.x + sprite.w > details.x + 154 * details.scale ||
                sprite.y + sprite.h > details.y + details.h) {
                outFail = "Portrait and type icons must fit the identity column at every supported HUD size.";
                return false;
            }
        }
    }
    using game::runtime::hud::formatInventoryEntry;
    using game::runtime::hud::formatShopCardEntry;
    using game::runtime::hud::formatTypeLineEntry;
    using game::runtime::hud::formatUnitEntry;
    using game::runtime::hud::humanizeToken;
    using game::runtime::hud::clampInventoryOffset;
    using game::runtime::hud::normalizeInventoryEntries;
    using game::runtime::hud::sliceInventoryEntries;
    using game::runtime::hud::stepInventoryOffset;

    if (humanizeToken("potion_super") != "Potion Super") {
        outFail = "humanizeToken should convert underscore tokens to title words";
        return false;
    }
    if (humanizeToken("LEECH-SEED") != "Leech Seed") {
        outFail = "humanizeToken should normalize case and hyphen separators";
        return false;
    }

    const std::vector<std::pair<std::string, int>> raw = {
        {"potion", 2},
        {"", 9},
        {"antidote", 0},
        {"pokeball", 5},
        {"x_speed", 5},
        {"ether", -1},
        {"super_potion", 1},
    };

    const auto normalized = normalizeInventoryEntries(raw, 3);
    if (normalized.size() != 3u) {
        outFail = "normalizeInventoryEntries should filter and truncate to maxCount";
        return false;
    }
    if (normalized[0].id != "pokeball" || normalized[0].count != 5) {
        outFail = "normalizeInventoryEntries should sort by count desc";
        return false;
    }
    if (normalized[1].id != "x_speed" || normalized[1].count != 5) {
        outFail = "normalizeInventoryEntries tie-break sort mismatch";
        return false;
    }
    if (normalized[2].id != "potion" || normalized[2].count != 2) {
        outFail = "normalizeInventoryEntries third slot mismatch";
        return false;
    }
    if (clampInventoryOffset(-2, 6, 10) != 0) {
        outFail = "clampInventoryOffset should clamp negative offsets to zero";
        return false;
    }
    if (clampInventoryOffset(99, 6, 10) != 4) {
        outFail = "clampInventoryOffset max bound mismatch";
        return false;
    }
    if (clampInventoryOffset(2, 6, 4) != 0) {
        outFail = "clampInventoryOffset should clamp to zero when visible >= total";
        return false;
    }
    if (stepInventoryOffset(1, 1, 6, 10) != 0) {
        outFail = "stepInventoryOffset wheel-up behavior mismatch";
        return false;
    }
    if (stepInventoryOffset(1, -1, 6, 10) != 2) {
        outFail = "stepInventoryOffset wheel-down behavior mismatch";
        return false;
    }
    if (stepInventoryOffset(4, -1, 6, 10) != 4) {
        outFail = "stepInventoryOffset should clamp at max offset";
        return false;
    }

    const auto normalizedAll = normalizeInventoryEntries(raw, 0);
    if (normalizedAll.size() != 4u) {
        outFail = "normalizeInventoryEntries maxCount=0 should keep all filtered entries";
        return false;
    }
    const auto sliced = sliceInventoryEntries(normalizedAll, 1, 2);
    if (sliced.size() != 2u || sliced[0].id != "x_speed" || sliced[1].id != "potion") {
        outFail = "sliceInventoryEntries result mismatch";
        return false;
    }
    if (!sliceInventoryEntries(normalizedAll, 0, 0).empty()) {
        outFail = "sliceInventoryEntries should return empty for maxVisible=0";
        return false;
    }

    if (formatInventoryEntry({ "super_potion", 4 }) != "Super Potion x4") {
        outFail = "formatInventoryEntry mismatch";
        return false;
    }
    if (formatTypeLineEntry("fire", 3) != "Fire x3") {
        outFail = "formatTypeLineEntry mismatch";
        return false;
    }
    if (formatUnitEntry("mankey", 6) != "Mankey Lv6") {
        outFail = "formatUnitEntry mismatch";
        return false;
    }
    if (formatShopCardEntry("mr_mime", 0, -2) != "Mr Mime Lv1  0g") {
        outFail = "formatShopCardEntry should clamp level/cost";
        return false;
    }

    return true;
}



