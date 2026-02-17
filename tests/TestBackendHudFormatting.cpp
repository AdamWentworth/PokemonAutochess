#include "game/runtime/BackendHudFormatting.h"

#include <string>
#include <utility>
#include <vector>

bool test_backend_hud_formatting_contract(std::string& outFail) {
    using game::runtime::hud::formatInventoryEntry;
    using game::runtime::hud::formatShopCardEntry;
    using game::runtime::hud::formatTypeLineEntry;
    using game::runtime::hud::formatUnitEntry;
    using game::runtime::hud::humanizeToken;
    using game::runtime::hud::normalizeInventoryEntries;

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
