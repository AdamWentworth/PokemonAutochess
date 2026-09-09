// MovementSystem.cpp
#include "MovementSystem.h"

#include "engine/core/EngineServices.h"
#include "engine/core/ecs/World.h"
#include "engine/render/Model.h"
#include "game/PhaseState.h"
#include "game/animation/FlightLocomotion.h"
#include "game/logging/DebugTrace.h"
#include "game/logging/LoggerUtil.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr float kInfCost = std::numeric_limits<float>::max();

bool isCombatActive(const PokemonInstance& unit) {
    return unit.alive && !unit.captureInProgress;
}

bool setFacingToTarget(PokemonInstance& unit, const glm::vec3& targetPos) {
    const glm::vec3 delta = targetPos - unit.position;
    const float lenSq = glm::dot(delta, delta);
    if (lenSq <= 1e-8f) return false;

    const glm::vec3 lookDir = delta / std::sqrt(lenSq);
    constexpr float kRadToDeg = 57.29577951308232f;
    unit.rotation.y = std::atan2(lookDir.x, lookDir.z) * kRadToDeg;
    return true;
}

glm::ivec2 worldCell(const GameConfigData& cfg, const glm::vec3& pos) {
    const float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    const float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    const int col = static_cast<int>(std::round((pos.x - boardOriginX) / cfg.cellSize));
    const int row = static_cast<int>(std::round((pos.z - boardOriginZ) / cfg.cellSize));
    return {col, row};
}

bool hasCommittedMove(const PokemonInstance& unit) {
    return unit.committedDest.x >= 0 && unit.committedDest.y >= 0;
}

bool shouldHoldLocomotionAfterArrival(const GameWorld &world,
                                      const std::vector<PokemonInstance> &units,
                                      const PokemonInstance &unit) {
    const auto map = world.combatMap();
    const auto actor = world.combatActor(unit);
    bool foundEnemy = false;
    for (const auto& other : units) {
        if (other.id == unit.id || other.side == unit.side || !isCombatActive(other)) continue;
        const auto target = world.combatActor(other);
        if (!map.canPerceive(actor, target)) continue;
        foundEnemy = true;
        if (map.canEngageMelee(actor, target)) return false;
    }
    return foundEnemy;
}

const char* sideName(PokemonSide side) {
    return side == PokemonSide::Player ? "player" : "enemy";
}

std::string_view traceMoveName(const PokemonInstance& unit) {
    if (!unit.activeAttackMoveName.empty()) return unit.activeAttackMoveName;
    if (!unit.pendingDamageMoveName.empty()) return unit.pendingDamageMoveName;
    if (!unit.chainedFastMove.empty()) return unit.chainedFastMove;
    if (!unit.fastMove.empty()) return unit.fastMove;
    return {};
}

std::string traceMoveLabel(const PokemonInstance& unit) {
    const std::string_view move = traceMoveName(unit);
    return move.empty() ? std::string("-") : std::string(move);
}

bool shouldTraceAnim(const EngineServices* services, const PokemonInstance& unit) {
    if (services && services->terminalLogMode == EngineTerminalLogMode::AnimationDecision) {
        return true;
    }
    return DebugTrace::anim(unit.name, traceMoveName(unit));
}

float animationDurationSec(const PokemonInstance& unit, int animIndex) {
    if (animIndex < 0) return 0.0f;
    if (unit.model) return unit.model->getAnimationDurationSec(animIndex);
    if (static_cast<std::size_t>(animIndex) < unit.backendAnimDurationsSec.size()) {
        return std::max(
            0.0f,
            unit.backendAnimDurationsSec[static_cast<std::size_t>(animIndex)]);
    }
    return 0.0f;
}

std::string animationName(const PokemonInstance& unit, int animIndex) {
    if (animIndex < 0) return "-";
    if (unit.model) {
        const std::string& name = unit.model->getAnimationName(animIndex);
        if (!name.empty()) return name;
    }
    return "-";
}

const char* animationRole(const PokemonInstance& unit, int animIndex) {
    if (animIndex < 0) return "none";
    if (animIndex == unit.currentAttackAnimIndex && unit.attackTimerSec > 0.0f) return "attack_current";
    if (animIndex == unit.animAttack1Index) return "attack1";
    if (animIndex == unit.animMoveIndex) return "move";
    if (animIndex == unit.animGroundIdleIndex && unit.usesAirLocomotion) return "ground_idle";
    if (animIndex == unit.animAirIdleIndex && unit.usesAirLocomotion) return "air_idle";
    if (animIndex == unit.animIdleIndex) return "idle";
    if (animIndex == unit.animTakeoffIndex) return "takeoff";
    if (animIndex == unit.animTakeoffLoopIndex) return "takeoff_loop";
    if (animIndex == unit.animLandAIndex) return "land_a";
    if (animIndex == unit.animLandBIndex) return "land_b";
    if (animIndex == unit.animLandCIndex) return "land_c";
    if (animIndex == unit.animLandIndex) return "land";
    if (animIndex == unit.animFaintIndex) return "faint";
    return "custom";
}

