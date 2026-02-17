#include "game/world/MoveImpactRouting.h"

MoveImpactRoute classifyMoveImpactRoute(std::string_view moveLower) noexcept {
    if (moveLower == "tackle") return MoveImpactRoute::Tackle;
    if (moveLower == "vine_whip" || moveLower == "leech_seed") return MoveImpactRoute::GrassImpact;
    if (moveLower == "growl") return MoveImpactRoute::GrowlSoundRings;
    if (moveLower == "scratch" || moveLower == "metal_claw") return MoveImpactRoute::ClawSwipe;
    if (moveLower == "tail_whip" || moveLower == "bubble" || moveLower == "water_gun") {
        return MoveImpactRoute::AquaSwoosh;
    }
    return MoveImpactRoute::None;
}
