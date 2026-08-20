#include "game/editor/PokemonAutochessEditorLayoutTransactions.h"

#include <algorithm>

namespace game::editor::layout_transactions {

SceneEditHistory::SceneEditHistory(std::size_t limit) noexcept
    : limit_(std::max<std::size_t>(1u, limit)) {}

void SceneEditHistory::clear() noexcept {
    undo_.clear();
    redo_.clear();
}

void SceneEditHistory::record(
    game::runtime::lgpe_route1_runtime::BoardLayoutTransform previous) {
    undo_.push_back(std::move(previous));
    if (undo_.size() > limit_) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
}

bool SceneEditHistory::canUndo() const noexcept {
    return !undo_.empty();
}

bool SceneEditHistory::canRedo() const noexcept {
    return !redo_.empty();
}

const game::runtime::lgpe_route1_runtime::BoardLayoutTransform*
SceneEditHistory::undoTarget() const noexcept {
    return undo_.empty() ? nullptr : &undo_.back();
}

const game::runtime::lgpe_route1_runtime::BoardLayoutTransform*
SceneEditHistory::redoTarget() const noexcept {
    return redo_.empty() ? nullptr : &redo_.back();
}

bool SceneEditHistory::acceptUndo(
    game::runtime::lgpe_route1_runtime::BoardLayoutTransform current) {
    if (undo_.empty()) {
        return false;
    }
    undo_.pop_back();
    redo_.push_back(std::move(current));
    return true;
}

bool SceneEditHistory::acceptRedo(
    game::runtime::lgpe_route1_runtime::BoardLayoutTransform current) {
    if (redo_.empty()) {
        return false;
    }
    redo_.pop_back();
    undo_.push_back(std::move(current));
    return true;
}

std::size_t SceneEditHistory::undoCount() const noexcept {
    return undo_.size();
}

std::size_t SceneEditHistory::redoCount() const noexcept {
    return redo_.size();
}

EditSession::EditSession(std::size_t historyLimit) noexcept
    : sceneEditHistory_(historyLimit) {}

void EditSession::clearSceneState() noexcept {
    sceneLiveEdit_.clear();
    sceneEditHistory_.clear();
}

void EditSession::clearSceneLiveEdit() noexcept {
    sceneLiveEdit_.clear();
}

void EditSession::clearPreviewUnitLiveEdit() noexcept {
    previewUnitLiveEdit_.clear();
}

bool EditSession::previewUnitLiveEditActive() const noexcept {
    return previewUnitLiveEdit_.active();
}

void EditSession::beginSceneLiveEdit(
    std::string_view targetId,
    const Layout& baseline) {
    sceneLiveEdit_.beginIfNeeded(targetId, baseline);
}

void EditSession::beginPreviewUnitLiveEdit(
    std::string_view targetId,
    const PreviewUnitTransform& baseline) {
    previewUnitLiveEdit_.beginIfNeeded(targetId, baseline);
}

EditSession::SceneCommitPlan EditSession::prepareSceneCommit(
    std::string_view targetId) const {
    return {
        .targetConflict = sceneLiveEdit_.conflictsWith(targetId),
        .baseline = sceneLiveEdit_.baselineCopy()};
}

EditSession::PreviewUnitCommitPlan
EditSession::preparePreviewUnitCommit(
    std::string_view targetId) const {
    return {
        .targetConflict =
            previewUnitLiveEdit_.conflictsWith(targetId),
        .baseline = previewUnitLiveEdit_.baselineCopy()};
}

void EditSession::acceptSceneCommit(SceneCommitPlan plan) {
    if (plan.baseline) {
        sceneEditHistory_.record(std::move(*plan.baseline));
    }
    sceneLiveEdit_.clear();
}

void EditSession::finishSceneLiveEdit() noexcept {
    sceneLiveEdit_.clear();
}

void EditSession::finishPreviewUnitLiveEdit() noexcept {
    previewUnitLiveEdit_.clear();
}

std::optional<EditSession::Layout>
EditSession::cancelSceneLiveEdit(const char* targetId) {
    auto baseline = sceneLiveEdit_.canCancel(targetId)
        ? sceneLiveEdit_.baselineCopy()
        : std::nullopt;
    sceneLiveEdit_.clear();
    return baseline;
}

std::optional<PreviewUnitRollback>
EditSession::cancelPreviewUnitLiveEdit(const char* targetId) {
    if (!previewUnitLiveEdit_.canCancel(targetId)) {
        return std::nullopt;
    }
    PreviewUnitRollback rollback{
        .targetId = previewUnitLiveEdit_.targetId(),
        .baseline = *previewUnitLiveEdit_.baseline()};
    previewUnitLiveEdit_.clear();
    return rollback;
}

void EditSession::recordSceneEdit(Layout previous) {
    sceneEditHistory_.record(std::move(previous));
}

bool EditSession::canUndo() const noexcept {
    return sceneEditHistory_.canUndo();
}

bool EditSession::canRedo() const noexcept {
    return sceneEditHistory_.canRedo();
}

std::optional<EditSession::Layout>
EditSession::undoTarget() const {
    const auto* target = sceneEditHistory_.undoTarget();
    return target ? std::optional<Layout>(*target) : std::nullopt;
}

std::optional<EditSession::Layout>
EditSession::redoTarget() const {
    const auto* target = sceneEditHistory_.redoTarget();
    return target ? std::optional<Layout>(*target) : std::nullopt;
}

bool EditSession::acceptUndo(Layout current) {
    if (!sceneEditHistory_.acceptUndo(std::move(current))) {
        return false;
    }
    sceneLiveEdit_.clear();
    return true;
}

bool EditSession::acceptRedo(Layout current) {
    if (!sceneEditHistory_.acceptRedo(std::move(current))) {
        return false;
    }
    sceneLiveEdit_.clear();
    return true;
}

} // namespace game::editor::layout_transactions
