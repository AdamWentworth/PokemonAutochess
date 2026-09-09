#pragma once

struct PokemonInstance;

enum class LedgeJumpPhase { None,
                            Start,
                            Airborne,
                            Landing };

struct LedgeJumpState {
    LedgeJumpPhase phase = LedgeJumpPhase::None;
    float elapsedSec = 0.0f;
    float startSec = 0.0f;
    float airborneSec = 0.0f;
    float landingSec = 0.0f;
    float arcHeight = 0.0f;
    bool active() const { return phase != LedgeJumpPhase::None; }
};

namespace LedgeJump {
void begin(PokemonInstance &unit, float cellSize);
// Owns both movement and clip clocks. Returns true only after landing finishes.
bool advance(PokemonInstance &unit, float dt);
} // namespace LedgeJump
