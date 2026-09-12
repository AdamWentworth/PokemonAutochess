#pragma once

#include "game/GameState.h"
#include "game/arena/CombatMap.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

class GameWorld;
struct GameServices;

// A replayable environment travel prototype. It owns presentation, not the roster.
class ArenaTravelState final : public GameState {
public:
    enum class Phase { Hold, Recall, Cover, Load, Warm, Reveal, Throw, SendOut, Ready, Failed };
    ArenaTravelState(GameWorld& world, GameServices& services, std::string sourceScript);
    ~ArenaTravelState() override;
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;
    void handleInput(const InputEvent& event) override;
    const std::string& debugScriptPath() const { return currentScript_; }
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
    void enter(Phase phase);
    void updateVisuals();
    bool loadDestination();
    void planningFlags();
    GameWorld& world_;
    GameServices& services_;
    std::string currentScript_, destinationScript_, error_;
    std::vector<Slot> formation_;
    Phase phase_ = Phase::Hold;
    float elapsed_ = 0;
    bool started_ = false, coverPresented_ = false, exited_ = false;
    int warmFrames_ = 0, relocated_ = 0;
};
