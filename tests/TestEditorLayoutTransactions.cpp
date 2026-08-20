#include "game/editor/PokemonAutochessEditorLayoutTransactions.h"

#include <fstream>
#include <iterator>
#include <string>

bool test_editor_layout_transactions_contract(std::string& outFail) {
    namespace transactions = game::editor::layout_transactions;
    namespace route1 = game::runtime::lgpe_route1_runtime;

    transactions::SceneLiveEdit sceneEdit;
    if (sceneEdit.active() || sceneEdit.baseline() ||
        sceneEdit.canCancel(nullptr)) {
        outFail = "A live scene edit should begin inactive.";
        return false;
    }

    route1::BoardLayoutTransform firstBaseline;
    firstBaseline.yawDegrees = 10.0f;
    sceneEdit.beginIfNeeded("environment/tree-01", firstBaseline);
    route1::BoardLayoutTransform ignoredReplacement;
    ignoredReplacement.yawDegrees = 20.0f;
    sceneEdit.beginIfNeeded(
        "environment/tree-01",
        ignoredReplacement);
    if (!sceneEdit.active() ||
        !sceneEdit.targets("environment/tree-01") ||
        sceneEdit.conflictsWith("environment/tree-01") ||
        !sceneEdit.conflictsWith("environment/tree-02") ||
        !sceneEdit.canCancel(nullptr) ||
        !sceneEdit.canCancel("environment/tree-01") ||
        sceneEdit.canCancel("environment/tree-02") ||
        !sceneEdit.baseline() ||
        sceneEdit.baseline()->yawDegrees != 10.0f) {
        outFail =
            "A repeated live-edit preview should retain the first baseline and target identity.";
        return false;
    }

    route1::BoardLayoutTransform secondBaseline;
    secondBaseline.yawDegrees = 30.0f;
    sceneEdit.beginIfNeeded("environment/tree-02", secondBaseline);
    const auto copiedBaseline = sceneEdit.baselineCopy();
    if (!copiedBaseline || copiedBaseline->yawDegrees != 30.0f ||
        sceneEdit.targetId() != "environment/tree-02") {
        outFail =
            "Changing live-edit targets should atomically replace the target and baseline.";
        return false;
    }
    sceneEdit.clear();
    if (sceneEdit.active() || !sceneEdit.targetId().empty() ||
        sceneEdit.baseline()) {
        outFail = "Clearing a live edit should discard its target and rollback baseline.";
        return false;
    }

    transactions::PreviewUnitLiveEdit previewEdit;
    previewEdit.beginIfNeeded(
        "game-preview/player/bulbasaur",
        transactions::PreviewUnitTransform{
            .position = {1.0f, 2.0f, 3.0f},
            .rotationDegrees = {4.0f, 5.0f, 6.0f}});
    if (!previewEdit.baseline() ||
        previewEdit.baseline()->position[2] != 3.0f ||
        previewEdit.baseline()->rotationDegrees[1] != 5.0f) {
        outFail =
            "Gameplay-preview unit edits should use the same typed baseline lifecycle.";
        return false;
    }

    const auto layoutWithYaw = [](float yaw) {
        route1::BoardLayoutTransform layout;
        layout.yawDegrees = yaw;
        return layout;
    };
    transactions::SceneEditHistory history(2u);
    if (history.canUndo() || history.canRedo() || history.undoTarget() ||
        history.redoTarget() || history.acceptUndo(layoutWithYaw(1.0f)) ||
        history.acceptRedo(layoutWithYaw(1.0f))) {
        outFail = "An empty scene-edit history should reject undo and redo transitions.";
        return false;
    }

    history.record(layoutWithYaw(10.0f));
    history.record(layoutWithYaw(20.0f));
    history.record(layoutWithYaw(30.0f));
    if (history.undoCount() != 2u || history.redoCount() != 0u ||
        !history.undoTarget() || history.undoTarget()->yawDegrees != 30.0f) {
        outFail =
            "Scene-edit history should retain the newest records up to its configured limit.";
        return false;
    }

    const auto* pendingUndo = history.undoTarget();
    if (!pendingUndo || pendingUndo->yawDegrees != 30.0f ||
        history.undoCount() != 2u || history.redoCount() != 0u) {
        outFail =
            "Inspecting an undo target must not consume history before external persistence succeeds.";
        return false;
    }
    if (!history.acceptUndo(layoutWithYaw(40.0f)) ||
        history.undoCount() != 1u || history.redoCount() != 1u ||
        !history.undoTarget() || history.undoTarget()->yawDegrees != 20.0f ||
        !history.redoTarget() || history.redoTarget()->yawDegrees != 40.0f) {
        outFail =
            "Accepting an undo should move the current layout to the redo stack.";
        return false;
    }
    if (!history.acceptUndo(layoutWithYaw(50.0f)) ||
        !history.acceptRedo(layoutWithYaw(60.0f)) ||
        history.undoCount() != 1u || history.redoCount() != 1u ||
        !history.undoTarget() || history.undoTarget()->yawDegrees != 60.0f ||
        !history.redoTarget() || history.redoTarget()->yawDegrees != 40.0f) {
        outFail =
            "Undo and redo acceptance should move only the externally confirmed transition.";
        return false;
    }

    history.record(layoutWithYaw(70.0f));
    if (!history.canUndo() || history.canRedo() ||
        history.undoCount() != 2u || history.redoCount() != 0u ||
        !history.undoTarget() || history.undoTarget()->yawDegrees != 70.0f) {
        outFail =
            "Recording a new scene edit should clear the redo branch and preserve the history limit.";
        return false;
    }
    history.clear();
    if (history.canUndo() || history.canRedo() ||
        history.undoCount() != 0u || history.redoCount() != 0u) {
        outFail = "Clearing scene-edit history should remove both branches.";
        return false;
    }

    std::ifstream pluginSource("tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find("PokemonAutochessEditorLayoutTransactions.h") ==
            std::string::npos ||
        pluginText.find("layoutEditBaseline_") != std::string::npos ||
        pluginText.find("sceneUndoStack_") != std::string::npos ||
        pluginText.find("previewUnitEditBaseline_") != std::string::npos ||
        pluginText.find("sceneEditHistory_") == std::string::npos) {
        outFail =
            "The editor plugin should delegate live-edit and undo/redo state ownership to the transaction component.";
        return false;
    }

    return true;
}
