#pragma once

#include <string_view>

enum class MoveImpactRoute {
    None = 0,
    Tackle,
    GrassImpact,
    GrowlSoundRings,
    ClawSwipe,
    AquaSwoosh,
};

MoveImpactRoute classifyMoveImpactRoute(std::string_view moveLower) noexcept;
