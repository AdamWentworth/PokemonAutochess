#pragma once

#include <string_view>

namespace game::runtime::route1_scene_variants {

struct Variant {
    std::string_view sceneId;
    std::string_view boardLayoutManifestPath;
    std::string_view authoredSceneDocumentPath;
    std::string_view arenaBundlePath;
    bool usesSourceTerrain = true;
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

inline constexpr Variant kRoute1Pilot{
    .sceneId = "routes/route1-pilot",
    .boardLayoutManifestPath = "config/environment/route1_pilot_board_layout.json",
    .authoredSceneDocumentPath = "scenes/route1_pilot.scene.json",
    .arenaBundlePath = "content/phlosion/environment/arena-pilot/arena.phscene",
    .usesSourceTerrain = false};

inline constexpr Variant kRoute1SouthClearing{
    .sceneId = "routes/route1-south-clearing",
    .boardLayoutManifestPath = "config/environment/route1_south_clearing_board_layout.json",
    .authoredSceneDocumentPath = "scenes/route1_south_clearing.scene.json",
    .arenaBundlePath = "content/phlosion/environment/south-clearing/arena.phscene",
    .usesSourceTerrain = false};

inline constexpr Variant kRoute1NorthTerraces{
    .sceneId = "routes/route1-north-terraces",
    .boardLayoutManifestPath = "config/environment/route1_north_terraces_board_layout.json",
    .authoredSceneDocumentPath = "scenes/route1_north_terraces.scene.json",
    .arenaBundlePath = "content/phlosion/environment/north-terraces/arena.phscene",
    .usesSourceTerrain = false};

inline constexpr const Variant *find(
    std::string_view sceneId) noexcept {
    if (sceneId == kRoute1.sceneId) {
        return &kRoute1;
    }
    if (sceneId == kRoute1_5.sceneId) {
        return &kRoute1_5;
    }
    if (sceneId == kRoute1Pilot.sceneId) {
        return &kRoute1Pilot;
    }
    if (sceneId == kRoute1SouthClearing.sceneId) {
        return &kRoute1SouthClearing;
    }
    if (sceneId == kRoute1NorthTerraces.sceneId) {
        return &kRoute1NorthTerraces;
    }
    return nullptr;
}

inline constexpr bool editable(
    std::string_view sceneId) noexcept {
    return find(sceneId) != nullptr;
}

inline constexpr const Variant& fromStateScriptPath(
    std::string_view stateScriptPath) noexcept {
    if (stateScriptPath.find("route1_north_terraces") != std::string_view::npos) {
        return kRoute1NorthTerraces;
    }
    if (stateScriptPath.find("route1_south_clearing") != std::string_view::npos) {
        return kRoute1SouthClearing;
    }
    if (stateScriptPath.find("route1_pilot") != std::string_view::npos) {
        return kRoute1Pilot;
    }
    return stateScriptPath.find("route1_5") !=
            std::string_view::npos
        ? kRoute1_5
        : kRoute1;
}

} // namespace game::runtime::route1_scene_variants
