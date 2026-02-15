// src/game/PokemonInstance.h
#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
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
//
// NOTE:
// - We DO apply a draw-time world-space Y offset (PokemonInstance::visualYOffset) for smoother lift/land visuals.
// - Animations can remain "grounded"; the offset comes from code, not the clip.
enum class AirLocomotionState {
    Grounded,
    TakingOff,
    Airborne,

    // Landing is a 3-part sequence:
    //  - LandingStart  (A): transition from flight to descending
    //  - LandingLoop   (B): looping descent (variable length)
    //  - LandingFinish (C): finalize landing / touch down
    LandingStart,
    LandingLoop,
    LandingFinish
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
    std::vector<std::string> types;
    int   baseExp = 0;
    float speciesScale = 1.0f;
    // Additional render-time correction derived from model bounds interpretation.
    // Kept separate so board-scaling logic can remain driven by speciesScale.
    float modelScaleCorrection = 1.0f;

    int   hp = 100;
    int   maxHP = 100;
    int   attack = 10;
    float movementSpeed = 1.0f;

    // moves/energy
    std::string fastMove;
    std::string chargedMove;

    int energy = 0;
    int maxEnergy = 100;
    int xp = 0;

    // Fainting / removal visuals
    bool  fainting = false;
    float faintTimerSec = 0.0f;
    float faintAnimDurationSec = 0.0f;
    int   animFaintIndex = -1;
    float fadeOutSec = 0.0f;
    float fadeOutTimerSec = 0.0f;
    float visualScale = 1.0f;

    // Capture attempt in progress (temporarily removed from combat, tile still reserved)
    bool  captureInProgress = false;
    float captureScale = 1.0f;
    float captureTintStrength = 0.0f;

    // movement interpolation (used by your Lua bindings)
    bool isMoving = false;
    glm::vec3 moveFrom{0.0f};
    glm::vec3 moveTo{0.0f};
    float moveT = 1.0f;
    glm::ivec2 committedDest{-1, -1};

    // per-instance animation time (seconds)
    float animTimeSec = 0.0f;

    // Animation sampling FPS (from .animset.json top-level \"fps\").
    float animFps = 24.0f;

    // animation role indices (resolved from manifest -> model animation index)
    int animIdleIndex    = 1;
    int animMoveIndex    = 1;
    int animAttack1Index = 1;

    // current animation being played
    int activeAnimIndex = 1;

    // attack one-shot control
    float attackTimerSec    = 0.0f;
    float attackDurationSec = 0.0f; // filled from manifest / animset


    // Pending damage scheduled to land at a specific point in the attack animation.
    bool  pendingDamageActive     = false;
    bool  pendingDamageApplied    = false;
    int   pendingDamageTargetId   = -1;
    int   pendingDamageAmount     = 0;
    float pendingDamageHitTimeSec = 0.0f; // clip-time seconds (compared against animTimeSec)
    std::string pendingDamageMoveName;
    bool  pendingDamageIsGrass    = false;
    bool  pendingDamageIsTackle   = false;

    // Pending projectile (leech seed) spawned during attack animation.
    bool  pendingProjectileActive = false;
    bool  pendingProjectileSpawned = false;
    int   pendingProjectileTargetId = -1;
    float pendingProjectileSpawnTimeSec = 0.0f; // clip-time seconds
    float pendingProjectileTravelSec = 0.0f;    // real-time seconds

    // Pending impact event (for leech seed, non-damage impacts, etc.)
    bool  pendingImpactActive = false;
    bool  pendingImpactApplied = false;
    int   pendingImpactTargetId = -1;
    float pendingImpactTimeSec = 0.0f; // clip-time seconds
    bool  pendingImpactIsGrass = false;
    bool  pendingImpactIsLeechSeed = false;

    // Leech seed status (sapping over time)
    bool  leechSeeded = false;
    int   leechSeedSourceId = -1;
    float leechSeedTimeLeftSec = 0.0f;
    float leechSeedTickTimerSec = 0.0f;

    // Which clip to play during the current attack window (defaults to animAttack1Index).
    int currentAttackAnimIndex = -1;

    // Cache of animation indices resolved by clip name (avoids repeated linear searches).
    std::unordered_map<std::string, int> animIndexCache;

    // Generic fast-move chaining (for start/loop/end style moves).
    std::string chainedFastMove;     // lower-case move name
    float fastChainTimerSec = 0.0f;  // if >0 and chainedFastMove matches, treat as "continuing"

    // Airborne queued attack can request a specific anim index.
    int queuedAttackAnimIndex = -1;


    // --- Flight visuals (optional; visual-only) ---
    // Enabled when an animset provides takeoff+landing clips, or meta movementMode="airborne".
    bool usesAirLocomotion = false;

    // Optional animation indices for flight presentation.
    int animGroundIdleIndex = 1; // defaults to animIdleIndex
    int animAirIdleIndex    = 1; // defaults to animIdleIndex
    int animTakeoffIndex    = -1;

    // Back-compat: single landing one-shot (older animsets).
    int animLandIndex       = -1;

    // Preferred landing sequence (Pidgey):
    // A = start descending, B = looping descent, C = finish landing.
    int animLandAIndex      = -1;
    int animLandBIndex      = -1;
    int animLandCIndex      = -1;

    // Phase/timing
    AirLocomotionState airState = AirLocomotionState::Grounded;
    float airStateTimeSec = 0.0f; // "animation-time" accumulator (scaled by playback speed)

    // Visual offsets (applied at draw-time; world-space).
    // FlightLocomotion drives this. 0 disables lift visuals.
    float visualYOffset = 0.0f;

    // Target hover height (world units) when airborne.
    // Recommended: set via animset meta "airLiftY". If 0, no code-driven lift is applied.
    float airLiftY = 0.0f;

    // Optional tuning (seconds). If 0, clip duration (or a small fallback) is used.
    // These are in "animation seconds" (i.e. clip-time), and are still affected by takeoff/land speed multipliers.
    float takeoffSec = 0.0f;

    // Total landing sequence duration in clip-time (A + B + C). If 0, we default to A + one loop of B + C
    // when A/B/C exist, otherwise fall back to single-land duration.
    float landingSec = 0.0f;

    // Visual playback multipliers for one-shots
    float takeoffAnimSpeed = 1.65f;
    float landAnimSpeed    = 1.90f;

    // Runtime override computed at landing start so the full landing sequence (A->B->C)
    // can take the same real-time as takeoff (even though it is multiple clips).
    // If <= 0, FlightLocomotion uses landAnimSpeed.
    float landAnimSpeedOverride = -1.0f;

    // Optional: speed multiplier for the attack animation itself (visual-only).
    float attackAnimSpeed  = 1.00f;
    // Landing-loop (B) target duration in clip-time (computed when landing begins).
    float landingLoopTargetSec = 0.0f;

    // Internal state for movement-transition detection
    bool wasMovingLastFrame = false;

    // When an attack is triggered while airborne, we land first and then play attack1.
    bool pendingAttackAfterLanding = false;
    float queuedAttackDurationSec = 0.0f;

    // Debug logs for animation resolution + locomotion transitions.
    bool debugAnimLogs = false;

    static int getNextUnitID() {
        static int next = 1;
        return next++;
    }
};
