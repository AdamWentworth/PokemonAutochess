// src/game/GameWorld.h
#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>

#include "PokemonInstance.h"
#include "engine/ui/HealthBarData.h"

// Tail fire particle VFX (data-driven via cfg)
#include "vfx/TailFireVFX.h"
#include "vfx/TailFireVFXConfigDB.h"

class Camera3D;
class BoardRenderer;
class ResourceManager;
struct GameDataDb;
namespace LogBus { class Logger; }

class GameWorld {
public:
    void setResources(ResourceManager* rm) { resources = rm; }
    void setData(const GameDataDb* db) { data = db; }
    void setLogger(LogBus::Logger* logger) { log = logger; }
    LogBus::Logger* getLogger() const { return log; }
    const GameDataDb* getData() const { return data; }

    void spawnPokemon(const std::string& pokemonName,
                      const glm::vec3& startPos,
                      PokemonSide side = PokemonSide::Player,
                      int level = -1);

    void spawnPokemonAtGrid(const std::string& pokemonName,
                            int col, int row,
                            PokemonSide side = PokemonSide::Player,
                            int level = -1);

    // Advances animation clocks + VFX emitters
    void update(float dt);

    void drawAll(const Camera3D& camera, BoardRenderer& boardRenderer);

    std::vector<PokemonInstance>& getPokemons();
    const PokemonInstance* getPokemonByName(const std::string& name) const;


    PokemonInstance* findUnitById(int unitId);
    const PokemonInstance* findUnitById(int unitId) const;
    void addToBench(const std::string& pokemonName);
    std::vector<PokemonInstance>& getBenchPokemons();

    std::vector<HealthBarData> getHealthBarData(const Camera3D& camera, int screenWidth, int screenHeight) const;

    glm::vec3 getNearestEnemyPosition(const PokemonInstance& unit) const;

private:
    ResourceManager* resources = nullptr; // engine-owned
    const GameDataDb* data = nullptr;     // game-owned, injected
    LogBus::Logger* log = nullptr;        // optional game-owned logger

    std::vector<PokemonInstance> pokemons;
    std::vector<PokemonInstance> benchPokemons;

    glm::vec3 gridToWorld(int col, int row) const;

    void applyLevelScaling(PokemonInstance& inst, int level) const;
    void applyLoadoutForLevel(PokemonInstance& inst) const;

private:
    // Shared loop clock: keeps idle/walk animations in sync across all units.
    float sharedLoopAnimTimeSec = 0.0f;

    // Tail fire particles (drawn after opaque models)
    TailFireVFX tailFireVfx;
    bool tailFireVfxInitialized = false;
};
