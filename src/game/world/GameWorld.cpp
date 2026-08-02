// src/game/world/GameWorld.cpp
#include <cmath>
#include <algorithm>
#include <memory>
#include <string>
#include <cctype>
#include <array>

namespace {
std::string Capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

#include "game/world/GameWorld.h"
#include "game/GameConfig.h"

#include "engine/render/Model.h"

#include "game/config/GameDataDb.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/config/AnimSetLoader.h"

#include "game/logging/LoggerUtil.h"

namespace {
float ResolveModelScaleCorrectionImpl(const std::shared_ptr<Model>& model,
                                      const std::string& scaleModeRaw,
                                      const std::string& axisModeRaw) {
    if (!model) return 1.0f;

    const std::string scaleMode = Lower(scaleModeRaw);
    if (scaleMode.empty() || scaleMode == "native" || scaleMode == "raw") {
        const float importerScale = std::max(0.0f, model->getScaleFactor());
        if (importerScale <= 1e-6f) return 1.0f;
        // Cancel importer normalization so config scale reflects native GLB size.
        return 1.0f / importerScale;
    }

    // Legacy compatibility path.
    if (scaleMode != "normalized") {
        const float importerScale = std::max(0.0f, model->getScaleFactor());
        if (importerScale <= 1e-6f) return 1.0f;
        return 1.0f / importerScale;
    }

    if (!model->hasBounds()) return 1.0f;

    std::string axisMode = Lower(axisModeRaw);
    if (axisMode.empty() || axisMode == "max") return 1.0f;

    const glm::vec3 ext = model->getBoundsMax() - model->getBoundsMin();
    const float ex = std::max(0.0f, ext.x);
    const float ey = std::max(0.0f, ext.y);
    const float ez = std::max(0.0f, ext.z);
    const float maxExtent = std::max(ex, std::max(ey, ez));
    if (maxExtent <= 1e-6f) return 1.0f;

    float chosenExtent = maxExtent;
    if (axisMode == "x") chosenExtent = ex;
    else if (axisMode == "y") chosenExtent = ey;
    else if (axisMode == "z") chosenExtent = ez;
    else if (axisMode == "median") {
        std::array<float, 3> arr{ex, ey, ez};
        std::sort(arr.begin(), arr.end());
        chosenExtent = arr[1];
    }

    if (chosenExtent <= 1e-6f) return 1.0f;
    return maxExtent / chosenExtent;
}
} // namespace

float GameWorld::resolveModelScaleCorrection(const std::shared_ptr<Model>& model,
                                             const std::string& scaleModeRaw,
                                             const std::string& axisModeRaw) {
    return ResolveModelScaleCorrectionImpl(model, scaleModeRaw, axisModeRaw);
}

GameWorld::GameWorld(const GameConfigData& cfg)
    : config(cfg) {
    money = std::max(0, config.startingCash);
}

float GameWorld::getBoardCellSize() const {
    return editorBoardCellSize > 0.0f
        ? editorBoardCellSize
        : std::max(0.05f, config.cellSize * boardScaleMul);
}

void GameWorld::bindGroundHeightResolver(
    const void* sourceIdentity,
    GroundHeightResolver resolver) {
    if (!sourceIdentity || !resolver) {
        clearGroundHeightResolver();
        return;
    }
    if (groundHeightResolverSource == sourceIdentity &&
        groundHeightResolver) {
        return;
    }
    groundHeightResolverSource = sourceIdentity;
    groundHeightResolver = std::move(resolver);
    conformPokemonToGround();
}

void GameWorld::clearGroundHeightResolver() {
    if (!groundHeightResolver) {
        groundHeightResolverSource = nullptr;
        return;
    }
    groundHeightResolver = {};
    groundHeightResolverSource = nullptr;
    const auto flatten = [](glm::vec3& position) {
        position.y = 0.0f;
    };
    for (auto& unit : pokemons) {
        flatten(unit.position);
        flatten(unit.moveFrom);
        flatten(unit.moveTo);
    }
    for (auto& unit : benchPokemons) {
        flatten(unit.position);
        flatten(unit.moveFrom);
        flatten(unit.moveTo);
    }
    for (auto& [unitId, pose] : battleStartPositions) {
        (void)unitId;
        flatten(pose.position);
    }
}

bool GameWorld::sampleGroundHeight(
    float worldX,
    float worldZ,
    float& outWorldY) const {
    return groundHeightResolver &&
        groundHeightResolver(worldX, worldZ, outWorldY) &&
        std::isfinite(outWorldY);
}

glm::vec3 GameWorld::conformPositionToGround(
    const glm::vec3& position) const {
    glm::vec3 grounded = position;
    float height = grounded.y;
    if (sampleGroundHeight(grounded.x, grounded.z, height)) {
        grounded.y = height;
    }
    return grounded;
}

void GameWorld::conformPokemonToGround() {
    if (!groundHeightResolver) {
        return;
    }
    const auto conformUnit = [&](PokemonInstance& unit) {
        unit.position = conformPositionToGround(unit.position);
        unit.moveFrom = conformPositionToGround(unit.moveFrom);
        unit.moveTo = conformPositionToGround(unit.moveTo);
    };
    for (auto& unit : pokemons) {
        conformUnit(unit);
    }
    for (auto& unit : benchPokemons) {
        conformUnit(unit);
    }
    for (auto& [unitId, pose] : battleStartPositions) {
        (void)unitId;
        pose.position = conformPositionToGround(pose.position);
    }
}

void GameWorld::setEditorBoardCellSize(float cellSize) {
    const float oldCell = getBoardCellSize();
    const float newCell = std::clamp(cellSize, 0.25f, 4.0f);
    if (std::abs(oldCell - newCell) < 0.0001f) {
        return;
    }
    const auto remapBoard = [&](std::vector<PokemonInstance>& list) {
        for (auto& unit : list) {
            const glm::ivec2 cell =
                worldToGridWithCellSize(unit.position, oldCell);
            unit.position =
                gridToWorldWithCellSize(cell.x, cell.y, newCell);
            if (unit.isMoving) {
                const glm::ivec2 fromCell =
                    worldToGridWithCellSize(unit.moveFrom, oldCell);
                const glm::ivec2 toCell =
                    worldToGridWithCellSize(unit.moveTo, oldCell);
                unit.moveFrom = gridToWorldWithCellSize(
                    fromCell.x, fromCell.y, newCell);
                unit.moveTo = gridToWorldWithCellSize(
                    toCell.x, toCell.y, newCell);
            }
        }
    };
    remapBoard(pokemons);
    for (auto& unit : benchPokemons) {
        const int slot = benchSlotFromPosition(unit.position, oldCell);
        unit.position = benchSlotToWorld(slot, newCell);
    }
    editorBoardCellSize = newCell;
    boardScaleMul = 1.0f;
}

glm::vec3 GameWorld::gridToWorldWithCellSize(int col, int row, float cellSize) const {
    const float boardOriginX = -((config.cols * cellSize) / 2.0f) + cellSize * 0.5f;
    const float boardOriginZ = -((config.rows * cellSize) / 2.0f) + cellSize * 0.5f;
    return conformPositionToGround(
        {boardOriginX + col * cellSize,
         0.0f,
         boardOriginZ + row * cellSize});
}

glm::ivec2 GameWorld::worldToGridWithCellSize(const glm::vec3& pos, float cellSize) const {
    const float boardOriginX = -((config.cols * cellSize) / 2.0f) + cellSize * 0.5f;
    const float boardOriginZ = -((config.rows * cellSize) / 2.0f) + cellSize * 0.5f;
    const int col = static_cast<int>(std::round((pos.x - boardOriginX) / cellSize));
    const int row = static_cast<int>(std::round((pos.z - boardOriginZ) / cellSize));
    return { col, row };
}

int GameWorld::benchSlotFromPosition(const glm::vec3& pos, float cellSize) const {
    const int benchSlots = std::max(1, config.benchSlots);
    const float totalWidth = benchSlots * cellSize;
    const float startX = -totalWidth * 0.5f;
    int slot = static_cast<int>(std::round((pos.x - (startX + cellSize * 0.5f)) / cellSize));
    slot = std::clamp(slot, 0, benchSlots - 1);
    return slot;
}

glm::vec3 GameWorld::benchSlotToWorld(int slot, float cellSize) const {
    const int benchSlots = std::max(1, config.benchSlots);
    slot = std::clamp(slot, 0, benchSlots - 1);
    const float totalWidth = benchSlots * cellSize;
    const float startX = -totalWidth * 0.5f;
    const float benchGap =
        static_cast<float>(std::max(0, config.benchGapCells)) *
        cellSize;
    const float startZ =
        (config.rows * cellSize) * 0.5f + benchGap;
    const float x = startX + cellSize * 0.5f + slot * cellSize;
    const float z = startZ + cellSize * 0.5f;
    return conformPositionToGround(glm::vec3(x, 0.0f, z));
}

void GameWorld::reconcileBoardScaleFromRoster() {
    // Fixed board sizing policy:
    // keep a constant board cell size and never auto-resize from roster composition.
    // This keeps rendered model size driven only by native model size and config visualScale.
    if (editorBoardCellSize > 0.0f ||
        std::abs(boardScaleMul - 1.0f) < 0.0001f) {
        boardResizePauseSec = 0.0f;
        return;
    }

    const float oldCell = getBoardCellSize();
    const float newCell = std::max(0.05f, config.cellSize);

    auto remapBoard = [&](std::vector<PokemonInstance>& list) {
        for (auto& u : list) {
            const glm::ivec2 cell = worldToGridWithCellSize(u.position, oldCell);
            u.position = gridToWorldWithCellSize(cell.x, cell.y, newCell);

            if (u.isMoving) {
                const glm::ivec2 fromCell = worldToGridWithCellSize(u.moveFrom, oldCell);
                const glm::ivec2 toCell = worldToGridWithCellSize(u.moveTo, oldCell);
                u.moveFrom = gridToWorldWithCellSize(fromCell.x, fromCell.y, newCell);
                u.moveTo = gridToWorldWithCellSize(toCell.x, toCell.y, newCell);
            }
        }
    };

    remapBoard(pokemons);

    for (auto& u : benchPokemons) {
        const int slot = benchSlotFromPosition(u.position, oldCell);
        u.position = benchSlotToWorld(slot, newCell);
    }

    for (auto& kv : battleStartPositions) {
        const glm::ivec2 cell = worldToGridWithCellSize(kv.second.position, oldCell);
        kv.second.position = gridToWorldWithCellSize(cell.x, cell.y, newCell);
    }

    boardScaleMul = 1.0f;
    boardResizePauseSec = 0.0f;

    if (log) {
        game::log::infoTerminalOnly(log, "[BoardScale] Auto-resize disabled; board multiplier fixed at 1.0");
    }
}

void GameWorld::tryApplyEvolution(PokemonInstance& unit) {
    if (!data) return;

    const EvolutionRule* rule = data->evolution.getRule(unit.name);
    if (!rule) return;
    if (unit.level < rule->level) return;

    const PokemonStats* nextStats = data->pokemon.getStats(rule->evolvesTo);
    if (!nextStats) {
        if (log) {
            game::log::warn(log, "Evolution target '" + rule->evolvesTo + "' missing in pokemon_config.json");
        }
        return;
    }

    const std::string prevName = unit.name;
    const std::string nextName = rule->evolvesTo;
    std::shared_ptr<Model> nextModel = unit.model;

    unit.name = nextName;
    unit.model = nextModel;
    unit.baseHp = nextStats->hp;
    unit.baseAttack = nextStats->attack;
    unit.baseMovementSpeed = nextStats->movementSpeed;
    unit.types = nextStats->types;
    unit.baseExp = nextStats->baseExp;
    unit.speciesScale = nextStats->visualScale;
    unit.modelScaleCorrection = resolveModelScaleCorrection(nextModel,
                                                            nextStats->modelScaleMode,
                                                            nextStats->modelScaleAxis);

    applyLevelScaling(unit, unit.level, /*preserveHp=*/true);
    applyLoadoutForLevel(unit, /*preserveEnergy=*/true);

    unit.animTimeSec = sharedLoopAnimTimeSec;
    const std::string path = "assets/models/" + nextStats->model;
    AnimSet::applyAnimSetOverrides(unit, path, data ? &data->flyers : nullptr);
    unit.animTimeSec = sharedLoopAnimTimeSec;

    if (log) {
        game::log::info(log, Capitalize(prevName) + " evolved into " + Capitalize(nextName) + "!");
    }
}

const PokemonInstance* GameWorld::getPokemonByName(const std::string& name) const {
    for (const auto& p : pokemons) {
        if (p.name == name) return &p;
    }
    return nullptr;
}



PokemonInstance* GameWorld::findUnitById(int unitId) {
    for (auto& p : pokemons) {
        if (p.id == unitId) return &p;
    }
    for (auto& b : benchPokemons) {
        if (b.id == unitId) return &b;
    }
    return nullptr;
}

const PokemonInstance* GameWorld::findUnitById(int unitId) const {
    for (const auto& p : pokemons) {
        if (p.id == unitId) return &p;
    }
    for (const auto& b : benchPokemons) {
        if (b.id == unitId) return &b;
    }
    return nullptr;
}

bool GameWorld::setEditorPreviewUnitTransform(
    int unitId,
    const glm::vec3& position,
    const glm::vec3& rotationDegrees,
    bool snapToGameplaySlot) {
    PokemonInstance* unit = findUnitById(unitId);
    if (!unit) {
        return false;
    }

    const auto benchIt = std::find_if(
        benchPokemons.begin(),
        benchPokemons.end(),
        [unitId](const PokemonInstance& candidate) {
            return candidate.id == unitId;
        });
    glm::vec3 resolvedPosition = position;
    if (snapToGameplaySlot) {
        if (benchIt != benchPokemons.end()) {
            const int slot = benchSlotFromPosition(
                position, getBoardCellSize());
            resolvedPosition = benchSlotToWorld(
                slot, getBoardCellSize());
        } else {
            glm::ivec2 cell = worldToGrid(position);
            cell.x = std::clamp(cell.x, 0, std::max(0, config.cols - 1));
            cell.y = std::clamp(cell.y, 0, std::max(0, config.rows - 1));
            resolvedPosition = gridToWorld(cell.x, cell.y);
        }
    } else {
        resolvedPosition = conformPositionToGround(position);
    }

    unit->position = resolvedPosition;
    unit->rotation = rotationDegrees;
    unit->isMoving = false;
    unit->moveFrom = resolvedPosition;
    unit->moveTo = resolvedPosition;
    unit->moveT = 1.0f;
    unit->committedDest = {-1, -1};
    battleStartPositions[unitId] = BattleStartPose{
        .position = resolvedPosition,
        .rotation = rotationDegrees};
    return true;
}

std::vector<PokemonInstance>& GameWorld::getPokemons() { return pokemons; }
std::vector<PokemonInstance>& GameWorld::getBenchPokemons() { return benchPokemons; }

