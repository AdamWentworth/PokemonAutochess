// src/game/scripting/ScriptAPI.h
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <utility>
#include <tuple>

#include "game/GameServices.h"
#include "game/PokemonInstance.h"
#include "game/scripting/ScriptEventBus.h"

class GameWorld;
class GameStateManager;
namespace LogBus { class Logger; }

// ScriptAPI: explicit, stable surface for Lua to interact with gameplay.
// Commands are queued and applied at a controlled boundary (flush()).
class ScriptAPI {
public:
    ScriptAPI(GameWorld* world,
              GameStateManager* manager,
              GameServices& services);

    LogBus::Logger& logger() const;
    ScriptEventBus& events() const;
    const GameConfigData& config() const;

    int getMoney() const;
    void addMoney(int amount);
    bool spendMoney(int amount);
    int getItemCount(const std::string& item) const;
    void addItem(const std::string& item, int amount = 1);
    bool consumeItem(const std::string& item, int amount = 1);
    float getPokemonCatchRate(const std::string& name) const;
    std::string getGameMode() const;
    void setGameMode(const std::string& mode);
    bool getHasStartedGame() const;
    void setHasStartedGame(bool started);
    bool setVideoMode(int width, int height, bool fullscreen);
    GameServices::VideoMode getVideoMode() const;
    std::string getRendererBackendPreference() const;
    bool isRendererBackendImplemented(const std::string& backend) const;
    bool setRendererBackendPreference(const std::string& backend);
    bool getVSyncPreference() const;
    bool setVSyncPreference(bool enabled);
    int getFpsCapPreference() const;
    bool setFpsCapPreference(int fpsCap);
    int getGraphicsQualityPreference() const;
    bool setGraphicsQualityPreference(int quality);
    bool getRequireDiscreteGpuPreference() const;
    bool setRequireDiscreteGpuPreference(bool required);
    std::string getActiveRendererBackend() const;
    std::string getActiveGpuRenderer() const;
    std::vector<std::string> getGpuAdapters() const;
    std::string getPreferredGpuAdapterPreference() const;
    bool setPreferredGpuAdapterPreference(const std::string& adapterName);
    bool getCharacterInkingPreference() const;
    bool setCharacterInkingPreference(bool enabled);
    int getAudioMasterVolumePreference() const;
    bool setAudioMasterVolumePreference(int volumePercent);
    int getAudioMusicVolumePreference() const;
    bool setAudioMusicVolumePreference(int volumePercent);
    int getAudioSfxVolumePreference() const;
    bool setAudioSfxVolumePreference(int volumePercent);
    int getAudioVoiceVolumePreference() const;
    bool setAudioVoiceVolumePreference(int volumePercent);
    bool getAudioMutePreference() const;
    bool setAudioMutePreference(bool enabled);
    bool isActiveGpuDiscrete() const;
    bool requestRestartToMenu(const std::string& menuScreen);
    std::string consumeBootMenuScreen();
    void requestQuit();
    void startNewGame(const std::string& mode);
    struct ClassicIncomeResult {
        int baseIncome = 0;
        int interestIncome = 0;
        int streakIncome = 0;
        int totalIncome = 0;
        int winStreak = 0;
        int lossStreak = 0;
        int roundIndex = 0;
        bool won = false;
    };
    ClassicIncomeResult awardClassicRoundIncome(bool playerWon);

    struct ClassicShopCardSnapshot {
        std::string name;
        int level = 1;
        int cost = 0;
    };
    std::vector<ClassicShopCardSnapshot> getClassicShopCards() const;
    void setClassicShopCards(const std::vector<ClassicShopCardSnapshot>& cards);
    void clearClassicShopCards();

    struct UnitSnapshot {
        int id = -1;
        std::string name;
        PokemonSide side = PokemonSide::Player;
        int hp = 0;
        int attack = 0;
        float speed = 0.0f;
        int energy = 0;
        int maxEnergy = 0;
        int col = 0;
        int row = 0;
        bool alive = false;
        bool fainting = false;
        bool blocksTile = false;
        bool captureInProgress = false;
        std::string fastMove;
        std::string chargedMove;
        std::vector<std::string> types;
    };

    struct MovementUnitSnapshot {
        int id = -1;
        int col = 0;
        int row = 0;
        float speed = 0.0f;
        bool alive = false;
        bool blocksTile = false;
        bool isMoving = false;
        int plannedCol = -1;
        int plannedRow = -1;
        int enemyCol = -1;
        int enemyRow = -1;
        bool adjacentToEnemy = false;
    };

    struct CombatUnitSnapshot {
        int id = -1;
        std::string name;
        PokemonSide side = PokemonSide::Player;
        int hp = 0;
        int attack = 0;
        float speed = 0.0f;
        int energy = 0;
        int maxEnergy = 0;
        int col = 0;
        int row = 0;
        bool alive = false;
        bool fainting = false;
        bool captureInProgress = false;
        std::string fastMove;
        std::string chargedMove;
        std::vector<std::string> types;
        int adjacentEnemyCount = 0;
        int bestAdjacentEnemyId = -1;
        bool canAttack = false;
        bool attackReady = false;
    };

