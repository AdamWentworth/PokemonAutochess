#pragma once

#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game::editor::layout_transactions {

template <typename State>
class LiveEdit {
public:
    void beginIfNeeded(
        std::string_view targetId,
        const State& baseline) {
        if (baseline_ && targetId_ == targetId) {
            return;
        }
        targetId_.assign(targetId);
        baseline_ = baseline;
    }

    bool active() const noexcept {
        return baseline_.has_value();
    }

    bool targets(std::string_view targetId) const noexcept {
        return active() && targetId_ == targetId;
    }

    bool conflictsWith(std::string_view targetId) const noexcept {
        return active() && targetId_ != targetId;
    }

    bool canCancel(const char* targetId) const noexcept {
        return active() && (!targetId || targets(targetId));
    }

    const std::string& targetId() const noexcept {
        return targetId_;
    }

    const State* baseline() const noexcept {
        return baseline_ ? &*baseline_ : nullptr;
    }

    std::optional<State> baselineCopy() const {
        return baseline_;
    }

    void clear() noexcept {
        baseline_.reset();
        targetId_.clear();
    }

private:
    std::string targetId_;
    std::optional<State> baseline_;
};

struct PreviewUnitTransform {
    std::array<float, 3> position{};
    std::array<float, 3> rotationDegrees{};
};

using SceneLiveEdit = LiveEdit<
    game::runtime::lgpe_route1_runtime::BoardLayoutTransform>;
using PreviewUnitLiveEdit = LiveEdit<PreviewUnitTransform>;

class SceneEditHistory {
public:
    explicit SceneEditHistory(std::size_t limit = 128u) noexcept;

    void clear() noexcept;
    void record(
        game::runtime::lgpe_route1_runtime::BoardLayoutTransform previous);

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform*
    undoTarget() const noexcept;
    const game::runtime::lgpe_route1_runtime::BoardLayoutTransform*
    redoTarget() const noexcept;
    bool acceptUndo(
        game::runtime::lgpe_route1_runtime::BoardLayoutTransform current);
    bool acceptRedo(
        game::runtime::lgpe_route1_runtime::BoardLayoutTransform current);

    std::size_t undoCount() const noexcept;
    std::size_t redoCount() const noexcept;

private:
    std::size_t limit_ = 128u;
    std::vector<
        game::runtime::lgpe_route1_runtime::BoardLayoutTransform>
        undo_;
    std::vector<
        game::runtime::lgpe_route1_runtime::BoardLayoutTransform>
        redo_;
};

} // namespace game::editor::layout_transactions
