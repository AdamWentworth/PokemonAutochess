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

} // namespace game::editor::layout_transactions
