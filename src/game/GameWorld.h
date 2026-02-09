// src/game/GameWorld.h
#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <unordered_map>

#include "PokemonInstance.h"
#include "engine/ui/HealthBarData.h"

// Tail fire particle VFX (data-driven via cfg)
#include "vfx/TailFireVFX.h"
#include "vfx/TailFireVFXConfigDB.h"
// Grass impact VFX (shared by grass-type moves)
#include "vfx/GrassImpactVFX.h"
// Tackle impact VFX
#include "vfx/TackleImpactVFX.h"
// Leech seed projectile VFX
#include "vfx/LeechSeedProjectileVFX.h"
// Leech seed heal VFX
#include "vfx/HealPlusVFX.h"
// Leech seed drain dots
#include "vfx/LeechSeedDrainVFX.h"
// Leech seed config
#include "config/LeechSeedConfigDB.h"

class Camera3D;
class BoardRenderer;
class ResourceManager;
struct GameDataDb;
struct GameConfigData;
namespace LogBus { class Logger; }

class GameWorld {
public:
    struct CombatBalance {
        float playerDamageMult = 1.0f;
        float enemyDamageMult = 1.0f;
        float playerDamageTakenMult = 1.0f;
        float enemyDamageTakenMult = 1.0f;
    };

    explicit GameWorld(const GameConfigData& cfg);

    void setResources(ResourceManager* rm) { resources = rm; }
    void setData(const GameDataDb* db) { data = db; }
    void setLogger(LogBus::Logger* logger) { log = logger; }
    void setRenderEnabled(bool enabled) { renderEnabled = enabled; }
    LogBus::Logger* getLogger() const { return log; }
    const GameDataDb* getData() const { return data; }
    const GameConfigData& getConfig() const { return config; }

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
    void addToBench(const std::string& pokemonName, int level = -1);
    std::vector<PokemonInstance>& getBenchPokemons();

    std::vector<HealthBarData> getHealthBarData(const Camera3D& camera, int screenWidth, int screenHeight) const;

    glm::vec3 getNearestEnemyPosition(const PokemonInstance& unit) const;

    void setCombatBalance(const CombatBalance& b) { combatBalance = b; }
    const CombatBalance& getCombatBalance() const { return combatBalance; }
    void resetCombatBalance() { combatBalance = CombatBalance{}; }

    // Impact VFX for grass-type attacks
    void emitGrassImpactAt(const PokemonInstance& target);
    // Impact VFX for tackle
    void emitTackleImpactAt(const PokemonInstance& target, const PokemonInstance* attacker = nullptr);

    // Apply leech seed status on hit
    void applyLeechSeed(int attackerId, int targetId);
    // Finalize a faint (cleanup + XP award). Safe to call once.
    void handleUnitFaint(PokemonInstance& target);
    void healPlayerUnitsToFull();
    void capturePlayerPositionsForBattle();
    void restorePlayerPositionsAfterBattle();
    void setBoardInteractionLocked(bool locked) { boardInteractionLocked = locked; }
    bool isBoardInteractionLocked() const { return boardInteractionLocked; }

private:
    ResourceManager* resources = nullptr; // engine-owned
    const GameDataDb* data = nullptr;     // game-owned, injected
    LogBus::Logger* log = nullptr;        // optional game-owned logger
    const GameConfigData& config;
    bool renderEnabled = false;

    std::vector<PokemonInstance> pokemons;
    std::vector<PokemonInstance> benchPokemons;

    CombatBalance combatBalance{};
    bool boardInteractionLocked = false;
    std::unordered_map<int, glm::vec3> battleStartPositions;

    glm::vec3 gridToWorld(int col, int row) const;

    void applyLevelScaling(PokemonInstance& inst, int level, bool preserveHp) const;
    void applyLoadoutForLevel(PokemonInstance& inst, bool preserveEnergy) const;
    void awardXpForFaint(const PokemonInstance& dead);
    void addXp(PokemonInstance& unit, int amount);
    int xpToNextLevel(int level) const;
    int xpFromFaint(const PokemonInstance& dead) const;
    void beginFaint(PokemonInstance& target);
    void updateFaint(PokemonInstance& target, float dt);

private:
    // Shared loop clock: keeps idle/walk animations in sync across all units.
    float sharedLoopAnimTimeSec = 0.0f;

    // Tail fire particles (drawn after opaque models)
    TailFireVFX tailFireVfx;
    bool tailFireVfxInitialized = false;

    // Grass impact particles (drawn after opaque models)
    GrassImpactVFX grassImpactVfx;
    bool grassImpactVfxInitialized = false;

    // Tackle impact particles (drawn after opaque models)
    TackleImpactVFX tackleImpactVfx;
    bool tackleImpactVfxInitialized = false;

    // Leech seed projectiles (drawn after opaque models)
    LeechSeedProjectileVFX leechSeedVfx;
    bool leechSeedVfxInitialized = false;

    // Leech seed heal VFX
    HealPlusVFX healPlusVfx;
    bool healPlusVfxInitialized = false;

    LeechSeedDrainVFX leechSeedDrainVfx;
    bool leechSeedDrainVfxInitialized = false;

    // Leech seed config
    bool leechSeedConfigLoaded = false;
    LeechSeedConfig leechSeedConfig{};

    struct PendingLeechHeal {
        int sourceId = -1;
        int amount = 0;
        float timeLeftSec = 0.0f;
    };
    std::vector<PendingLeechHeal> pendingLeechHeals;

    void updateLeechSeedStatus(float dt);
    void ensureLeechSeedConfigLoaded();
};
