#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/core/Paths.h"

#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

bool in01(float v, float eps = 0.0001f) {
    return v >= -eps && v <= (1.0f + eps);
}

PokemonInstance makeCaptureTarget(const std::string& name,
                                  PokemonSide side,
                                  bool alive,
                                  bool fainting,
                                  bool captureInProgress,
                                  int level,
                                  int maxHp,
                                  int hp) {
    PokemonInstance unit;
    unit.id = PokemonInstance::getNextUnitID();
    unit.name = name;
    unit.side = side;
    unit.alive = alive;
    unit.fainting = fainting;
    unit.captureInProgress = captureInProgress;
    unit.level = std::max(1, level);
    unit.baseHp = std::max(1, maxHp);
    unit.maxHP = std::max(1, maxHp);
    unit.hp = std::max(0, std::min(unit.maxHP, hp));
    unit.position = glm::vec3(1.0f, 0.0f, -2.0f);
    return unit;
}

bool loadPokemonConfig(GameDataDb& db, std::string& outFail) {
    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    if (db.pokemon.loadConfig(pokemonPath, nullptr)) return true;
    outFail = "Failed to load pokemon config: " + pokemonPath;
    return false;
}

const GameWorld::CaptureAttemptRenderSnapshot* findSnapByTarget(
    const std::vector<GameWorld::CaptureAttemptRenderSnapshot>& snaps, int targetId) {
    for (const auto& s : snaps) {
        if (s.targetId == targetId) return &s;
    }
    return nullptr;
}

} // namespace

bool test_shared_capture_presentation_contract(std::string& outFail) {
    using namespace game::runtime::shared_capture;

    {
        GameWorld::CaptureAttemptRenderSnapshot snap{};
        snap.phase = 0;
        snap.absorbNorm01 = 0.5f;
        if (!expect(nearf(ballClipTimeSec(snap, 1.25f), 0.0f),
                    "ballClipTimeSec should keep the ball closed outside Absorb phase.",
                    outFail)) {
            return false;
        }

        snap.phase = 1;
        if (!expect(nearf(ballClipTimeSec(snap, 2.0f), 1.0f),
                    "ballClipTimeSec should map absorbNorm01 to clip time during Absorb phase.",
                    outFail)) {
            return false;
        }

        snap.absorbNorm01 = 2.0f;
        if (!expect(nearf(ballClipTimeSec(snap, 2.0f), 2.0f),
                    "ballClipTimeSec should clamp absorbNorm01 to clip duration.",
                    outFail)) {
            return false;
        }

        if (!expect(nearf(ballClipTimeSec(snap, 0.0f), 0.0f),
                    "ballClipTimeSec should return 0 for non-positive clip durations.",
                    outFail)) {
            return false;
        }
    }

    {
        const glm::vec3 pos(1.0f, 2.0f, 3.0f);
        const glm::mat4 m = buildBallModelMatrix(pos, 90.0f, 2.0f);
        if (!expect(glm::distance(glm::vec3(m[3]), pos) < 0.0001f,
                    "buildBallModelMatrix should preserve translation in the final matrix.",
                    outFail)) {
            return false;
        }
        if (!expect(nearf(glm::length(glm::vec3(m[0])), 2.0f) &&
                        nearf(glm::length(glm::vec3(m[1])), 2.0f) &&
                        nearf(glm::length(glm::vec3(m[2])), 2.0f),
                    "buildBallModelMatrix should apply uniform scale to all basis axes.",
                    outFail)) {
            return false;
        }
    }

    {
        game::runtime::render_model::MeshData mesh;
        if (!expect(findPokeballAnimIndex(mesh) == -1,
                    "findPokeballAnimIndex(mesh) should return -1 when no animations exist.",
                    outFail)) {
            return false;
        }

        pac_model_types::AnimationClip clipIdle;
        clipIdle.name = "Idle";
        pac_model_types::AnimationClip clipOpen;
        clipOpen.name = "Hinge_TopAction";
        pac_model_types::AnimationClip clipOther;
        clipOther.name = "Other";
        mesh.animations = {clipIdle, clipOpen, clipOther};
        if (!expect(findPokeballAnimIndex(mesh) == 1,
                    "findPokeballAnimIndex(mesh) should prefer the Hinge_TopAction clip.",
                    outFail)) {
            return false;
        }

        mesh.animations = {clipIdle, clipOther};
        if (!expect(findPokeballAnimIndex(mesh) == 0,
                    "findPokeballAnimIndex(mesh) should fall back to the first clip when Hinge_TopAction is missing.",
                    outFail)) {
            return false;
        }
    }

    {
        SnapshotCache cache;
        if (!expect(!cache.refresh(nullptr) && cache.snaps.empty() && cache.byTargetId.empty(),
                    "SnapshotCache::refresh(nullptr) should fail cleanly and clear cached state.",
                    outFail)) {
            return false;
        }

        GameConfigData cfg;
        cfg.captureMinChance = 1.0f;
        cfg.captureMaxChance = 1.0f;
        cfg.captureAttemptSec = 0.2f;

        GameWorld world(cfg);
        world.setRenderEnabled(false);

        GameDataDb db;
        if (!loadPokemonConfig(db, outFail)) return false;
        world.setData(&db);

        PokemonInstance enemy = makeCaptureTarget("bulbasaur", PokemonSide::Enemy, true, false, false, 3, 100, 50);
        const int enemyId = enemy.id;
        world.getPokemons().push_back(enemy);

        if (!expect(world.startCaptureAttempt(enemyId, 1.0f, nullptr),
                    "Expected capture attempt to start for SnapshotCache refresh contract test.",
                    outFail)) {
            return false;
        }

        if (!expect(cache.refresh(&world),
                    "SnapshotCache::refresh should succeed when capture attempts are active.",
                    outFail)) {
            return false;
        }
        const auto* snap = cache.findByTarget(enemyId);
        if (!expect(snap != nullptr && snap->targetId == enemyId,
                    "SnapshotCache::findByTarget should return the active capture snapshot by target id.",
                    outFail)) {
            return false;
        }
        if (!expect(cache.findByTarget(-9999) == nullptr,
                    "SnapshotCache::findByTarget should return nullptr for unknown targets.",
                    outFail)) {
            return false;
        }
    }

    return true;
}

