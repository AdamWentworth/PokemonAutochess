#include <string>

#include "game/PokemonInstance.h"
#include "game/scripting/LuaBindings_Internal.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

}  // namespace

bool test_combat_anim_index_cache_contract(std::string& outFail) {
    PokemonInstance unit{};
    unit.model.reset();

    // Backend/shared path can cache aliases without an OpenGL Model*.
    unit.animIndexCache["pm0004_00_00_00300_roar01"] = 7;
    if (!expect(animIndexCached(unit, "pm0004_00_00_00300_roar01.gfbanm") == 7,
                "Backend anim cache should resolve .gfbanm alias to cached base clip index.",
                outFail)) {
        return false;
    }

    unit.animIndexCache.clear();
    unit.animIndexCache["pm0001_00_00_00450_rangeattack01"] = 5;
    if (!expect(animIndexCached(unit, "PM0001_00_00_00450_RANGEATTACK01.GFBANM") == 5,
                "Backend anim cache should resolve case-insensitive clip aliases.",
                outFail)) {
        return false;
    }

    unit.animIndexCache.clear();
    if (!expect(animIndexCached(unit, "pm0001_00_00_00450_rangeattack01.gfbanm") < 0,
                "When no backend alias is cached and no Model* exists, animIndexCached should fail cleanly.",
                outFail)) {
        return false;
    }

    return true;
}

