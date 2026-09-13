#include "game/editor/PokemonAutochessEditorPreviewCatalog.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_set>

bool test_editor_preview_catalog_contract(std::string& outFail) {
    namespace catalog = game::editor::preview_catalog;

    const auto& definitions = catalog::all();
    if (definitions.size() != catalog::kDefinitionCount ||
        definitions.size() != 23u) {
        outFail = "The editor should expose 19 Blender arena setups and four frontend previews.";
        return false;
    }

    std::unordered_set<std::string> ids;
    ids.reserve(definitions.size());
    bool sawClassic = false;
    bool sawAdventure = false;
    bool sawSnapshot = false;
    bool sawRoutePlanning = false;
    bool sawRouteBattle = false;
    std::ifstream projectFile("phlosion.project.json");
    const auto project = nlohmann::json::parse(projectFile);
    std::unordered_set<std::string> sceneIds;
    for (const auto& scene : project.at("scenes")) {
        const auto id = scene.at("scene_id").get<std::string>();
        const auto* variant = game::runtime::route1_scene_variants::find(id);
        if (!variant || variant->arenaBundlePath.empty() || variant->usesSourceTerrain) {
            outFail = "The active editor scene catalog must contain only Blender-authored arenas: " + id;
            return false;
        }
        sceneIds.insert(id);
    }
    if (sceneIds.size() != 5u || !sceneIds.contains(project.at("startup_scene").at("scene_id").get<std::string>())) {
        outFail = "The editor must retain all five Blender locations and a valid startup scene.";
        return false;
    }
    std::unordered_set<std::string> scenesWithSetups;

    for (const auto& definition : definitions) {
        const std::string_view id = definition.id ? definition.id : "";
        const std::string_view displayName =
            definition.displayName ? definition.displayName : "";
        const std::string_view group = definition.group ? definition.group : "";
        const std::string_view description =
            definition.description ? definition.description : "";
        const std::string_view state = definition.state ? definition.state : "";
        const std::string_view gameMode =
            definition.gameMode ? definition.gameMode : "";
        const std::string_view source =
            definition.snapshot ? definition.snapshot : "";
        const std::string_view sceneId =
            definition.sceneId ? definition.sceneId : "";

        if (id.empty() || displayName.empty() || group.empty() ||
            description.empty() || state.empty() || gameMode.empty()) {
            outFail = "Every project editor preview needs complete display and activation metadata.";
            return false;
        }
        if (!ids.emplace(id).second) {
            outFail = "Project editor preview IDs must be unique: " + std::string(id);
            return false;
        }
        if (catalog::find(id) != &definition) {
            outFail = "Project editor preview lookup should return the catalog-owned record: " +
                      std::string(id);
            return false;
        }

        sawClassic = sawClassic || gameMode == "classic";
        sawAdventure = sawAdventure || gameMode == "adventure";
        sawSnapshot = sawSnapshot || state == "snapshot";
        sawRoutePlanning = sawRoutePlanning || state == "route_planning";
        sawRouteBattle = sawRouteBattle || state == "route_battle";

        const bool externalState =
            state == "snapshot" || state == "route_planning" ||
            state == "route_battle";
        if (externalState && (source.empty() || sceneId.empty())) {
            outFail = "Route and snapshot previews require both a source document and scene identity: " +
                      std::string(id);
            return false;
        }
        if (!sceneId.empty()) {
            if (!sceneIds.contains(std::string(sceneId))) {
                outFail = "A scenario points to a retired editor scene: " + std::string(id);
                return false;
            }
            if (scenesWithSetups.insert(std::string(sceneId)).second && state != "route_planning") {
                outFail = "Opening a location should default to Planning, before mechanic tests.";
                return false;
            }
            const bool normalSetup = state == "route_planning" || state == "route_battle" || id.ends_with("-crowded");
            if (group != (normalSetup ? "Starting setups" : "Tests")) {
                outFail = "Normal setups and mechanic tests must remain distinct.";
                return false;
            }
        }
        if (!source.empty() && !std::filesystem::exists(source)) {
            outFail = "Project editor preview source does not exist: " +
                      std::string(source);
            return false;
        }
    }

    if (scenesWithSetups != sceneIds || !sawClassic || !sawAdventure || !sawSnapshot ||
        !sawRoutePlanning || !sawRouteBattle) {
        outFail = "The project editor preview catalog lost required mode or activation coverage.";
        return false;
    }
    if (!catalog::find("boot") || !catalog::find("main-menu") ||
        catalog::find("not-a-preview") || catalog::find("route1-planning-classic")) {
        outFail = "Project editor preview lookup should preserve stable frontend IDs and reject unknown IDs.";
        return false;
    }

    std::ifstream pluginSource("tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find("struct PreviewDefinition") != std::string::npos ||
        pluginText.find("kPreviewDefinitions") != std::string::npos ||
        pluginText.find("PokemonAutochessEditorPreviewCatalog.h") ==
            std::string::npos) {
        outFail = "The editor plugin should consume the dedicated preview catalog instead of re-owning its records.";
        return false;
    }

    return true;
}