    struct MoveStatusSnapshot {
        std::string effect;
        float magnitude = 0.0f;
        float durationSec = 0.0f;
        std::string target;
        bool valid = false;
    };

    struct MoveSnapshot {
        std::string name;
        std::string type;
        std::string kind;
        float cooldownSec = 0.0f;
        int power = 0;
        float range = 0.0f;
        int energyGain = 0;
        int energyCost = 0;
        MoveStatusSnapshot status;
    };

    // ---- Query surface (value types only) ----
    std::vector<UnitSnapshot> listUnits() const;
    std::vector<MovementUnitSnapshot> listUnitsForMovement() const;
    std::vector<CombatUnitSnapshot> listUnitsForCombat() const;
    std::optional<UnitSnapshot> getUnitSnapshot(int unitId) const;
    std::pair<int, int> nearestEnemyCell(int unitId) const;
    std::tuple<float, float, float> gridToWorldPos(int col, int row) const;
    std::pair<int, int> worldToGridPos(float x, float y, float z) const;
    bool isAdjacentToEnemy(int unitId) const;
    std::vector<int> enemiesAdjacent(int unitId) const;
    bool canAttack(int unitId) const;
    bool attackReady(int unitId) const;
    float attackMinRequestSec(int attackerId,
                              const std::optional<std::string>& moveName,
                              const std::optional<std::string>& kind) const;
    int getEnergy(int unitId) const;
    int getMaxEnergy(int unitId) const;
    float getUnitSpeed(int unitId) const;
    float getDamageMultiplier(int attackerId, int targetId) const;
    std::string getUnitFastMove(int unitId) const;
    std::string getUnitChargedMove(int unitId) const;
    std::optional<MoveSnapshot> getMove(const std::string& name) const;
    bool hasPlannedMove(int unitId) const;
    bool isMoving(int unitId) const;

    // Drain and clear queued script events.
    std::vector<ScriptEvent> drainEvents();

    // Command queue: apply queued commands in order.
    void flush();

    // ---- Command surface (queued unless noted) ----
    void emit(const std::string& tagOrMsg, const std::optional<std::string>& payload);
    void emitCatch(const std::string& msg);
    void emitGold(const std::string& msg);
    void spawnPokemon(const std::string& name, float x, float y, float z);
    void spawnOnGrid(const std::string& name, int col, int row, PokemonSide side, int level);
    void pushState(const std::string& scriptPath);
    void pushCombatState(const std::string& scriptPath);
    void popState();
    void addToBench(const std::string& name, int level);
    bool applyMove(int unitId, int col, int row);
    bool commitMove(int unitId, int col, int row);
    void faceEnemy(int unitId, const std::optional<int>& tgtCol, const std::optional<int>& tgtRow);
    void faceTarget(int unitId, int targetId);
    bool setEnergy(int unitId, int value);
    int addEnergy(int unitId, int delta);

    // Applies damage immediately (animation timing and gating are sensitive).
    int applyDamage(int attackerId,
                    int targetId,
                    int amount,
                    const std::optional<float>& cadenceSec,
                    const std::optional<std::string>& moveName,
                    const std::optional<std::string>& kind);

private:
    struct EmitCommand {
        std::string tag;
        std::string payload;
        bool hasPayload = false;
    };
    struct SpawnCommand {
        std::string name;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };
    struct SpawnOnGridCommand {
        std::string name;
        int col = 0;
        int row = 0;
        PokemonSide side = PokemonSide::Player;
        int level = -1;
    };
    struct PushStateCommand {
        std::string scriptPath;
    };
    struct PushCombatStateCommand {
        std::string scriptPath;
    };
    struct PopStateCommand {};
    struct AddToBenchCommand {
        std::string name;
        int level = -1;
    };
    struct ApplyMoveCommand {
        int unitId = -1;
        int col = 0;
        int row = 0;
    };
    struct CommitMoveCommand {
        int unitId = -1;
        int col = 0;
        int row = 0;
    };
    struct FaceEnemyCommand {
        int unitId = -1;
        bool hasTarget = false;
        int col = 0;
        int row = 0;
    };
    struct FaceTargetCommand {
        int unitId = -1;
        int targetId = -1;
    };
    struct SetEnergyCommand {
        int unitId = -1;
        int value = 0;
    };
    struct AddEnergyCommand {
        int unitId = -1;
        int delta = 0;
    };
    struct StartNewGameCommand {
        std::string mode;
    };

    using Command = std::variant<
        EmitCommand,
        SpawnCommand,
        SpawnOnGridCommand,
        PushStateCommand,
        PushCombatStateCommand,
        PopStateCommand,
        AddToBenchCommand,
        ApplyMoveCommand,
        CommitMoveCommand,
        FaceEnemyCommand,
        FaceTargetCommand,
        SetEnergyCommand,
        AddEnergyCommand,
        StartNewGameCommand
    >;

    void enqueue(Command cmd);
    void applyCommand(const Command& cmd);

    GameWorld* world_ = nullptr;
    GameStateManager* manager_ = nullptr;
    GameServices& services_;
    std::vector<Command> queue_;
};
