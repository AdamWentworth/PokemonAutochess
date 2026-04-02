#include <string>

#include "game/preview/effects/GrowlPreviewEffect.h"
#include "game/preview/effects/LeechSeedPreviewEffect.h"
#include "game/preview/effects/TacklePreviewEffect.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_preview_effect_pokemon_species_contract(std::string& outFail) {
    game::preview::GrowlPreviewEffect growl;
    const auto growlSpecies = growl.previewPokemonSpecies();
    if (!expect(growlSpecies.attackerSpecies == "charmander" &&
                    growlSpecies.targetSpecies == "bulbasaur",
                "Growl preview should keep Charmander as the caster and Bulbasaur as the target.",
                outFail)) {
        return false;
    }

    game::preview::TacklePreviewEffect tackle;
    const auto tackleSpecies = tackle.previewPokemonSpecies();
    if (!expect(tackleSpecies.attackerSpecies == "bulbasaur" &&
                    tackleSpecies.targetSpecies == "charmander",
                "Tackle preview should use Bulbasaur as the caster and Charmander as the target.",
                outFail)) {
        return false;
    }
    if (!expect(tackle.wantsExactClipMotionPreview(),
                "Tackle preview should request exact clip motion playback instead of gameplay-derived lunge motion.",
                outFail)) {
        return false;
    }

    game::preview::LeechSeedPreviewEffect leechSeed;
    const auto leechSeedSpecies = leechSeed.previewPokemonSpecies();
    if (!expect(leechSeedSpecies.attackerSpecies == "bulbasaur" &&
                    leechSeedSpecies.targetSpecies == "charmander",
                "Leech Seed preview should use Bulbasaur as the caster and Charmander as the target.",
                outFail)) {
        return false;
    }

    return true;
}
