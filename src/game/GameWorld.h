// src/game/GameWorld.h
#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <unordered_map>

#include "PokemonInstance.h"
#include "engine/ui/HealthBarData.h"
#include "engine/core/IRandom.h"

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
// Growl wave VFX
#include "vfx/GrowlWaveVFX.h"
// Claw swipe VFX (scratch/metal claw)
#include "vfx/ClawSwipeVFX.h"
// Aqua swoosh/bubble/water-gun impact VFX
#include "vfx/AquaSwooshVFX.h"
// Leech seed config
#include "config/LeechSeedConfigDB.h"

class Camera3D;
class BoardRenderer;
class ResourceManager;
class Model;
struct GameDataDb;
struct GameConfigData;
namespace LogBus { class Logger; }

class GameWorld {
public:
    struct ClassicShopCard {
        std::string name;
        int level = 1;
        int cost = 0;
    };

    struct CombatBalance {
        float playerDamageMult = 1.0f;
        float enemyDamageMult = 1.0f;
        float playerDamageTakenMult = 1.0f;
        float enemyDamageTakenMult = 1.0f;
    };

    struct ClassicRoundIncomeResult {
        int baseIncome = 0;
        int interestIncome = 0;
        int streakIncome = 0;
        int totalIncome = 0;
        int winStreak = 0;
        int lossStreak = 0;
        int roundIndex = 0;
        bool won = false;
    };

    struct TypeLineCount {
        std::string type;
        int uniqueLineCount = 0;
    };

    explicit GameWorld(const GameConfigData& cfg);

    void setResources(ResourceManager* rm) { resources = rm; }
    void setData(const GameDataDb* db) { data = db; }
    void setLogger(LogBus::Logger* logger) { log = logger; }
    void setRenderEnabled(bool enabled) { renderEnabled = enabled; }
    void setRng(engine::IRandom* rngIn) { rng = rngIn; }
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
    glm::vec3 gridToWorld(int col, int row) const;
    glm::ivec2 worldToGrid(const glm::vec3& pos) const;
    float getBoardCellSize() const;

    // Advances animation clocks + VFX emitters
    void update(float dt);

    void drawAll(const Camera3D& camera, BoardRenderer& boardRenderer);

    std::vector<PokemonInstance>& getPokemons();
    const PokemonInstance* getPokemonByName(const std::string& name) const;


    PokemonInstance* findUnitById(int unitId);
    const PokemonInstance* findUnitById(int unitId) const;
    void addToBench(const std::string& pokemonName, int level = -1);
    std::vector<PokemonInstance>& getBenchPokemons();
    void mergeTriplesForPlayer();

    std::vector<HealthBarData> getHealthBarData(const Camera3D& camera, int screenWidth, int screenHeight) const;
    std::vector<TypeLineCount> getPlayerTypeLineCounts() const;

    glm::vec3 getNearestEnemyPosition(const PokemonInstance& unit) const;

    void setCombatBalance(const CombatBalance& b) { combatBalance = b; }
    const CombatBalance& getCombatBalance() const { return combatBalance; }
    void resetCombatBalance() { combatBalance = CombatBalance{}; }

    int getMoney() const { return money; }
    void addMoney(int amount);
    bool spendMoney(int amount);
    int getSellValueForSpecies(const std::string& pokemonName) const;
    void setClassicShopCards(const std::vector<ClassicShopCard>& cards);
    const std::vector<ClassicShopCard>& getClassicShopCards() const { return classicShopCards; }
    void clearClassicShopCards();
    void setUnitDropZoneLayoutHint(int cardCount, bool allItems);
    int getUnitDropZoneCardCount() const { return unitDropZoneCardCount; }
    bool getUnitDropZoneUsesItemLayout() const { return unitDropZoneAllItems; }
    void setUnitSellRewardsEnabled(bool enabled) { unitSellRewardsEnabled = enabled; }
    bool isUnitSellRewardsEnabled() const { return unitSellRewardsEnabled; }
    int getItemCount(const std::string& item) const;
    void addItem(const std::string& item, int amount = 1);
    bool consumeItem(const std::string& item, int amount = 1);
    std::vector<std::pair<std::string, int>> listItems() const;

    void setSelectedItem(const std::string& itemId);
    const std::string& getSelectedItem() const { return selectedItemId; }
    void clearSelectedItem();

    bool tryUseHealingItem(const std::string& itemId, int targetId);
    bool startCaptureAttempt(int targetId, float ballMult, const glm::vec3* throwOrigin = nullptr);

    // Impact VFX for grass-type attacks
    void emitGrassImpactAt(const PokemonInstance& target);
    // Impact VFX for tackle
    void emitTackleImpactAt(const PokemonInstance& target, const PokemonInstance* attacker = nullptr);
    // Move-specific impact VFX routing
    void emitMoveImpactByName(const std::string& moveName,
                              const PokemonInstance& target,
                              const PokemonInstance* attacker = nullptr);

