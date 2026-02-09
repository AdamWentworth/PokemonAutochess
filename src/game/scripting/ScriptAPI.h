// src/game/scripting/ScriptAPI.h
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <utility>

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
        std::string fastMove;
        std::string chargedMove;
        std::vector<std::string> types;
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
    std::optional<UnitSnapshot> getUnitSnapshot(int unitId) const;
    std::pair<int, int> nearestEnemyCell(int unitId) const;
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
        AddEnergyCommand
    >;

    void enqueue(Command cmd);
    void applyCommand(const Command& cmd);

    GameWorld* world_ = nullptr;
    GameStateManager* manager_ = nullptr;
    GameServices& services_;
    std::vector<Command> queue_;
};
