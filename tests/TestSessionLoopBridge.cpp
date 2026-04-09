#include "game/logging/LogBus.h"
#include "game/runtime/session/SessionLoopBridge.h"
#include "game/runtime/session/SessionWorldBackdrop.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

class ScopedCoutCapture {
public:
    ScopedCoutCapture()
        : old_(std::cout.rdbuf(buffer_.rdbuf())) {}

    ~ScopedCoutCapture() {
        std::cout.rdbuf(old_);
    }

    std::string str() const {
        return buffer_.str();
    }

private:
    std::ostringstream buffer_;
    std::streambuf* old_ = nullptr;
};

} // namespace

bool test_session_loop_bridge_contract(std::string& outFail) {
    using game::runtime::session_loop_bridge::Context;
    using game::runtime::session_loop_bridge::handleEvent;
    using game::runtime::session_loop_runtime::PauseState;
    using game::runtime::session_world_backdrop::defaultRoute1BackdropTuningState;

    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);
    log.info("seed");

    PauseState pauseState;
    auto route1BackdropTuning = defaultRoute1BackdropTuningState();

    ScopedCoutCapture capture;
    handleEvent(
        InputEvent::KeyDownEvent(InputEvent::Key::F8),
        Context{
            .log = &log,
            .pauseState = &pauseState,
            .route1BackdropTuning = &route1BackdropTuning,
        });
    handleEvent(
        InputEvent::KeyDownEvent(InputEvent::Key::Right),
        Context{
            .log = &log,
            .pauseState = &pauseState,
            .route1BackdropTuning = &route1BackdropTuning,
        });

    const auto mainLines = log.recentMainLines(4);
    if (mainLines.size() != 1u || mainLines[0].text != "seed") {
        outFail =
            "SessionLoopBridge Route 1 tuning logs should stay off the main recent log feed.";
        return false;
    }

    if (!route1BackdropTuning.enabled ||
        route1BackdropTuning.offsetXCells != 0.25f) {
        outFail =
            "SessionLoopBridge should toggle Route 1 tuning on and apply arrow-key offsets.";
        return false;
    }

    const std::string terminalOutput = capture.str();
    if (terminalOutput.find("[Backdrop][Route1Tune] ON") == std::string::npos ||
        terminalOutput.find("[Backdrop][Route1Tune] Controls:") == std::string::npos ||
        terminalOutput.find("offset_x_cells=0.250") == std::string::npos) {
        outFail =
            "SessionLoopBridge should emit Route 1 tuning updates through the terminal-only logging path.";
        return false;
    }

    return true;
}
