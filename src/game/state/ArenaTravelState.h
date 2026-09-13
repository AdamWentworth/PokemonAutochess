#pragma once

#include "game/GameState.h"
#include "game/arena/CombatMap.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

class GameWorld;
class GameStateManager;
struct GameServices;

// Shared recall/arrival presentation for real rounds and the replayable Travel Test.
class ArenaTravelState final : public GameState {
public:
    enum class Phase { Hold, Recall, Cover, Load, Warm, Reveal, Throw, SendOut, Ready, Failed };
    ArenaTravelState(GameWorld& world, GameServices& services, std::string sourceScript);
    // Normal rounds retain their current arena while encounter scripts advance.
    ArenaTravelState(GameWorld& world, GameServices& services, std::string sourceScript,
                     GameStateManager& manager, std::string nextShopScript);
    ~ArenaTravelState() override;
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;
    void handleInput(const InputEvent& event) override;
    const std::string& debugScriptPath() const { return currentScript_; }
    const std::string& nextShopScriptPath() const { return nextShopScript_; }
    Phase phase() const { return phase_; }
    float coverAlpha() const;
    // Called only after a real destination world draw, not by fixed updates/prewarming.
    void worldFramePresented(bool ready);

private:
    struct Slot {
        int id = -1;
        bool bench = false;
        int benchSlot = -1;
        game::arena::Cell cell;
        glm::vec3 rotation{};
    };
    void begin();
    void captureFormation();
    void enter(Phase phase);
    void updateVisuals();
    bool loadDestination();
    void planningFlags();
    GameWorld& world_;
    GameServices& services_;
    GameStateManager* manager_ = nullptr;
    std::string nextShopScript_;
    std::string currentScript_, destinationScript_, error_;
    std::vector<Slot> formation_;
    Phase phase_ = Phase::Hold;
    float elapsed_ = 0;
    bool started_ = false, coverPresented_ = false, exited_ = false;
    int warmFrames_ = 0, relocated_ = 0;
};
