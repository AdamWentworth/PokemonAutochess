#include "game/runtime/ui/HudFormatting.h"
#include "game/runtime/ui/TypeRosterHud.h"
#include "game/runtime/ui/UnitDetailsHud.h"

#include <string>
#include <utility>
#include <vector>

bool test_ui_hud_formatting_contract(std::string& outFail) {
    for (const auto &style : game::runtime::type_roster_hud::styles) {
        std::vector<IRenderBackend::DebugSprite> sprites;
        game::runtime::type_roster_hud::icon(sprites, style.id, 10, 20, 21);
        if (sprites.size() != 1 || sprites[0].texturePath != "assets/ui/types/home/" + std::string(style.id) + ".png") {
            outFail = "Every Pokemon type must use its original HOME icon asset.";
            return false;
        }
    }
    for (const auto size : {std::pair{640, 360}, std::pair{845, 513}, std::pair{1920, 1080}}) {
        for (bool inspected : {false, true}) {
            const auto layout = game::runtime::type_roster_hud::layout(size.first, size.second, 18, inspected);
            const auto details = game::runtime::unit_details_hud::layout(size.first, size.second);
            if (layout.columns * layout.rowsPerColumn < 18 || layout.x + layout.w > size.first ||
                layout.y + layout.h > size.second * .80f || (inspected && details.y + details.h >= layout.y)) {
                outFail = "Selected-unit stats and all represented types must fit above the shop without overlapping.";
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



