#include "game/editor/PokemonAutochessEditorCommands.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

bool test_editor_commands_contract(std::string& outFail) {
    namespace commands = game::editor::commands;

    if (commands::count(false) != 0u || commands::count(true) != 2u ||
        commands::command(false, 0u).id ||
        commands::command(true, 2u).id) {
        outFail =
            "Project commands should publish exactly two Route 1 entries only while Route 1 is available.";
        return false;
    }

    const auto clearCommand = commands::command(true, 0u);
    const auto resetCommand = commands::command(true, 1u);
    if (!clearCommand.id ||
        std::string_view(clearCommand.id) != commands::kClearBoardFootprintId ||
        !clearCommand.displayName || !clearCommand.category ||
        !clearCommand.description || !clearCommand.buttonLabel ||
        !clearCommand.confirmationText || !clearCommand.confirmationRequired ||
        !clearCommand.fields || clearCommand.fieldCount != 6u ||
        !resetCommand.id ||
        std::string_view(resetCommand.id) != commands::kResetImportedSceneId ||
        !resetCommand.confirmationRequired || resetCommand.fields ||
        resetCommand.fieldCount != 0u) {
        outFail =
            "The Route 1 command catalog should preserve its stable IDs, display metadata, confirmations, and field ownership.";
        return false;
    }

    const std::array<std::string_view, 6> expectedFieldIds{{
        "padding_cells",
        "clear_terrain",
        "clear_vegetation",
        "clear_objects",
        "retain_ramps",
        "add_ground_infill",
    }};
    for (std::size_t index = 0u; index < expectedFieldIds.size(); ++index) {
        if (!clearCommand.fields[index].id ||
            expectedFieldIds[index] != clearCommand.fields[index].id ||
            !clearCommand.fields[index].displayName) {
            outFail = "The board-clearance command field order or metadata changed.";
            return false;
        }
    }
    const auto& paddingField = clearCommand.fields[0];
    if (paddingField.kind !=
            engine::editor::EditorProjectCommandFieldKind::Float ||
        paddingField.defaultFloat != 0.35f ||
        paddingField.minimumFloat != 0.0f ||
        paddingField.maximumFloat != 3.0f ||
        paddingField.stepFloat != 0.05f) {
        outFail = "The board-clearance padding field lost its authored range or default.";
        return false;
    }

    if (commands::resolve(commands::kClearBoardFootprintId) !=
            commands::Kind::ClearBoardFootprint ||
        commands::resolve(commands::kResetImportedSceneId) !=
            commands::Kind::ResetImportedScene ||
        commands::resolve("") != commands::Kind::Unknown ||
        commands::resolve("not-a-command") != commands::Kind::Unknown) {
        outFail = "Project command ID resolution should reject unknown IDs.";
        return false;
    }

    commands::BoardClearanceRequest request;
    std::string error = "stale";
    if (!commands::parseBoardClearanceRequest(
            nullptr,
            10u,
            request,
            &error) ||
        !error.empty() || request.paddingCells != 0.35f ||
        !request.clearTerrain || !request.clearVegetation ||
        !request.clearObjects || !request.retainRamps ||
        !request.addGroundInfill) {
        outFail =
            "A board-clearance request without values should retain every authored default.";
        return false;
    }

    const std::array<engine::editor::EditorProjectCommandValue, 7> values{{
        {.id = "padding_cells", .floatValue = 1.25f},
        {.id = "clear_terrain", .booleanValue = false},
        {.id = "clear_vegetation", .booleanValue = false},
        {.id = "clear_objects", .booleanValue = false},
        {.id = "retain_ramps", .booleanValue = false},
        {.id = "add_ground_infill", .booleanValue = true},
        {.id = "future_unknown_field", .booleanValue = true},
    }};
    if (!commands::parseBoardClearanceRequest(
            values.data(),
            values.size(),
            request,
            &error) ||
        request.paddingCells != 1.25f || request.clearTerrain ||
        request.clearVegetation || request.clearObjects ||
        request.retainRamps || !request.addGroundInfill || !error.empty()) {
        outFail =
            "Board-clearance value parsing should apply known values and ignore forward-compatible unknown fields.";
        return false;
    }

    const std::array<engine::editor::EditorProjectCommandValue, 4>
        disabledValues{{
            {.id = "clear_terrain", .booleanValue = false},
            {.id = "clear_vegetation", .booleanValue = false},
            {.id = "clear_objects", .booleanValue = false},
            {.id = "add_ground_infill", .booleanValue = false},
        }};
    if (commands::parseBoardClearanceRequest(
            disabledValues.data(),
            disabledValues.size(),
            request,
            &error) ||
        error != "Enable at least one Route 1 clearing or infill option.") {
        outFail =
            "Board clearance should reject requests with every clearing and infill operation disabled.";
        return false;
    }

    const commands::BoardClearanceResult basicResult{
        .suppressedTerrainCount = 2u,
        .suppressedVegetationCount = 3u,
        .suppressedObjectCount = 4u,
        .groundInfillCreated = true};
    if (commands::boardClearanceStatus(basicResult) !=
        "Board clearing autosaved: 2 terrain, 3 vegetation, 4 object obstructions suppressed; ground infill created.") {
        outFail = "Board-clearance success status formatting changed.";
        return false;
    }
    auto aggregateResult = basicResult;
    aggregateResult.groundInfillCreated = false;
    aggregateResult.skippedUnsafeAggregateCount = 5u;
    const std::string aggregateStatus =
        commands::boardClearanceStatus(aggregateResult);
    if (aggregateStatus.find("suppressed.") == std::string::npos ||
        aggregateStatus.find("5 broad source foliage layers were preserved") ==
            std::string::npos) {
        outFail =
            "Board-clearance status should report preserved unsafe aggregate foliage layers.";
        return false;
    }
    if (commands::unknownCommandError("missing") !=
        "Unknown PokemonAutochess editor command: missing") {
        outFail = "Unknown project command diagnostics changed.";
        return false;
    }

    std::ifstream pluginSource("tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find("PokemonAutochessEditorCommands.h") ==
            std::string::npos ||
        pluginText.find("kBoardClearanceCommandFields") != std::string::npos ||
        pluginText.find("pokemonautochess.route1.clear_board_footprint") !=
            std::string::npos ||
        pluginText.find("editor_commands::resolve") == std::string::npos) {
        outFail =
            "The editor plugin should delegate command metadata, ID dispatch, value parsing, and status formatting to the command component.";
        return false;
    }

    return true;
}
