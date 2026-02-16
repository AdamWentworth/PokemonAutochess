#include "GameWorld.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

#include "engine/render/Model.h"
#include "engine/utils/ResourceManager.h"

#include "GameConfig.h"
#include "config/AnimSetLoader.h"
#include "config/GameDataDb.h"
#include "config/PokemonConfigLoader.h"

namespace {

float computeFinalVisualScale(const PokemonInstance& instance) {
    return (instance.model ? instance.model->getScaleFactor() : 1.0f) *
           std::max(0.0f, instance.modelScaleCorrection) *
           std::max(0.0f, instance.speciesScale) *
           std::max(0.0f, instance.visualScale);
}

}  // namespace

bool GameWorld::buildPokemonInstance(const std::string& pokemonName,
                                     PokemonSide side,
                                     int level,
                                     PokemonInstance& outInst) {
    if (!data) {
        std::cerr << "[GameWorld] GameDataDb not set. Call GameWorld::setData() during init.\n";
        return false;
    }

    const PokemonStats* stats = data->pokemon.getStats(pokemonName);
    if (!stats) {
        std::cerr << "[GameWorld] No config found for Pokemon: " << pokemonName << "\n";
        return false;
    }

    const std::string modelPath = "assets/models/" + stats->model;
    std::shared_ptr<Model> sharedModel;
    if (!resources) {
        std::cerr << "[GameWorld] Resource service not set; cannot load model: " << modelPath << "\n";
        if (renderEnabled) return false;
    } else {
        sharedModel = resources->getModel(modelPath);
    }

    PokemonInstance inst;
    inst.id = PokemonInstance::getNextUnitID();
    inst.name = pokemonName;
    inst.model = sharedModel;
    inst.rotation = glm::vec3(0.0f, (side == PokemonSide::Player ? 180.0f : 0.0f), 0.0f);
    inst.side = side;

    inst.baseHp = stats->hp;
    inst.baseAttack = stats->attack;
    inst.baseMovementSpeed = stats->movementSpeed;
    inst.types = stats->types;
    inst.baseExp = stats->baseExp;
    inst.speciesScale = stats->visualScale;
    inst.modelScaleCorrection = resolveModelScaleCorrection(sharedModel,
                                                            stats->modelScaleMode,
                                                            stats->modelScaleAxis);

    applyLevelScaling(inst, level, /*preserveHp=*/false);
    applyLoadoutForLevel(inst, /*preserveEnergy=*/false);
    reconcileBoardScaleFromRoster();

    inst.animTimeSec = 0.0f;
    const PokemonStats* finalStats = data->pokemon.getStats(inst.name);
    const std::string finalPath = "assets/models/" +
                                  ((finalStats && !finalStats->model.empty()) ? finalStats->model : stats->model);
    AnimSet::applyAnimSetOverrides(inst, finalPath, data ? &data->flyers : nullptr);
    inst.animTimeSec = sharedLoopAnimTimeSec;

    outInst = std::move(inst);
    return true;
}

void GameWorld::spawnPokemon(const std::string& pokemonName,
                             const glm::vec3& startPos,
                             PokemonSide side,
                             int level) {
    PokemonInstance inst;
    if (!buildPokemonInstance(pokemonName, side, level, inst)) return;

    inst.position = startPos;
    pokemons.push_back(inst);
    if (side == PokemonSide::Player) {
        mergeTriplesForPlayer();
    }

    std::cout << "[GameWorld] Spawned " << pokemonName
              << " (ID: " << inst.id
              << ", L" << inst.level
              << ", HP: " << inst.hp << "/" << inst.maxHP
              << ", ATK: " << inst.attack
              << ", SPD: " << inst.movementSpeed
              << ", Scale: model=" << (inst.model ? inst.model->getScaleFactor() : 1.0f)
              << " corr=" << inst.modelScaleCorrection
              << " species=" << inst.speciesScale
              << " visual=" << inst.visualScale
              << " final=" << computeFinalVisualScale(inst)
              << ", FAST: " << (inst.fastMove.empty() ? "-" : inst.fastMove)
              << ", CHARGED: " << (inst.chargedMove.empty() ? "-" : inst.chargedMove)
              << ", Ecap: " << inst.maxEnergy
              << ")\n";
}

glm::vec3 GameWorld::gridToWorld(int col, int row) const {
    return gridToWorldWithCellSize(col, row, getBoardCellSize());
}

glm::ivec2 GameWorld::worldToGrid(const glm::vec3& pos) const {
    return worldToGridWithCellSize(pos, getBoardCellSize());
}

void GameWorld::spawnPokemonAtGrid(const std::string& pokemonName,
                                   int col,
                                   int row,
                                   PokemonSide side,
                                   int level) {
    spawnPokemon(pokemonName, gridToWorld(col, row), side, level);
}

void GameWorld::addToBench(const std::string& pokemonName, int level) {
    PokemonInstance inst;
    if (!buildPokemonInstance(pokemonName, PokemonSide::Player, level, inst)) return;

    int slot = static_cast<int>(benchPokemons.size());
    const int benchSlots = std::max(1, config.benchSlots);
    slot = std::min(slot, benchSlots - 1);
    inst.position = benchSlotToWorld(slot, getBoardCellSize());

    benchPokemons.push_back(inst);
    mergeTriplesForPlayer();

    std::cout << "[GameWorld] Benched " << pokemonName
              << " (ID: " << inst.id
              << " L" << inst.level
              << ", Scale: model=" << (inst.model ? inst.model->getScaleFactor() : 1.0f)
              << " corr=" << inst.modelScaleCorrection
              << " species=" << inst.speciesScale
              << " visual=" << inst.visualScale
              << " final=" << computeFinalVisualScale(inst)
              << ", FAST: " << (inst.fastMove.empty() ? "-" : inst.fastMove)
              << ", CHARGED: " << (inst.chargedMove.empty() ? "-" : inst.chargedMove)
              << ")\n";
}
