#include "game/editor/PokemonAutochessEditorPreviewCatalog.h"

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
        definitions.size() != 28u) {
        outFail = "The project editor preview catalog should expose all 28 stable previews.";
        return false;
    }

    std::unordered_set<std::string> ids;
    ids.reserve(definitions.size());
    bool sawClassic = false;
    bool sawAdventure = false;
    bool sawSnapshot = false;
    bool sawRoutePlanning = false;
    bool sawRouteBattle = false;

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
        if (!source.empty() && !std::filesystem::exists(source)) {
            outFail = "Project editor preview source does not exist: " +
                      std::string(source);
            return false;
        }
    }

    if (!sawClassic || !sawAdventure || !sawSnapshot ||
        !sawRoutePlanning || !sawRouteBattle) {
        outFail = "The project editor preview catalog lost required mode or activation coverage.";
        return false;
    }
    if (!catalog::find("boot") || !catalog::find("main-menu") ||
        catalog::find("not-a-preview")) {
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
