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

bool isMetalClawImpactMove(std::string_view moveLower) noexcept {
    return moveLower == "metal_claw";
}

bool isTailWhipImpactMove(std::string_view moveLower) noexcept {
    return moveLower == "tail_whip";
}

AquaImpactStyle classifyAquaImpactStyle(std::string_view moveLower) noexcept {
    if (moveLower == "bubble") return AquaImpactStyle::Bubble;
    if (moveLower == "water_gun") return AquaImpactStyle::WaterGun;
    return AquaImpactStyle::TailWhip;
}