    // Apply leech seed status on hit
    void applyLeechSeed(int attackerId, int targetId);
    // Finalize a faint (cleanup + XP award). Safe to call once.
    void handleUnitFaint(PokemonInstance& target);
    void healPlayerUnitsToFull();
    void resetForNewGame(int startingMoney = -1);
    ClassicRoundIncomeResult awardClassicRoundIncome(bool playerWon);
    void capturePlayerPositionsForBattle();
    void restorePlayerPositionsAfterBattle();
    void setBoardInteractionLocked(bool locked) { boardInteractionLocked = locked; }
    bool isBoardInteractionLocked() const { return boardInteractionLocked; }
    bool isBoardResizePauseActive() const { return boardResizePauseSec > 0.0f; }
    void setUnitDragActive(bool active) { unitDragActive = active; }
    bool isUnitDragActive() const { return unitDragActive; }
    void blockUiClicks(int frames = 1) {
        if (frames <= 0) return;
        if (uiClickBlockFrames < frames) uiClickBlockFrames = frames;
    }
    bool consumeUiClickBlocked() {
        if (uiClickBlockFrames <= 0) return false;
        --uiClickBlockFrames;
        return true;
    }

private:
    ResourceManager* resources = nullptr; // engine-owned
    const GameDataDb* data = nullptr;     // game-owned, injected
    LogBus::Logger* log = nullptr;        // optional game-owned logger
    const GameConfigData& config;
    bool renderEnabled = false;
    engine::IRandom* rng = nullptr;

    std::vector<PokemonInstance> pokemons;
    std::vector<PokemonInstance> benchPokemons;

    CombatBalance combatBalance{};
    bool boardInteractionLocked = false;
    bool unitDragActive = false;
    int uiClickBlockFrames = 0;
    std::unordered_map<int, glm::vec3> battleStartPositions;
    int money = 0;
    std::vector<ClassicShopCard> classicShopCards;
    int unitDropZoneCardCount = 0;
    bool unitDropZoneAllItems = false;
    bool unitSellRewardsEnabled = true;
    std::unordered_map<std::string, int> items;
    std::string selectedItemId;

    struct CaptureAttempt {
        int targetId = -1;
        bool success = false;
        int shakes = 0;
        int shakesEmitted = 0;
        float phaseTime = 0.0f;
        float throwDur = 0.35f;
        float absorbDur = 0.35f;
        float shakeDur = 0.75f;
        float resolveDur = 0.35f;
        float timeLeftSec = 0.0f;
        std::string name;
        int level = 1;
        glm::vec3 startPos{0.0f};
        glm::vec3 targetPos{0.0f};
        glm::vec3 ballPos{0.0f};
        float ballScale = 1.0f;
        float ballBaseScale = 1.0f;
        float ballStartScale = 1.0f;
        float ballImpactScale = 1.0f;
        float ballYawDeg = 0.0f;
        enum class Phase { Throw, Absorb, Shake, Resolve } phase = Phase::Throw;
    };
    std::vector<CaptureAttempt> captureAttempts;

    glm::vec3 gridToWorldWithCellSize(int col, int row, float cellSize) const;
    glm::ivec2 worldToGridWithCellSize(const glm::vec3& pos, float cellSize) const;
    int benchSlotFromPosition(const glm::vec3& pos, float cellSize) const;
    glm::vec3 benchSlotToWorld(int slot, float cellSize) const;
    void reconcileBoardScaleFromRoster();
    void tickPokemonAnimation(PokemonInstance& unit, float dt);
    void updateRenderVfx(float dt);

    void applyLevelScaling(PokemonInstance& inst, int level, bool preserveHp) const;
    void applyLoadoutForLevel(PokemonInstance& inst, bool preserveEnergy) const;
    void tryApplyEvolution(PokemonInstance& unit);
    std::string resolveEvolutionLineRoot(const std::string& species) const;
    void awardXpForFaint(const PokemonInstance& dead);
    void addXp(PokemonInstance& unit, int amount);
    int xpToNextLevel(int level) const;
    int totalXpFromLevelProgress(const PokemonInstance& unit) const;
    void levelProgressFromTotalXp(int totalXp, int& outLevel, int& outXp) const;
    bool mergeOneTripleForPlayer();
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

    GrowlWaveVFX growlWaveVfx;
    bool growlWaveVfxInitialized = false;

    ClawSwipeVFX clawSwipeVfx;
    bool clawSwipeVfxInitialized = false;

    AquaSwooshVFX aquaSwooshVfx;
    bool aquaSwooshVfxInitialized = false;

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
    void updateCaptureAttempts(float dt);
    void ensurePokeballModel();
    bool buildPokemonInstance(const std::string& pokemonName,
                              PokemonSide side,
                              int level,
                              PokemonInstance& outInst);
    static float resolveModelScaleCorrection(const std::shared_ptr<Model>& model,
                                             const std::string& scaleModeRaw,
                                             const std::string& axisModeRaw);

    int classicWinStreak = 0;
    int classicLossStreak = 0;
    int classicRoundsCompleted = 0;
    float boardScaleMul = 1.0f;
    float boardResizePauseSec = 0.0f;

    std::shared_ptr<Model> pokeballModel;
    bool pokeballModelLoaded = false;

    glm::mat4 lastViewMatrix = glm::mat4(1.0f);
    bool hasLastViewMatrix = false;
};
