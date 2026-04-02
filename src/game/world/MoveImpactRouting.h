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

enum class AquaImpactStyle {
    TailWhip = 0,
    Bubble,
    WaterGun,
};

MoveImpactRoute classifyMoveImpactRoute(std::string_view moveLower) noexcept;
bool isMetalClawImpactMove(std::string_view moveLower) noexcept;
bool isTailWhipImpactMove(std::string_view moveLower) noexcept;
AquaImpactStyle classifyAquaImpactStyle(std::string_view moveLower) noexcept;
bool shouldApplyProceduralAttackLunge(std::string_view moveLower) noexcept;
