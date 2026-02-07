// CombatSystem.h
#pragma once
#include "engine/core/Updatable.h"
#include <memory>
#include <sol/sol.hpp>

class GameWorld;
class ScriptAPI;
class ScriptEventBus;
namespace engine { class IAssetStore; }

class CombatSystem : public Updatable {
public:
    explicit CombatSystem(GameWorld* world,
                          ScriptEventBus* events = nullptr,
                          engine::IAssetStore* assets = nullptr);
    void update(float deltaTime) override;

private:
    GameWorld* gameWorld;
    sol::state lua;
    std::unique_ptr<ScriptAPI> api;
    engine::IAssetStore* assetStore = nullptr;
    bool ok = false;

    void loadScript();
};
