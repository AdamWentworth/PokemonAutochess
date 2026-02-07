// src/game/scripting/ScriptAPI.h
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

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

    GameWorld* world() const { return world_; }
    GameStateManager* manager() const { return manager_; }
    LogBus::Logger& logger() const;
    ScriptEventBus& events() const;
    const GameConfigData& config() const;

    // Drain and clear queued script events.
    std::vector<ScriptEvent> drainEvents();

    // Command queue: apply queued commands in order.
    void flush();

    // ---- Command surface (queued unless noted) ----
    void emit(const std::string& tagOrMsg, const std::optional<std::string>& payload);
    void spawnPokemon(const std::string& name, float x, float y, float z);
    void spawnOnGrid(const std::string& name, int col, int row, PokemonSide side, int level);
    void pushState(const std::string& scriptPath);
    void popState();
    bool applyMove(int unitId, int col, int row);
    bool commitMove(int unitId, int col, int row);
    void faceEnemy(int unitId, const std::optional<int>& tgtCol, const std::optional<int>& tgtRow);
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
    struct PopStateCommand {};
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
        PopStateCommand,
        ApplyMoveCommand,
        CommitMoveCommand,
        FaceEnemyCommand,
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
