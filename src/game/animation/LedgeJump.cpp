#include "game/animation/LedgeJump.h"
#include "game/PokemonInstance.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <cmath>

namespace {
float duration(const PokemonInstance &unit, int index) {
    if (index < 0) return 0.0f;
    if (unit.model) return unit.model->getAnimationDurationSec(index);
    return static_cast<std::size_t>(index) < unit.backendAnimDurationsSec.size()
               ? unit.backendAnimDurationsSec[index]
               : 0.0f;
}
float phaseDuration(float clip, float fallback) {
    return std::isfinite(clip) && clip > 0.0001f ? clip : fallback;
}
void sample(PokemonInstance &unit) {
    const auto &jump = unit.ledgeJump;
    int clip = -1;
    if (jump.phase == LedgeJumpPhase::Start) {
        unit.position = unit.moveFrom;
        unit.moveT = 0.0f;
        clip = unit.animJumpStartIndex;
    } else if (jump.phase == LedgeJumpPhase::Airborne) {
        const float t = std::clamp(jump.elapsedSec / jump.airborneSec, 0.0f, 1.0f);
        unit.moveT = t;
        unit.position = glm::mix(unit.moveFrom, unit.moveTo, t);
        // Stay above the upper shelf until crossing its edge, then descend.
        unit.position.y = unit.moveFrom.y + (unit.moveTo.y - unit.moveFrom.y) * t * t +
                          4.0f * jump.arcHeight * t * (1.0f - t);
        clip = unit.animJumpLoopIndex;
    } else if (jump.phase == LedgeJumpPhase::Landing) {
        unit.position = unit.moveTo;
        unit.moveT = 1.0f;
        clip = unit.animJumpLandIndex;
    }
    // A missing role uses a stationary pose; it never changes traversal rights
    // or leaves the unit waiting forever for a clip-completion callback.
    if (clip < 0) clip = unit.animIdleIndex;
    unit.activeAnimIndex = clip;
    const float authoredSeconds = jump.phase == LedgeJumpPhase::Start ? unit.jumpStartDurationSec : jump.phase == LedgeJumpPhase::Airborne ? unit.jumpLoopDurationSec
                                                                                                                                           : unit.jumpLandDurationSec;
    const float seconds = phaseDuration(authoredSeconds, duration(unit, clip));
    unit.animTimeSec = seconds > 0.0001f
                           ? (jump.phase == LedgeJumpPhase::Airborne && unit.animJumpLoopIndex >= 0
                                  ? std::fmod(jump.elapsedSec, seconds)
                                  : std::min(jump.elapsedSec, seconds - 0.0001f))
                           : 0.0f;
}
} // namespace

namespace LedgeJump {
void begin(PokemonInstance &unit, float cellSize) {
    auto &jump = unit.ledgeJump;
    jump = {};
    jump.phase = LedgeJumpPhase::Start;
    jump.startSec = phaseDuration(unit.jumpStartDurationSec, phaseDuration(duration(unit, unit.animJumpStartIndex), 0.15f));
    jump.landingSec = phaseDuration(unit.jumpLandDurationSec, phaseDuration(duration(unit, unit.animJumpLandIndex), 0.20f));
    jump.airborneSec = std::max(0.20f, 1.0f / std::max(0.01f, unit.movementSpeed));
    jump.arcHeight = std::max(0.20f * cellSize, (unit.moveFrom.y - unit.moveTo.y) * 0.4f);
    unit.visualYOffset = 0.0f;
    sample(unit);
}

bool advance(PokemonInstance &unit, float dt) {
    auto &jump = unit.ledgeJump;
    if (!jump.active()) return false;
    float remaining = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    while (jump.active()) {
        const float seconds = jump.phase == LedgeJumpPhase::Start ? jump.startSec : jump.phase == LedgeJumpPhase::Airborne ? jump.airborneSec
                                                                                                                           : jump.landingSec;
        const float consumed = std::min(remaining, std::max(0.0f, seconds - jump.elapsedSec));
        jump.elapsedSec += consumed;
        remaining -= consumed;
        if (jump.elapsedSec + 0.000001f < seconds) break;
        jump.elapsedSec = 0.0f;
        if (jump.phase == LedgeJumpPhase::Start) jump.phase = LedgeJumpPhase::Airborne;
        else if (jump.phase == LedgeJumpPhase::Airborne) jump.phase = LedgeJumpPhase::Landing;
        else {
            jump = {};
            unit.position = unit.moveTo;
            unit.moveT = 1.0f;
            unit.activeAnimIndex = unit.animIdleIndex;
            unit.animTimeSec = 0.0f;
            return true;
        }
    }
    sample(unit);
    return false;
}
} // namespace LedgeJump
