#include "game/editor/PokemonAutochessEditorPersistence.h"

#include "engine/assets/phlosion/PhlosionSceneArchive.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

} // namespace

bool test_editor_persistence_contract(std::string& outFail) {
    namespace persistence = game::editor::persistence;
    namespace route1 =
        game::runtime::lgpe_route1_runtime;

    const auto uniqueSuffix = std::to_string(
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count());
    TemporaryDirectory temporary{
        std::filesystem::temp_directory_path() /
        ("pac-editor-persistence-" + uniqueSuffix)};
    std::filesystem::create_directories(temporary.path);

    persistence::Store store(temporary.path);
    std::string error;
    if (!store.loadPreviewUnitOverrides(&error) ||
        store.hasPreviewUnitOverride("battle", "board/player/eevee/1")) {
        outFail =
            "An absent preview-layout document should load as an empty schema-one store.";
        return false;
    }
    const persistence::PreviewUnitRecord record{
        .speciesName = "eevee",
        .playerSide = true,
        .benchUnit = false,
        .boardColumn = 2,
        .boardRow = 3,
        .benchSlot = -1,
        .transform = {
            .position = {1.25f, 2.5f, -3.75f},
            .rotationDegrees = {0.0f, 90.0f, 0.0f}}};
    if (!store.savePreviewUnitOverride(
            "battle",
            "board/player/eevee/1",
            record,
            &error)) {
        outFail =
            "The persistence store could not save a typed preview-unit override: " +
            error;
        return false;
    }
    const auto previewPath =
        temporary.path /
        "config/editor/game_preview_layouts.json";
    if (!std::filesystem::is_regular_file(previewPath) ||
        std::filesystem::exists(
            previewPath.string() + ".editor-tmp") ||
        !store.hasPreviewUnitOverride(
            "battle", "board/player/eevee/1")) {
        outFail =
            "Preview-unit persistence should publish the destination and remove its temporary replacement file.";
        return false;
    }
    const auto blockedRoot = temporary.path / "blocked-root";
    {
        std::ofstream blocked(blockedRoot, std::ios::binary);
        blocked << "not a directory\n";
    }
    store.setProjectRoot(blockedRoot);
    if (store.savePreviewUnitOverride(
            "battle",
            "board/player/pikachu/1",
            record,
            &error) ||
        store.hasPreviewUnitOverride(
            "battle", "board/player/pikachu/1") ||
        store.resetPreviewUnitOverride(
            "battle",
            "board/player/eevee/1",
            &error) ||
        !store.hasPreviewUnitOverride(
            "battle", "board/player/eevee/1")) {
        outFail =
            "Failed preview-unit writes and resets should roll the in-memory document back transactionally.";
        return false;
    }
    store.setProjectRoot(temporary.path);

    persistence::Store reloaded(temporary.path);
    if (!reloaded.loadPreviewUnitOverrides(&error)) {
        outFail =
            "The saved preview-unit document did not round-trip: " +
            error;
        return false;
    }
    game::editor::layout_transactions::PreviewUnitTransform transform{
        .position = {9.0f, 9.0f, 9.0f},
        .rotationDegrees = {8.0f, 8.0f, 8.0f}};
    if (!reloaded.applyPreviewUnitOverride(
            "battle",
            "board/player/eevee/1",
            transform) ||
        transform.position != record.transform.position ||
        transform.rotationDegrees !=
            record.transform.rotationDegrees) {
        outFail =
            "Typed preview-unit transforms should survive a save/load round trip.";
        return false;
    }
    if (!reloaded.resetPreviewUnitOverride(
            "battle",
            "board/player/eevee/1",
            &error) ||
        reloaded.hasPreviewUnitOverride(
            "battle", "board/player/eevee/1")) {
        outFail =
            "Resetting a preview-unit override should erase and persist its typed record.";
        return false;
    }

    route1::BoardLayoutTransform boardLayout;
    boardLayout.terrainGridOrigin = {22, -17};
    if (reloaded.saveBoardRegistration(
            false, boardLayout, &error) ||
        error.find("before Route 1 is mounted") ==
            std::string::npos) {
        outFail =
            "Board persistence should reject writes before a scene is mounted.";
        return false;
    }
    if (!reloaded.saveBoardRegistration(
            true, boardLayout, &error)) {
        outFail =
            "The board-registration manifest could not be persisted: " +
            error;
        return false;
    }
    const auto boardPath =
        temporary.path / route1::kBoardLayoutManifestPath;
    if (readFile(boardPath) !=
        route1::serializeBoardLayoutTransform(boardLayout) ||
        std::filesystem::exists(
            boardPath.string() + ".editor-tmp")) {
        outFail =
            "Board persistence should publish exactly the runtime serializer output and clean its temporary file.";
        return false;
    }

    const auto authoredPath =
        temporary.path / "scenes/test.scene.json";
    const engine::assets::phlosion::AuthoredSceneDocument
        emptyDocument;
    if (!reloaded.saveAuthoredScene(
            true,
            authoredPath,
            emptyDocument,
            &error)) {
        outFail =
            "The empty authored-scene checkpoint could not be persisted: " +
            error;
        return false;
    }
    const std::string authoredText = readFile(authoredPath);
    if (authoredText.find(
            "\"kind\": \"phlosion_authored_scene\"") ==
            std::string::npos ||
        authoredText.find("\"nodes\": []") ==
            std::string::npos ||
        std::filesystem::exists(
            authoredPath.string() + ".editor-tmp")) {
        outFail =
            "Empty authored scenes should preserve the promoted source-identical checkpoint format.";
        return false;
    }

    {
        std::ofstream malformed(
            previewPath,
            std::ios::binary | std::ios::trunc);
        malformed << "[]\n";
    }
    if (reloaded.loadPreviewUnitOverrides(&error) ||
        error.find("document root is not an object") ==
            std::string::npos) {
        outFail =
            "Malformed preview-unit documents should fail with a useful load diagnostic.";
        return false;
    }

    std::ifstream pluginSource(
        "tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find(
            "PokemonAutochessEditorPersistence.h") ==
            std::string::npos ||
        pluginText.find("nlohmann::json") !=
            std::string::npos ||
        pluginText.find("std::ofstream") !=
            std::string::npos ||
        pluginText.find("copy_file") !=
            std::string::npos) {
        outFail =
            "The editor plugin should delegate JSON ownership and temporary-file replacement to the persistence store.";
        return false;
    }

    return true;
}
