#pragma once

#include <algorithm>
#include <cmath>

namespace game::runtime::ui_frontend {

struct IntroConfig {
    bool enabled = false;
    float holdSeconds = 0.65f;
    float moveSeconds = 1.90f;
    float settleSeconds = 0.15f;
    float fadeSeconds = 0.65f;
    float focusU = 0.5f;
    float focusV = 0.5f;
    float zoom = 1.0f;
};

struct IntroFrame {
    float centerU = 0.5f;
    float centerV = 0.5f;
    float zoom = 1.0f;
    float uiAlpha = 1.0f;
    bool selectionReady = true;
};

// Presentation time belongs to the state, not rendering or viewport layout.
// A resize cannot restart the sequence or consume an extra animation tick.
class FrontendIntro {
  public:
    void reset(IntroConfig config = {}) {
        const auto finiteClamp = [](float value, float fallback, float lo, float hi) {
            return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
        };
        config.holdSeconds = finiteClamp(config.holdSeconds, .65f, 0.0f, 10.0f);
        config.moveSeconds = finiteClamp(config.moveSeconds, 1.90f, .01f, 10.0f);
        config.settleSeconds = finiteClamp(config.settleSeconds, .15f, 0.0f, 10.0f);
        config.fadeSeconds = finiteClamp(config.fadeSeconds, .65f, .01f, 10.0f);
        config.focusU = finiteClamp(config.focusU, .5f, 0.0f, 1.0f);
        config.focusV = finiteClamp(config.focusV, .5f, 0.0f, 1.0f);
        config.zoom = finiteClamp(config.zoom, 1.0f, 1.0f, 4.0f);
        config_ = config;
        elapsed_ = 0.0f;
    }

    void advance(float deltaSeconds) {
        if (config_.enabled && std::isfinite(deltaSeconds) && deltaSeconds > 0.0f)
            elapsed_ = std::min(duration(), elapsed_ + deltaSeconds);
    }

    IntroFrame frame() const {
        if (!config_.enabled) return {};
        if (elapsed_ >= duration()) {
            return {.centerU = config_.focusU, .centerV = config_.focusV,
                    .zoom = config_.zoom, .uiAlpha = 1.0f, .selectionReady = true};
        }
        const float move = ease((elapsed_ - config_.holdSeconds) / config_.moveSeconds);
        const float fadeStart = config_.holdSeconds + config_.moveSeconds + config_.settleSeconds;
        return {
            .centerU = .5f + (config_.focusU - .5f) * move,
            .centerV = .5f + (config_.focusV - .5f) * move,
            .zoom = 1.0f + (config_.zoom - 1.0f) * move,
            .uiAlpha = ease((elapsed_ - fadeStart) / config_.fadeSeconds),
            .selectionReady = elapsed_ >= duration()
        };
    }

  private:
    static float ease(float value) {
        const float t = std::clamp(value, 0.0f, 1.0f);
        // Zero velocity and acceleration at each end of the camera move.
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
    float duration() const {
        return config_.holdSeconds + config_.moveSeconds + config_.settleSeconds + config_.fadeSeconds;
    }
    IntroConfig config_;
    float elapsed_ = 0.0f;
};

} // namespace game::runtime::ui_frontend
