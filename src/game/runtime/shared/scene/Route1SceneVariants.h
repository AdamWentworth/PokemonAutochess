#pragma once

#include <string_view>

namespace game::runtime::route1_scene_variants {

struct Variant {
    std::string_view sceneId;
    std::string_view boardLayoutManifestPath;
    std::string_view authoredSceneDocumentPath;
};

inline constexpr Variant kRoute1{
    .sceneId = "routes/route1",
    .boardLayoutManifestPath =
        "config/environment/route1_board_layout.json",
    .authoredSceneDocumentPath = "scenes/route1.scene.json"};

inline constexpr Variant kRoute1_5{
    .sceneId = "routes/route1-5",
    .boardLayoutManifestPath =
        "config/environment/route1_5_board_layout.json",
    .authoredSceneDocumentPath = "scenes/route1_5.scene.json"};

inline constexpr const Variant* find(
    std::string_view sceneId) noexcept {
    if (sceneId == kRoute1.sceneId) {
        return &kRoute1;
    }
    if (sceneId == kRoute1_5.sceneId) {
        return &kRoute1_5;
    }
    return nullptr;
}

inline constexpr bool editable(
    std::string_view sceneId) noexcept {
    return find(sceneId) != nullptr;
}

inline constexpr const Variant& fromStateScriptPath(
    std::string_view stateScriptPath) noexcept {
    return stateScriptPath.find("route1_5") !=
            std::string_view::npos
        ? kRoute1_5
        : kRoute1;
}

} // namespace game::runtime::route1_scene_variants
