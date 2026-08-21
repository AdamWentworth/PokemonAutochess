#include "game/editor/PokemonAutochessEditorSceneMutationSession.h"

#include <algorithm>
#include <string>

namespace game::editor::scene_mutation_session {

namespace route1 =
    game::runtime::route1_environment;
using scene_mutations::BoardClearanceConfig;
using scene_mutations::BoardClearancePlan;

Session::Session(
    route1::RuntimeEnvironment& environment,
    persistence::Store& persistence,
    bool sceneMounted,
    const std::filesystem::path& authoredScenePath) noexcept
    : environment_(environment),
      persistence_(persistence),
      sceneMounted_(sceneMounted),
      authoredScenePath_(authoredScenePath) {}

bool Session::saveAuthoredScene(std::string* outError) {
    return persistence_.saveAuthoredScene(
        sceneMounted_,
        authoredScenePath_,
        environment_.authoredScene(),
        outError);
}

bool Session::saveBoardRegistration(std::string* outError) {
    return persistence_.saveBoardRegistration(
        sceneMounted_, environment_.layout(), outError);
}

void Session::rollback(const Layout& layout) noexcept {
    std::string ignored;
    environment_.applyBoardLayout(layout, &ignored);
}

bool Session::applyAuthoredLayout(
    const Layout& next,
    const Layout& rollbackLayout,
    std::string* outError,
    FailureStage* outFailureStage) {
    if (outFailureStage) {
        *outFailureStage = FailureStage::None;
    }
    if (!environment_.applyBoardLayout(next, outError)) {
        if (outFailureStage) {
            *outFailureStage = FailureStage::Apply;
        }
        rollback(rollbackLayout);
        return false;
    }
    if (!saveAuthoredScene(outError)) {
        if (outFailureStage) {
            *outFailureStage =
                FailureStage::AuthoredPersistence;
        }
        rollback(rollbackLayout);
        return false;
    }
    return true;
}

bool Session::applyBoardRegistration(
    const Layout& next,
    const Layout& rollbackLayout,
    std::string* outError,
    FailureStage* outFailureStage) {
    if (outFailureStage) {
        *outFailureStage = FailureStage::None;
    }
    if (!environment_.applyBoardLayout(next, outError)) {
        if (outFailureStage) {
            *outFailureStage = FailureStage::Apply;
        }
        rollback(rollbackLayout);
        return false;
    }
    if (!saveBoardRegistration(outError)) {
        if (outFailureStage) {
            *outFailureStage = FailureStage::BoardPersistence;
        }
        rollback(rollbackLayout);
        return false;
    }
    return true;
}

bool Session::applyHistoryLayout(
    const Layout& target,
    const Layout& rollbackLayout,
    std::string* outError,
    FailureStage* outFailureStage) {
    if (outFailureStage) {
        *outFailureStage = FailureStage::None;
    }
    if (!environment_.applyBoardLayout(target, outError)) {
        if (outFailureStage) {
            *outFailureStage = FailureStage::Apply;
        }
        rollback(rollbackLayout);
        return false;
    }
    if (!saveAuthoredScene(outError)) {
        if (outFailureStage) {
            *outFailureStage =
                FailureStage::AuthoredPersistence;
        }
        rollback(rollbackLayout);
        return false;
    }
    if (!saveBoardRegistration(outError)) {
        if (outFailureStage) {
            *outFailureStage = FailureStage::BoardPersistence;
        }
        rollback(rollbackLayout);
        return false;
    }
    return true;
}

bool Session::setObjectOverride(
    const engine::editor::EditorProjectLayoutEdit& edit,
    Layout& outPrevious,
    std::string* outError) {
    outPrevious = environment_.layout();
    if (!environment_.setLayoutObjectOverride(
            edit.stableId,
            edit.translation,
            edit.rotationDegrees,
            edit.scale,
            edit.suppressed,
            edit.reason
                ? edit.reason
                : "autochess_board_clearance",
            outError)) {
        return false;
    }
    std::string error;
    if (!saveAuthoredScene(&error)) {
        rollback(outPrevious);
        if (outError) {
            *outError =
                "Could not persist the layout override; the in-memory edit was rolled back: " +
                error;
        }
        return false;
    }
    return true;
}

bool Session::resetObjectOverride(
    std::string_view stableId,
    Layout& outPrevious,
    std::string* outError) {
    outPrevious = environment_.layout();
    if (!environment_.resetLayoutObjectOverride(
            std::string(stableId), outError)) {
        return false;
    }
    std::string error;
    if (!saveAuthoredScene(&error)) {
        rollback(outPrevious);
        if (outError) {
            *outError =
                "Could not persist the layout reset; the in-memory edit was rolled back: " +
                error;
        }
        return false;
    }
    return true;
}

bool Session::duplicateObject(
    std::string_view stableId,
    Layout& outPrevious,
    std::string& outCreatedStableId,
    std::string* outError) {
    outPrevious = environment_.layout();
    outCreatedStableId.clear();
    std::string error;
    if (!environment_.duplicateLayoutObject(
            std::string(stableId),
            outCreatedStableId,
            &error) ||
        !saveAuthoredScene(&error)) {
        rollback(outPrevious);
        outCreatedStableId.clear();
        if (outError) {
            *outError =
                "Could not duplicate and persist the prefab instance: " +
                error;
        }
        return false;
    }
    return true;
}

bool Session::deleteObject(
    std::string_view stableId,
    Layout& outPrevious,
    std::string* outError) {
    outPrevious = environment_.layout();
    std::string error;
    if (!environment_.deleteLayoutObject(
            std::string(stableId), &error) ||
        !saveAuthoredScene(&error)) {
        rollback(outPrevious);
        if (outError) {
            *outError =
                "Could not delete and persist the scene object: " +
                error;
        }
        return false;
    }
    return true;
}

bool Session::deleteObjects(
    const char* const* stableIds,
    std::size_t stableIdCount,
    Layout& outPrevious,
    std::string* outError) {
    outPrevious = environment_.layout();
    std::string error;
    for (std::size_t index = 0u;
         index < stableIdCount;
         ++index) {
        if (!stableIds[index] ||
            !environment_.deleteLayoutObject(
                stableIds[index], &error)) {
            rollback(outPrevious);
            if (outError) {
                *outError = error.empty()
                    ? "A selected scene object had no stable ID."
                    : error;
            }
            return false;
        }
    }
    if (!saveAuthoredScene(&error)) {
        rollback(outPrevious);
        if (outError) {
            *outError =
                "Could not persist the batch scene edit: " +
                error;
        }
        return false;
    }
    return true;
}

bool Session::renameObject(
    std::string_view stableId,
    std::string_view displayName,
    Layout& outPrevious,
    std::string* outError) {
    outPrevious = environment_.layout();
    if (!environment_.renameLayoutObject(
            std::string(stableId),
            std::string(displayName),
            outError)) {
        return false;
    }
    std::string error;
    if (!saveAuthoredScene(&error)) {
        rollback(outPrevious);
        if (outError) {
            *outError =
                "Could not persist the scene object rename: " +
                error;
        }
        return false;
    }
    return true;
}

bool Session::reparentObject(
    std::string_view stableId,
    std::string_view categoryPath,
    Layout& outPrevious,
    std::string* outError) {
    outPrevious = environment_.layout();
    if (!environment_.reparentLayoutObject(
            std::string(stableId),
            std::string(categoryPath),
            outError)) {
        return false;
    }
    std::string error;
    if (!saveAuthoredScene(&error)) {
        rollback(outPrevious);
        if (outError) {
            *outError =
                "Could not persist the scene object reparent: " +
                error;
        }
        return false;
    }
    return true;
}

bool Session::applyBoardClearance(
    const BoardClearancePlan& plan,
    const BoardClearanceConfig& config,
    Layout& outPrevious,
    std::string* outError) {
    outPrevious = environment_.layout();
    std::string error;
    for (const auto& stableId : plan.suppressStableIds) {
        if (!environment_.deleteLayoutObject(stableId, &error)) {
            rollback(outPrevious);
            if (outError) {
                *outError =
                    "Could not suppress board obstruction " +
                    stableId + ": " + error;
            }
            return false;
        }
    }
    if (plan.result.groundInfillCreated) {
        auto next = environment_.layout();
        std::erase_if(
            next.authoredPrefabInstances,
            [&](const auto& candidate) {
                return candidate.stableId ==
                    config.groundInstanceStableId;
            });
        for (const auto& groundTile : plan.groundInfillTiles) {
            const auto tile = std::find_if(
                next.authoredTerrainTiles.begin(),
                next.authoredTerrainTiles.end(),
                [&](const auto& candidate) {
                    return candidate.gridX == groundTile.gridX &&
                        candidate.gridZ == groundTile.gridZ;
                });
            if (tile == next.authoredTerrainTiles.end()) {
                next.authoredTerrainTiles.push_back(groundTile);
            } else {
                *tile = groundTile;
            }
        }
        if (!environment_.applyBoardLayout(next, &error)) {
            rollback(outPrevious);
            if (outError) {
                *outError =
                    "Could not create the board ground infill: " +
                    error;
            }
            return false;
        }
    }
    if (!saveAuthoredScene(&error)) {
        rollback(outPrevious);
        if (outError) {
            *outError =
                "Could not persist the board clearing: " + error;
        }
        return false;
    }
    return true;
}

} // namespace game::editor::scene_mutation_session
