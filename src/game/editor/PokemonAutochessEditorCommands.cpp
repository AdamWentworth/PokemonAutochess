#include "game/editor/PokemonAutochessEditorCommands.h"

#include <array>

namespace game::editor::commands {

namespace {

constexpr std::array<engine::editor::EditorProjectCommandField, 6>
    kBoardClearanceFields{{
        {.id = "padding_cells",
         .displayName = "Clearance padding (cells)",
         .description =
             "Additional Route 1 terrain-cell margin around the board and benches.",
         .kind = engine::editor::EditorProjectCommandFieldKind::Float,
         .defaultFloat = 0.35f,
         .minimumFloat = 0.0f,
         .maximumFloat = 3.0f,
         .stepFloat = 0.05f},
        {.id = "clear_terrain",
         .displayName = "Clear ledges and raised terrain",
         .defaultBoolean = true},
        {.id = "clear_vegetation",
         .displayName = "Clear vegetation",
         .defaultBoolean = true},
        {.id = "clear_objects",
         .displayName = "Clear props and other obstructions",
         .defaultBoolean = true},
        {.id = "retain_ramps",
         .displayName = "Retain ramps as entrances",
         .defaultBoolean = true},
        {.id = "add_ground_infill",
         .displayName = "Create light-lawn ground infill",
         .defaultBoolean = true},
    }};

} // namespace

std::size_t count(bool route1Available) noexcept {
    return route1Available ? 2u : 0u;
}

engine::editor::EditorProjectCommand command(
    bool route1Available,
    std::size_t index) noexcept {
    if (!route1Available) {
        return {};
    }
    if (index == 0u) {
        return {
            .id = kClearBoardFootprintId.data(),
            .displayName = "Autochess Board Clearing",
            .category = "PokemonAutochess / Route 1",
            .description =
                "Suppress Route 1 obstructions, flatten covered cells to the registered board level, and rebuild clean lawn below the board and benches.",
            .buttonLabel = "Clear + Flatten Board Footprint",
            .confirmationText =
                "Apply the selected clearing operations to the Route 1 board footprint? This autosaves as one undoable PokemonAutochess scene edit.",
            .fields = kBoardClearanceFields.data(),
            .fieldCount = kBoardClearanceFields.size(),
            .confirmationRequired = true};
    }
    if (index == 1u) {
        return {
            .id = kResetImportedSceneId.data(),
            .displayName = "Restore Imported Route 1",
            .category = "PokemonAutochess / Route 1",
            .description =
                "Remove every PokemonAutochess-authored Route 1 node and restore the promoted imported source baseline.",
            .buttonLabel = "Reset Entire Scene To Imported Source",
            .confirmationText =
                "Restore the imported Route 1 baseline and remove all authored layout work? This is undoable, but it intentionally replaces the current authored scene state.",
            .confirmationRequired = true};
    }
    return {};
}

Kind resolve(std::string_view commandId) noexcept {
    if (commandId == kClearBoardFootprintId) {
        return Kind::ClearBoardFootprint;
    }
    if (commandId == kResetImportedSceneId) {
        return Kind::ResetImportedScene;
    }
    return Kind::Unknown;
}

bool parseBoardClearanceRequest(
    const engine::editor::EditorProjectCommandValue* values,
    std::size_t valueCount,
    BoardClearanceRequest& outRequest,
    std::string* outError) {
    outRequest = {};
    for (std::size_t index = 0u;
         values && index < valueCount;
         ++index) {
        const std::string_view field =
            values[index].id ? values[index].id : "";
        if (field == "padding_cells") {
            outRequest.paddingCells = values[index].floatValue;
        } else if (field == "clear_terrain") {
            outRequest.clearTerrain = values[index].booleanValue;
        } else if (field == "clear_vegetation") {
            outRequest.clearVegetation = values[index].booleanValue;
        } else if (field == "clear_objects") {
            outRequest.clearObjects = values[index].booleanValue;
        } else if (field == "retain_ramps") {
            outRequest.retainRamps = values[index].booleanValue;
        } else if (field == "add_ground_infill") {
            outRequest.addGroundInfill = values[index].booleanValue;
        }
    }
    if (!outRequest.clearTerrain &&
        !outRequest.clearVegetation &&
        !outRequest.clearObjects &&
        !outRequest.addGroundInfill) {
        if (outError) {
            *outError =
                "Enable at least one Route 1 clearing or infill option.";
        }
        return false;
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

std::string boardClearanceStatus(
    const BoardClearanceResult& result) {
    std::string status =
        "Board clearing autosaved: " +
        std::to_string(result.suppressedTerrainCount) +
        " terrain, " +
        std::to_string(result.suppressedVegetationCount) +
        " vegetation, " +
        std::to_string(result.suppressedObjectCount) +
        " object obstructions suppressed" +
        (result.groundInfillCreated
             ? "; ground infill created."
             : ".");
    if (result.skippedUnsafeAggregateCount > 0u) {
        status +=
            " " +
            std::to_string(result.skippedUnsafeAggregateCount) +
            " broad source foliage layers were preserved because their editable boundaries are not safe.";
    }
    return status;
}

std::string unknownCommandError(std::string_view commandId) {
    return "Unknown PokemonAutochess editor command: " +
        std::string(commandId);
}

} // namespace game::editor::commands
