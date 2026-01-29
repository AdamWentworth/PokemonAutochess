// src/game/PokemonInstance.h
#pragma once

#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Model;

enum class PokemonSide {
    Player,
    Enemy
};


// Visual-only flight / airborne presentation (for small fliers like Pidgey).
// This is meant to improve transitions (ground idle -> takeoff -> flight -> land -> combat idle)
// without changing gameplay timing.
enum class AirLocomotionState {
    Grounded,
    TakingOff,
    Airborne,
    Landing
};

struct PokemonInstance {
    // identity
    int id = 0;
    std::string name;
    std::shared_ptr<Model> model;

    // transform (world)
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // degrees XYZ (you use rotation.y)

    PokemonSide side = PokemonSide::Player;
    bool alive = true;

    // leveling/stats
    int level = 1;

    int   baseHp = 100;
    int   baseAttack = 10;
    float baseMovementSpeed = 1.0f;

    int   hp = 100;
    int   maxHP = 100;
    int   attack = 10;
    float movementSpeed = 1.0f;

    // moves/energy
    std::string fastMove;
    std::string chargedMove;

    int energy = 0;
    int maxEnergy = 100;

    // movement interpolation (used by your Lua bindings)
    bool isMoving = false;
    glm::vec3 moveFrom{0.0f};
    glm::vec3 moveTo{0.0f};
    float moveT = 1.0f;
    glm::ivec2 committedDest{-1, -1};

    // per-instance animation time (seconds)
    float animTimeSec = 0.0f;

    // animation role indices (resolved from manifest -> model animation index)
    int animIdleIndex    = 1;
    int animMoveIndex    = 1;
    int animAttack1Index = 1;

    // current animation being played
    int activeAnimIndex = 1;

    // attack one-shot control
    float attackTimerSec    = 0.0f;
    float attackDurationSec = 0.0f; // filled from manifest (Bulbasaur)


    // --- Flight visuals (optional; visual-only) ---
    // Enabled when an animset provides takeoff+land clips, or meta movementMode="airborne".
    bool usesAirLocomotion = false;

    // Optional animation indices for flight presentation.
    int animGroundIdleIndex = 1; // defaults to animIdleIndex
    int animAirIdleIndex    = 1; // defaults to animIdleIndex
    int animTakeoffIndex    = -1;
    int animLandIndex       = -1;

    // Phase/timing
    AirLocomotionState airState = AirLocomotionState::Grounded;
    float airStateTimeSec = 0.0f;

    // Visual offsets (applied at draw-time)
    float flightHeight = 0.65f; // tuned for small birds like Pidgey
    float visualYOffset = 0.0f;

    // Optional tuning (seconds). If 0, clip duration (or a small fallback) is used.
    float takeoffSec = 0.0f;
    float landingSec = 0.0f;

    // Visual playback multipliers for one-shots
    float takeoffAnimSpeed = 1.35f;
    float landAnimSpeed    = 1.60f;

    // Internal state for movement-transition detection
    bool wasMovingLastFrame = false;

    // When an attack is triggered while airborne, we land first and then play attack1.
    bool pendingAttackAfterLanding = false;
    float queuedAttackDurationSec = 0.0f;

    static int getNextUnitID() {
        static int next = 1;
        return next++;
    }
};

