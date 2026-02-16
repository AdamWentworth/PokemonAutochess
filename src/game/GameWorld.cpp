// src/game/GameWorld.cpp
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

#include "GameWorld.h"
#include "GameConfig.h"

#include "engine/utils/ResourceManager.h"
#include "engine/render/Model.h"

#include "config/GameDataDb.h"
#include "config/PokemonConfigLoader.h"
#include "config/AnimSetLoader.h"

#include "logging/LoggerUtil.h"

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
    return std::max(0.05f, config.cellSize * boardScaleMul);
}

glm::vec3 GameWorld::gridToWorldWithCellSize(int col, int row, float cellSize) const {
    const float boardOriginX = -((config.cols * cellSize) / 2.0f) + cellSize * 0.5f;
    const float boardOriginZ = -((config.rows * cellSize) / 2.0f) + cellSize * 0.5f;
    return { boardOriginX + col * cellSize, 0.0f, boardOriginZ + row * cellSize };
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
    const float startZ = (config.rows * cellSize) * 0.5f + 0.5f;
    const float x = startX + cellSize * 0.5f + slot * cellSize;
    const float z = startZ + cellSize * 0.5f;
    return glm::vec3(x, 0.0f, z);
}

void GameWorld::reconcileBoardScaleFromRoster() {
    // Fixed board sizing policy:
    // keep a constant board cell size and never auto-resize from roster composition.
    // This keeps rendered model size driven only by native model size and config visualScale.
    if (std::abs(boardScaleMul - 1.0f) < 0.0001f) {
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
        const glm::ivec2 cell = worldToGridWithCellSize(kv.second, oldCell);
        kv.second = gridToWorldWithCellSize(cell.x, cell.y, newCell);
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
    const std::string path = "assets/models/" + nextStats->model;

    std::shared_ptr<Model> nextModel = unit.model;
    if (resources) {
        auto loaded = resources->getModel(path);
        if (loaded) nextModel = loaded;
        else if (renderEnabled) {
            if (log) game::log::warn(log, "Evolution model load failed for " + nextName + ": " + path);
            return;
        }
    } else if (renderEnabled) {
        if (log) game::log::warn(log, "Resource service missing; cannot load evolution model: " + path);
        return;
    }

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
std::vector<PokemonInstance>& GameWorld::getPokemons() { return pokemons; }
std::vector<PokemonInstance>& GameWorld::getBenchPokemons() { return benchPokemons; }
