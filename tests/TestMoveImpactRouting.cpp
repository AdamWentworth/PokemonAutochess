#include <string>
#include <string_view>

#include "game/world/MoveImpactRouting.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool expectRoute(std::string_view moveLower,
                 MoveImpactRoute expectedRoute,
                 const char* label,
                 std::string& outFail) {
    const MoveImpactRoute route = classifyMoveImpactRoute(moveLower);
    if (route == expectedRoute) return true;
    outFail = std::string("Unexpected route for ") + label;
    return false;
}

}  // namespace

bool test_move_impact_routing(std::string& outFail) {
    if (!expectRoute("tackle", MoveImpactRoute::Tackle, "tackle", outFail)) return false;
    if (!expectRoute("vine_whip", MoveImpactRoute::GrassImpact, "vine_whip", outFail)) return false;
    if (!expectRoute("leech_seed", MoveImpactRoute::GrassImpact, "leech_seed", outFail)) return false;
    if (!expectRoute("growl", MoveImpactRoute::GrowlSoundRings, "growl", outFail)) return false;
    if (!expectRoute("scratch", MoveImpactRoute::ClawSwipe, "scratch", outFail)) return false;
    if (!expectRoute("metal_claw", MoveImpactRoute::ClawSwipe, "metal_claw", outFail)) return false;
    if (!expectRoute("tail_whip", MoveImpactRoute::AquaSwoosh, "tail_whip", outFail)) return false;
    if (!expectRoute("bubble", MoveImpactRoute::AquaSwoosh, "bubble", outFail)) return false;
    if (!expectRoute("water_gun", MoveImpactRoute::AquaSwoosh, "water_gun", outFail)) return false;

    if (!expectRoute("", MoveImpactRoute::None, "empty move", outFail)) return false;
    if (!expectRoute("unknown_move", MoveImpactRoute::None, "unknown move", outFail)) return false;

    if (!expect(classifyMoveImpactRoute("Growl") == MoveImpactRoute::None,
                "Route classifier expects lowercase move keys; mixed-case should not route implicitly.",
                outFail)) {
        return false;
    }

    return true;
}
