#pragma once

#include "engine/editor/EditorProjectPlugin.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace game::editor::asset_catalog {

struct EnvironmentPrefabDefinition {
    std::string id;
    std::string displayName;
    std::string typeName;
    std::string category;
    std::string path;
    std::string description;
    std::string layoutStableId;
    bool previewable = false;
    bool sceneInstantiable = true;
};

class EnvironmentPrefabCatalog {
public:
    void clear() noexcept;
    void rebuild(
        const std::filesystem::path& projectRoot,
        std::string_view sceneId,
        const std::vector<
            game::runtime::lgpe_route1_runtime::LayoutObject>& objects);

    std::size_t size() const noexcept;
    engine::editor::EditorProjectAsset asset(
        std::size_t index) const noexcept;
    const EnvironmentPrefabDefinition* find(
        std::string_view assetId) const noexcept;

private:
    std::vector<EnvironmentPrefabDefinition> definitions_;
};

} // namespace game::editor::asset_catalog
