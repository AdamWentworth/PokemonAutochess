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

template <typename State>
struct LiveEditCommitPlan {
    bool targetConflict = false;
    std::optional<State> baseline;

    const State& rollbackOr(const State& current) const noexcept {
        return baseline ? *baseline : current;
    }
};

struct PreviewUnitRollback {
    std::string targetId;
    PreviewUnitTransform baseline;
};

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

class EditSession {
public:
    using Layout =
        game::runtime::lgpe_route1_runtime::BoardLayoutTransform;
    using SceneCommitPlan = LiveEditCommitPlan<Layout>;
    using PreviewUnitCommitPlan =
        LiveEditCommitPlan<PreviewUnitTransform>;

    explicit EditSession(std::size_t historyLimit = 128u) noexcept;

    void clearSceneState() noexcept;
    void clearSceneLiveEdit() noexcept;
    void clearPreviewUnitLiveEdit() noexcept;
    bool previewUnitLiveEditActive() const noexcept;

    void beginSceneLiveEdit(
        std::string_view targetId,
        const Layout& baseline);
    void beginPreviewUnitLiveEdit(
        std::string_view targetId,
        const PreviewUnitTransform& baseline);

    SceneCommitPlan prepareSceneCommit(
        std::string_view targetId) const;
    PreviewUnitCommitPlan preparePreviewUnitCommit(
        std::string_view targetId) const;
    void acceptSceneCommit(SceneCommitPlan plan);
    void finishSceneLiveEdit() noexcept;
    void finishPreviewUnitLiveEdit() noexcept;

    std::optional<Layout> cancelSceneLiveEdit(
        const char* targetId);
    std::optional<PreviewUnitRollback> cancelPreviewUnitLiveEdit(
        const char* targetId);

    void recordSceneEdit(Layout previous);
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    std::optional<Layout> undoTarget() const;
    std::optional<Layout> redoTarget() const;
    bool acceptUndo(Layout current);
    bool acceptRedo(Layout current);

private:
    SceneLiveEdit sceneLiveEdit_;
    PreviewUnitLiveEdit previewUnitLiveEdit_;
    SceneEditHistory sceneEditHistory_;
};

} // namespace game::editor::layout_transactions