bool test_gameworld_capture_render_snapshot_timing_contract(std::string& outFail) {
    GameConfigData cfg;
    cfg.captureMinChance = 1.0f;
    cfg.captureMaxChance = 1.0f;
    cfg.captureAttemptSec = 0.2f;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    GameDataDb db;
    if (!loadPokemonConfig(db, outFail)) return false;
    world.setData(&db);

    PokemonInstance enemy = makeCaptureTarget("bulbasaur", PokemonSide::Enemy, true, false, false, 3, 120, 20);
    const int enemyId = enemy.id;
    world.getPokemons().push_back(enemy);

    if (!world.startCaptureAttempt(enemyId, 1.0f, nullptr)) {
        outFail = "Expected capture attempt to start for snapshot timing contract test.";
        return false;
    }

    bool sawPhase[4] = {false, false, false, false};
    int lastPhase = -1;
    float maxAbsorbLateVisual = 0.0f;
    bool sawAbsorbEarlyZeroLate = false;
    bool sawAbsorbLateRamp = false;

    constexpr float dt = 0.01f;
    for (int step = 0; step < 600; ++step) {
        std::vector<GameWorld::CaptureAttemptRenderSnapshot> snaps;
        const bool any = world.buildCaptureAttemptRenderSnapshots(snaps);
        if (!any) {
            if (!expect(sawPhase[0] && sawPhase[1] && sawPhase[2] && sawPhase[3],
                        "Capture snapshot timing contract should expose Throw/Absorb/Shake/Resolve phases before completion.",
                        outFail)) {
                return false;
            }
            if (!expect(sawAbsorbEarlyZeroLate,
                        "absorbLateVisual01 should remain zero during early Absorb phase.",
                        outFail)) {
                return false;
            }
            if (!expect(sawAbsorbLateRamp && maxAbsorbLateVisual > 0.0f,
                        "absorbLateVisual01 should ramp above zero late in Absorb phase.",
                        outFail)) {
                return false;
            }
            return true;
        }

        const auto* snap = findSnapByTarget(snaps, enemyId);
        if (!expect(snap != nullptr,
                    "Expected active capture snapshot for target while capture attempt is in progress.",
                    outFail)) {
            return false;
        }
        if (!expect(snap->phase >= 0 && snap->phase <= 3,
                    "Capture snapshot phase should stay within [0,3].",
                    outFail)) {
            return false;
        }
        if (!expect(lastPhase <= snap->phase,
                    "Capture snapshot phase progression should be monotonic.",
                    outFail)) {
            return false;
        }
        lastPhase = snap->phase;
        sawPhase[snap->phase] = true;

        if (!expect(in01(snap->phaseNorm01),
                    "phaseNorm01 should remain normalized to [0,1].",
                    outFail)) {
            return false;
        }
        if (!expect(in01(snap->absorbNorm01) && in01(snap->absorbLateVisual01),
                    "absorbNorm01/absorbLateVisual01 should remain normalized to [0,1].",
                    outFail)) {
            return false;
        }

        if (snap->phase != 1) {
            if (!expect(nearf(snap->absorbNorm01, 0.0f) && nearf(snap->absorbLateVisual01, 0.0f),
                        "absorbNorm01 and absorbLateVisual01 should be zero outside Absorb phase.",
                        outFail)) {
                return false;
            }
        } else {
            if (!expect(nearf(snap->absorbNorm01, snap->phaseNorm01),
                        "absorbNorm01 should match phaseNorm01 during Absorb phase.",
                        outFail)) {
                return false;
            }
            if (snap->absorbNorm01 < 0.84f && nearf(snap->absorbLateVisual01, 0.0f)) {
                sawAbsorbEarlyZeroLate = true;
            }
            maxAbsorbLateVisual = std::max(maxAbsorbLateVisual, snap->absorbLateVisual01);
            if (snap->absorbNorm01 > 0.9f && snap->absorbLateVisual01 > 0.0f) {
                sawAbsorbLateRamp = true;
            }
        }

        world.update(dt);
    }

    outFail = "Capture snapshot timing contract test exceeded iteration budget.";
    return false;
}
