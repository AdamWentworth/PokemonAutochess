#include <string>
#include <unordered_set>
#include <vector>

#include "engine/core/Random.h"
#include "engine/core/Services.h"
#include "engine/core/TimeSources.h"
#include "engine/core/ecs/World.h"

#include "game/systems/ShopSystem.h"
#include "game/systems/RoundPhase.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool offersEqual(const std::vector<ShopSystem::ShopOffer>& a,
                 const std::vector<ShopSystem::ShopOffer>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].pokemonName != b[i].pokemonName) return false;
        if (a[i].level != b[i].level) return false;
        if (a[i].cost != b[i].cost) return false;
    }
    return true;
}

} // namespace

bool test_shop_system_phase_contract(std::string& outFail) {
    engine::XorShift32 rngA(77u);
    engine::XorShift32 rngB(77u);
    ShopSystem shopA(rngA);
    ShopSystem shopB(rngB);

    if (!expect(!shopA.isVisible(), "Shop should start hidden.", outFail)) return false;
    if (!expect(shopA.offers().empty(), "Shop should start with no offers.", outFail)) return false;
    if (!expect(!shopA.reroll(), "reroll should fail while shop is hidden.", outFail)) return false;

    shopA.onRoundPhaseChanged(RoundPhase::Resolution, RoundPhase::Planning);
    shopB.onRoundPhaseChanged(RoundPhase::Resolution, RoundPhase::Planning);

    if (!expect(shopA.isVisible(), "Shop should be visible during Planning phase.", outFail)) return false;
    if (!expect(shopA.offers().size() == 3, "Planning entry should generate 3 offers.", outFail)) return false;
    if (!expect(offersEqual(shopA.offers(), shopB.offers()),
                "Shop rolls should be deterministic for equal RNG seeds.", outFail)) return false;

    const std::unordered_set<std::string> allowed = {
        "bulbasaur", "charmander", "squirtle", "pidgey", "rattata"
    };
    for (const auto& offer : shopA.offers()) {
        if (!expect(!offer.pokemonName.empty(), "Offer name should never be empty.", outFail)) return false;
        if (!expect(allowed.count(offer.pokemonName) == 1, "Offer name should come from fallback pool.", outFail)) return false;
        if (!expect(offer.level == 1, "Fallback offers should default to level 1.", outFail)) return false;
        if (!expect(offer.cost == 0, "Fallback offers should default to zero cost.", outFail)) return false;
    }

    if (!expect(shopA.reroll(), "reroll should succeed while Planning phase is visible.", outFail)) return false;
    if (!expect(shopB.reroll(), "reroll should succeed while Planning phase is visible (control).", outFail)) return false;
    if (!expect(shopA.offers().size() == 3, "reroll should preserve slot count.", outFail)) return false;
    if (!expect(offersEqual(shopA.offers(), shopB.offers()),
                "reroll sequence should remain deterministic for equal seeds.", outFail)) return false;

    engine::ManualTimeSource time;
    engine::CoreServices core;
    core.rng = &rngA;
    core.time = &time;
    engine::ecs::World world(&core);
    const auto offersBeforeUpdate = shopA.offers();
    shopA.update(world, 1.0f / 60.0f);
    if (!expect(offersEqual(offersBeforeUpdate, shopA.offers()),
                "update should not mutate offers for phase-driven shop system.", outFail)) return false;

    shopA.onRoundPhaseChanged(RoundPhase::Planning, RoundPhase::Battle);
    if (!expect(!shopA.isVisible(), "Shop should hide outside Planning phase.", outFail)) return false;
    if (!expect(shopA.offers().empty(), "Hiding shop should clear offers.", outFail)) return false;
    if (!expect(!shopA.reroll(), "reroll should fail again once hidden.", outFail)) return false;

    return true;
}