void emitAnimTrace(LogBus::Logger* logger,
                   std::string_view stage,
                   const PokemonInstance& unit,
                   const std::string& details) {
    game::log::infoTerminalOnly(
        logger,
        std::string("[AnimTrace][") + std::string(stage) + "] " +
            "id=" + std::to_string(unit.id) +
            " name=" + unit.name +
            " side=" + sideName(unit.side) +
            " move=" + traceMoveLabel(unit) +
            " " + details);
}

std::string cellString(int col, int row) {
    return std::to_string(col) + "," + std::to_string(row);
}

std::string vecString(const glm::vec3& v) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << v.x << "," << v.y << "," << v.z;
    return out.str();
}

int cellIndex(const GameConfigData& cfg, int col, int row) {
    return row * cfg.cols + col;
}

bool inside(const GameConfigData& cfg, int col, int row) {
    return col >= 0 && col < cfg.cols && row >= 0 && row < cfg.rows;
}

struct PlannerUnit {
    PokemonInstance* unit = nullptr;
    int col = 0;
    int row = 0;
    float speed = 0.0f;
    game::arena::Actor target;
    int enemyCol = -1;
    int enemyRow = -1;
    bool adjacentToEnemy = false;
    float dist = kInfCost;
};

bool betterPriority(const PlannerUnit& a, const PlannerUnit& b) {
    if (a.dist != b.dist) return a.dist < b.dist;
    if (a.speed != b.speed) return a.speed > b.speed;
    return a.unit->id < b.unit->id;
}

}  // namespace

MovementSystem::MovementSystem(GameWorld* world,
                               GameServices& svc,
                               engine::ecs::Entity combatEntity_)
    : gameWorld(world), services(svc), combatEntity(combatEntity_) {}

MovementSystem::~MovementSystem() = default;

