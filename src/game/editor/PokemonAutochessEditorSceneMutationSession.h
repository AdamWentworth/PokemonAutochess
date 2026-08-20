#pragma once

#include "engine/editor/EditorProjectPlugin.h"
#include "game/editor/PokemonAutochessEditorPersistence.h"
#include "game/editor/PokemonAutochessEditorSceneMutations.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace game::editor::scene_mutation_session {

class Session {
public:
    using Layout =
        game::runtime::lgpe_route1_runtime::BoardLayoutTransform;
    enum class FailureStage : std::uint8_t {
        None = 0u,
        Apply,
        AuthoredPersistence,
        BoardPersistence,
    };

    Session(
        game::runtime::lgpe_route1_runtime::RuntimeEnvironment& environment,
        persistence::Store& persistence,
        bool sceneMounted,
        const std::filesystem::path& authoredScenePath) noexcept;

    bool applyAuthoredLayout(
        const Layout& next,
        const Layout& rollback,
        std::string* outError = nullptr,
        FailureStage* outFailureStage = nullptr);
    bool applyBoardRegistration(
        const Layout& next,
        const Layout& rollback,
        std::string* outError = nullptr,
        FailureStage* outFailureStage = nullptr);
    bool applyHistoryLayout(
        const Layout& target,
        const Layout& rollback,
        std::string* outError = nullptr,
        FailureStage* outFailureStage = nullptr);

    bool setObjectOverride(
        const engine::editor::EditorProjectLayoutEdit& edit,
        Layout& outPrevious,
        std::string* outError = nullptr);
    bool resetObjectOverride(
        std::string_view stableId,
        Layout& outPrevious,
        std::string* outError = nullptr);
    bool duplicateObject(
        std::string_view stableId,
        Layout& outPrevious,
        std::string& outCreatedStableId,
        std::string* outError = nullptr);
    bool deleteObject(
        std::string_view stableId,
        Layout& outPrevious,
        std::string* outError = nullptr);
    bool deleteObjects(
        const char* const* stableIds,
        std::size_t stableIdCount,
        Layout& outPrevious,
        std::string* outError = nullptr);
    bool renameObject(
        std::string_view stableId,
        std::string_view displayName,
        Layout& outPrevious,
        std::string* outError = nullptr);
    bool reparentObject(
        std::string_view stableId,
        std::string_view categoryPath,
        Layout& outPrevious,
        std::string* outError = nullptr);
    bool applyBoardClearance(
        const scene_mutations::BoardClearancePlan& plan,
        const scene_mutations::BoardClearanceConfig& config,
        Layout& outPrevious,
        std::string* outError = nullptr);

private:
    bool saveAuthoredScene(std::string* outError);
    bool saveBoardRegistration(std::string* outError);
    void rollback(const Layout& layout) noexcept;

    game::runtime::lgpe_route1_runtime::RuntimeEnvironment& environment_;
    persistence::Store& persistence_;
    bool sceneMounted_ = false;
    const std::filesystem::path& authoredScenePath_;
};

} // namespace game::editor::scene_mutation_session
