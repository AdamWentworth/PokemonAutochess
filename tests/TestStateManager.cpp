#include <memory>
#include <string>

#include "engine/input/InputEvent.h"
#include "game/GameState.h"
#include "game/GameStateManager.h"

namespace {
struct Tracker {
    int enters = 0;
    int exits = 0;
    int handled = 0;
    bool postPopWrite = false;
};

class TestState final : public GameState {
public:
    TestState(GameStateManager* manager, std::shared_ptr<Tracker> tracker, bool popOnInput)
        : manager_(manager), tracker_(std::move(tracker)), popOnInput_(popOnInput) {}

    void onEnter() override { tracker_->enters++; }
    void onExit() override { tracker_->exits++; }

    void handleInput(const InputEvent&) override {
        tracker_->handled++;
        if (popOnInput_ && manager_) {
            manager_->popState();
            // Must remain valid through end of this call.
            tracker_->postPopWrite = true;
        }
    }

    void update(float) override {}
    void render() override {}

private:
    GameStateManager* manager_ = nullptr;
    std::shared_ptr<Tracker> tracker_;
    bool popOnInput_ = false;
};
} // namespace

bool test_state_manager_input_deferral(std::string& outFail) {
    GameStateManager manager;
    auto baseTracker = std::make_shared<Tracker>();
    auto topTracker = std::make_shared<Tracker>();

    manager.pushState(std::make_unique<TestState>(&manager, baseTracker, false));
    manager.pushState(std::make_unique<TestState>(&manager, topTracker, true));

    const InputEvent click = InputEvent::MouseDownEvent(10, 10, InputEvent::MouseButton::Left);
    manager.handleInput(click);

    if (topTracker->handled != 1) {
        outFail = "Top state did not receive input.";
        return false;
    }
    if (!topTracker->postPopWrite) {
        outFail = "Top state did not finish handler after requesting pop.";
        return false;
    }
    if (topTracker->exits != 1) {
        outFail = "Top state was not popped exactly once.";
        return false;
    }

    // Second input should go to the base state after pop flush.
    manager.handleInput(click);
    if (baseTracker->handled != 1) {
        outFail = "Base state did not become active after top pop.";
        return false;
    }
    if (baseTracker->exits != 0) {
        outFail = "Base state was unexpectedly popped.";
        return false;
    }

    return true;
}