void MovementSystem::update(engine::ecs::World& ecsWorld, float deltaTime) {
    using Clock = std::chrono::high_resolution_clock;

    if (!ecsWorld.alive(combatEntity)) return;
    auto* combat = ecsWorld.get<game::CombatActive>(combatEntity);
    if (!combat || !combat->active) return;
    if (!gameWorld || gameWorld->isBoardResizePauseActive()) return;

    EngineFixedPerfBreakdown* fixedBreakdown =
        services.engineServices ? &services.engineServices->frameFixedBreakdown : nullptr;

    const auto& cfg = gameWorld->getConfig();
    const auto map = gameWorld->combatMap();
    auto& boardUnits = gameWorld->getPokemons();
    const int totalCells = cfg.cols * cfg.rows;

    const auto planStart = Clock::now();

    struct CachedUnit {
        int col = 0;
        int row = 0;
        PokemonSide side = PokemonSide::Player;
        float speed = 0.0f;
        bool active = false;
        bool blocksTile = false;
    };

    std::vector<CachedUnit> cached;
    cached.reserve(boardUnits.size());

    std::vector<std::uint8_t> blocked(totalCells, 0u);

    for (const auto& unit : boardUnits) {
        CachedUnit item;
        const glm::ivec2 cell = gameWorld->worldToGrid(unit.position);
        item.col = cell.x;
        item.row = cell.y;
        item.side = unit.side;
        item.speed = unit.movementSpeed;
        item.active = isCombatActive(unit);
        item.blocksTile = item.active || unit.captureInProgress || (unit.fainting && cfg.faintBlockTiles);
        cached.push_back(item);

        if (!item.blocksTile) continue;
        if (!inside(cfg, item.col, item.row)) continue;
        const int idx = cellIndex(cfg, item.col, item.row);
        blocked[idx] = 1u;
        if (unit.isMoving && hasCommittedMove(unit)) {
            const auto origin = gameWorld->worldToGrid(unit.moveFrom);
            game::arena::reserveStep(map, {origin.x, origin.y}, {unit.committedDest.x, unit.committedDest.y}, blocked);
        }
    }

    std::vector<PlannerUnit> units;
    units.reserve(boardUnits.size());
    for (std::size_t i = 0; i < boardUnits.size(); ++i) {
        if (!cached[i].active || !inside(cfg, cached[i].col, cached[i].row)) continue;

        PlannerUnit entry;
        entry.unit = &boardUnits[i];
        entry.col = cached[i].col;
        entry.row = cached[i].row;
        entry.speed = cached[i].speed;

        int bestDistance = std::numeric_limits<int>::max();
        int bestEnemyId = std::numeric_limits<int>::max();
        for (std::size_t j = 0; j < boardUnits.size(); ++j) {
            if (i == j) continue;
            if (!cached[j].active || cached[j].side == cached[i].side) continue;
            if (!map.canPerceive(gameWorld->combatActor(boardUnits[i]), gameWorld->combatActor(boardUnits[j]))) continue;

            const int dx = std::abs(entry.col - cached[j].col);
            const int dy = std::abs(entry.row - cached[j].row);
            const int dist = std::max(dx, dy);
            if (dist < bestDistance || (dist == bestDistance && boardUnits[j].id < bestEnemyId)) {
                bestDistance = dist;
                bestEnemyId = boardUnits[j].id;
                entry.target = gameWorld->combatActor(boardUnits[j]);
                entry.enemyCol = cached[j].col;
                entry.enemyRow = cached[j].row;
            }
        }

        entry.adjacentToEnemy = bestDistance == 1 && map.canEngageMelee(gameWorld->combatActor(*entry.unit), entry.target);
        if (entry.enemyCol != -1) {
            const int dx = entry.col - entry.enemyCol;
            const int dy = entry.row - entry.enemyRow;
            entry.dist = static_cast<float>(dx * dx + dy * dy);
        }

        units.push_back(entry);
    }

    std::sort(units.begin(), units.end(), [](const PlannerUnit& a, const PlannerUnit& b) {
        return betterPriority(a, b);
    });

    // Existing moves were reserved before sorting. Only idle units compete for
    // new steps; an in-flight move can never lose its reservation to a winner
    // chosen later. Newly accepted corridors immediately constrain later paths.
    for (const PlannerUnit& entry : units) {
        PokemonInstance& unit = *entry.unit;
        if (unit.isMoving && hasCommittedMove(unit)) continue;
        if (entry.adjacentToEnemy || entry.enemyCol == -1 || unit.movementSpeed <= 0.0f) {
            unit.isMoving = false;
            unit.committedDest = {-1, -1};
            continue;
        }
        const auto [wantCol, wantRow] = game::arena::firstStepTowards(
            map, gameWorld->combatActor(unit), entry.target, blocked);
        if (wantCol < 0 || wantRow < 0) {
            unit.isMoving = false;
            unit.committedDest = {-1, -1};
            continue;
        }
        game::arena::reserveStep(map, {entry.col, entry.row}, {wantCol, wantRow}, blocked);

        unit.committedDest = {wantCol, wantRow};
        unit.moveFrom = unit.position;
        unit.moveTo = gameWorld->gridToWorld(wantCol, wantRow);
        unit.moveT = 0.0f;
        unit.isMoving = true;
        if (shouldTraceAnim(services.engineServices, unit)) {
            std::ostringstream trace;
            trace << std::fixed << std::setprecision(3)
                  << "from_cell=" << cellString(entry.col, entry.row)
                  << "to_cell=" << cellString(wantCol, wantRow)
                  << "enemy_cell=" << cellString(entry.enemyCol, entry.enemyRow)
                  << "adjacent=" << (entry.adjacentToEnemy ? 1 : 0)
                  << "speed=" << unit.movementSpeed
                  << "move_from=" << vecString(unit.moveFrom)
                  << "move_to=" << vecString(unit.moveTo)
                  << "active_idx=" << unit.activeAnimIndex
                  << "active_role=" << animationRole(unit, unit.activeAnimIndex)
                  << "move_idx=" << unit.animMoveIndex
                  << "move_clip='" << animationName(unit, unit.animMoveIndex) << "'"
                  << "move_dur=" << animationDurationSec(unit, unit.animMoveIndex)
                  << "anim_time=" << unit.animTimeSec
                  << "atk_timer=" << unit.attackTimerSec;
            emitAnimTrace(&services.log, "MovePlan", unit, trace.str());
        }
    }

    for (const PlannerUnit& unit : units) {
        if (unit.enemyCol != -1 && unit.enemyRow != -1) {
            setFacingToTarget(*unit.unit, gameWorld->gridToWorld(unit.enemyCol, unit.enemyRow));
        }
    }

    if (fixedBreakdown) {
        fixedBreakdown->movementPlanMs += static_cast<float>(
            std::chrono::duration<double, std::milli>(Clock::now() - planStart).count());
    }

    auto& worldUnits = gameWorld->getPokemons();
    const auto advanceStart = Clock::now();

    const float cellSize = std::max(cfg.cellSize, 1e-4f);
    for (auto& unit : worldUnits) {
        if (!isCombatActive(unit) || !unit.isMoving || !hasCommittedMove(unit)) continue;

        const bool traceAnim = shouldTraceAnim(services.engineServices, unit);
        const glm::vec3 beforePos = unit.position;
        const float beforeMoveT = unit.moveT;

        // Flyers with a real ground-to-air role first complete that authored
        // takeoff chain in place. Continuously airborne species must not be
        // delayed by the generic fallback flight transition.
        // The committed destination remains reserved while the animation
        // system raises the visual into the aerial locomotion state.
        const bool waitingForFlight = unit.usesAirLocomotion &&
            FlightLocomotion::hasAuthoredTakeoff(unit) &&
            (unit.airState == AirLocomotionState::Grounded ||
             unit.airState == AirLocomotionState::TakingOff);
        if (waitingForFlight) {
            if (traceAnim && DebugTrace::animTicks()) {
                std::ostringstream trace;
                trace << std::fixed << std::setprecision(3)
                      << "pos=" << vecString(unit.position)
                      << "to=" << vecString(unit.moveTo)
                      << "move_t=" << unit.moveT
                      << "active_idx=" << unit.activeAnimIndex
                      << "role=" << animationRole(unit, unit.activeAnimIndex)
                      << "anim_time=" << unit.animTimeSec;
                emitAnimTrace(&services.log, "MoveHoldTakeoff", unit, trace.str());
            }
            continue;
        }

        const glm::vec3 toVec = unit.moveTo - unit.position;
        const float dist = glm::length(toVec);
        if (dist <= 1e-4f) {
            unit.position = unit.moveTo;
            unit.isMoving = false;
            unit.moveT = 1.0f;
            unit.committedDest = {-1, -1};
            if (traceAnim) {
                std::ostringstream trace;
                trace << std::fixed << std::setprecision(3)
                      << "reason=already_at_target"
                      << " pos=" << vecString(unit.position)
                      << " move_t=" << unit.moveT
                      << " active_idx=" << unit.activeAnimIndex
                      << " role=" << animationRole(unit, unit.activeAnimIndex)
                      << " anim_time=" << unit.animTimeSec;
                emitAnimTrace(&services.log, "MoveArrive", unit, trace.str());
            }
            continue;
        }

        const glm::vec3 dir = toVec / dist;
        const float step = std::max(0.0f, unit.movementSpeed) * cellSize * std::max(0.0f, deltaTime);
        if (step >= dist) {
            unit.position = unit.moveTo;
            unit.moveT = 1.0f;
            unit.committedDest = {-1, -1};
            unit.isMoving = shouldHoldLocomotionAfterArrival(*gameWorld, worldUnits, unit);
            if (traceAnim) {
                std::ostringstream trace;
                trace << std::fixed << std::setprecision(3)
                      << "reason=step_reached_target"
                      << " from=" << vecString(beforePos)
                      << " pos=" << vecString(unit.position)
                      << " dist_before=" << dist
                      << " step=" << step
                      << " move_t=" << beforeMoveT << "->" << unit.moveT
                      << " hold_locomotion=" << (unit.isMoving ? 1 : 0)
                      << " active_idx=" << unit.activeAnimIndex
                      << " role=" << animationRole(unit, unit.activeAnimIndex)
                      << " anim_time=" << unit.animTimeSec
                      << " atk_timer=" << unit.attackTimerSec;
                emitAnimTrace(&services.log, "MoveArrive", unit, trace.str());
            }
        } else {
            unit.position += dir * step;
            unit.moveT = std::min(1.0f, unit.moveT + (step / cellSize));
            if (traceAnim && DebugTrace::animTicks()) {
                std::ostringstream trace;
                trace << std::fixed << std::setprecision(3)
                      << "from=" << vecString(beforePos)
                      << "pos=" << vecString(unit.position)
                      << "to=" << vecString(unit.moveTo)
                      << "dist_before=" << dist
                      << "step=" << step
                      << "move_t=" << beforeMoveT << "->" << unit.moveT
                      << "active_idx=" << unit.activeAnimIndex
                      << "role=" << animationRole(unit, unit.activeAnimIndex)
                      << "anim_time=" << unit.animTimeSec
                      << "atk_timer=" << unit.attackTimerSec;
                emitAnimTrace(&services.log, "MoveStep", unit, trace.str());
            }
        }
    }

    // Keep every intermediate movement sample on the authored Route terrain.
    // This follows continuous ramp profiles instead of merely snapping to the
    // destination cell's elevation after arrival.
    gameWorld->conformPokemonToGround();

    if (fixedBreakdown) {
        fixedBreakdown->movementAdvanceMs += static_cast<float>(
            std::chrono::duration<double, std::milli>(Clock::now() - advanceStart).count());
    }
}
