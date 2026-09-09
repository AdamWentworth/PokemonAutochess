#include "game/runtime/shared/scene/BoardLayoutDocument.h"
#include "engine/core/IAssetStore.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace game::runtime::route1_environment {
using board_layout_metrics::kTerrainElevationStepCm;
using board_layout_metrics::kTerrainTileSizeCm;
namespace {
bool fail(std::string *outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

template <std::size_t N>
std::array<float, N> jsonFloatArray(
    const nlohmann::json &value,
    const char *label) {
    if (!value.is_array() || value.size() != N) {
        throw std::runtime_error(
            std::string(label) + " must contain " + std::to_string(N) +
            " numeric values.");
    }
    std::array<float, N> out{};
    for (std::size_t index = 0u; index < N; ++index) {
        if (!value[index].is_number()) {
            throw std::runtime_error(
                std::string(label) + " must contain only numeric values.");
        }
        out[index] = value[index].get<float>();
    }
    return out;
}

bool loadJson(
    const engine::IAssetStore &store,
    const std::string &virtualPath,
    nlohmann::json &out,
    std::string *outError) {
    std::string text;
    if (!store.readText(virtualPath, text, outError)) {
        return false;
    }
    try {
        out = nlohmann::json::parse(text);
        return true;
    } catch (const std::exception &ex) {
        return fail(
            outError,
            "Could not parse " + virtualPath + ": " + ex.what());
    }
}

} // namespace
bool isRoute1EnvironmentProfile(std::string_view profileId) {
    return profileId == "route1_environment_road001_00" ||
           profileId == "lgpe_route1_road001_00";
}

bool route1EnvironmentProfilesCompatible(
    std::string_view left,
    std::string_view right) {
    return left == right ||
           (isRoute1EnvironmentProfile(left) &&
            isRoute1EnvironmentProfile(right));
}

bool boardRegistrationMatchesTerrainGrid(
    const BoardLayoutTransform &layout) {
    const float expectedCellSizeWorld =
        kTerrainTileSizeCm * layout.sourceUnitsToWorld;
    const std::array<float, 3> expectedAnchor{
        (static_cast<float>(layout.terrainGridOrigin[0]) +
         static_cast<float>(layout.boardCells[0]) * 0.5f) *
            kTerrainTileSizeCm,
        static_cast<float>(layout.terrainElevationLevel) *
            kTerrainElevationStepCm,
        (static_cast<float>(layout.terrainGridOrigin[1]) +
         static_cast<float>(layout.boardCells[1]) * 0.5f) *
            kTerrainTileSizeCm};
    return std::abs(
               layout.boardCellSizeWorld -
               expectedCellSizeWorld) <= 0.0001f &&
           std::abs(layout.sourceAnchorCm[0] - expectedAnchor[0]) <=
               0.001f &&
           std::abs(layout.sourceAnchorCm[1] - expectedAnchor[1]) <=
               0.001f &&
           std::abs(layout.sourceAnchorCm[2] - expectedAnchor[2]) <=
               0.001f &&
           std::abs(layout.worldAnchor[0]) <= 0.0001f &&
           std::abs(layout.worldAnchor[2]) <= 0.0001f &&
           std::abs(layout.yawDegrees) <= 0.0001f &&
           layout.benchGapCells <= 64u &&
           ((layout.boardCells[0] + layout.benchSlots) % 2u) == 0u;
}

bool loadBoardLayoutTransform(
    const engine::IAssetStore &store,
    const std::string &virtualPath,
    BoardLayoutTransform &out,
    std::string *outError) {
    nlohmann::json root;
    if (!loadJson(store, virtualPath, root, outError)) {
        return false;
    }
    try {
        const int schemaVersion =
            root.at("schema_version").get<int>();
        const std::string kind = root.at("kind").get<std::string>();
        if ((schemaVersion != 1 &&
             schemaVersion != 2 &&
             schemaVersion != 3 &&
             schemaVersion != 4 &&
             schemaVersion != 5 &&
             schemaVersion != 6) ||
            (kind != "route1_environment_board_layout" &&
             kind != "lgpe_route1_board_layout_delta")) {
            return fail(
                outError,
                "Unsupported Route 1 board-layout manifest contract.");
        }
        BoardLayoutTransform decoded;
        decoded.coordinateSystem =
            root.at("coordinate_system").get<std::string>();
        decoded.sourceProfileId =
            root.at("source_profile_id").get<std::string>();
        const auto &transform = root.at("source_to_world");
        if (schemaVersion >= 6 &&
            transform.contains("source_anchor_cm")) {
            throw std::runtime_error(
                "schema 6 forbids source_anchor_cm; the board anchor is derived from terrain_grid_origin.");
        }
        decoded.sourceUnitsToWorld =
            transform.at("source_units_to_world").get<float>();
        if (schemaVersion <= 5) {
            decoded.sourceAnchorCm = jsonFloatArray<3>(
                transform.at("source_anchor_cm"),
                "source_anchor_cm");
        }
        decoded.worldAnchor = jsonFloatArray<3>(
            transform.at("world_anchor"),
            "world_anchor");
        decoded.yawDegrees =
            transform.at("yaw_degrees").get<float>();
        if (schemaVersion >= 6 &&
            !root.contains("board_registration")) {
            throw std::runtime_error(
                "schema 6 requires an explicit terrain-bound board_registration.");
        }
        const auto registration =
            root.find("board_registration");
        if (registration != root.end()) {
            const auto cells = registration->at("board_cells");
            if (!cells.is_array() ||
                cells.size() != 2u) {
                throw std::runtime_error(
                    "board_cells must contain two positive integers.");
            }
            decoded.boardCells = {
                cells.at(0).get<std::uint32_t>(),
                cells.at(1).get<std::uint32_t>()};
            if (schemaVersion >= 6) {
                if (registration->contains("cell_size_world")) {
                    throw std::runtime_error(
                        "schema 6 forbids cell_size_world; the board cell size is the Route 1 terrain tile size.");
                }
                const auto &origin =
                    registration->at("terrain_grid_origin");
                if (!origin.is_array() || origin.size() != 2u) {
                    throw std::runtime_error(
                        "terrain_grid_origin must contain two integer cell coordinates.");
                }
                decoded.terrainGridOrigin = {
                    origin.at(0).get<std::int32_t>(),
                    origin.at(1).get<std::int32_t>()};
                decoded.terrainElevationLevel =
                    registration->at(
                                    "terrain_elevation_level")
                        .get<std::int32_t>();
                const float declaredTileSize =
                    registration->at(
                                    "terrain_tile_size_cm")
                        .get<float>();
                if (!std::isfinite(declaredTileSize) ||
                    std::abs(
                        declaredTileSize -
                        kTerrainTileSizeCm) > 0.0001f) {
                    throw std::runtime_error(
                        "terrain_tile_size_cm must match the recovered Route 1 tile module.");
                }
                decoded.boardCellSizeWorld =
                    kTerrainTileSizeCm *
                    decoded.sourceUnitsToWorld;
                decoded.sourceAnchorCm = {
                    (static_cast<float>(
                         decoded.terrainGridOrigin[0]) +
                     static_cast<float>(
                         decoded.boardCells[0]) *
                         0.5f) *
                        kTerrainTileSizeCm,
                    static_cast<float>(
                        decoded.terrainElevationLevel) *
                        kTerrainElevationStepCm,
                    (static_cast<float>(
                         decoded.terrainGridOrigin[1]) +
                     static_cast<float>(
                         decoded.boardCells[1]) *
                         0.5f) *
                        kTerrainTileSizeCm};
            } else {
                if (const auto cellSize =
                        registration->find("cell_size_world");
                    cellSize != registration->end()) {
                    decoded.boardCellSizeWorld =
                        cellSize->get<float>();
                }
                decoded.terrainGridOrigin = {
                    static_cast<std::int32_t>(std::llround(
                        decoded.sourceAnchorCm[0] /
                            kTerrainTileSizeCm -
                        static_cast<float>(
                            decoded.boardCells[0]) *
                            0.5f)),
                    static_cast<std::int32_t>(std::llround(
                        decoded.sourceAnchorCm[2] /
                            kTerrainTileSizeCm -
                        static_cast<float>(
                            decoded.boardCells[1]) *
                            0.5f))};
                decoded.terrainElevationLevel =
                    static_cast<std::int32_t>(std::llround(
                        decoded.sourceAnchorCm[1] /
                        kTerrainElevationStepCm));
            }
            if (const auto benchSlots =
                    registration->find("bench_slots");
                benchSlots != registration->end()) {
                decoded.benchSlots =
                    benchSlots->get<std::uint32_t>();
            }
            if (const auto benchGap =
                    registration->find("bench_gap_cells");
                benchGap != registration->end()) {
                decoded.benchGapCells =
                    benchGap->get<std::uint32_t>();
            }
            if (const auto benches =
                    registration->find("bench_sides");
                benches != registration->end()) {
                if (!benches->is_array()) {
                    throw std::runtime_error(
                        "bench_sides must be an array.");
                }
                decoded.northBench = false;
                decoded.southBench = false;
                for (const auto &side : *benches) {
                    const std::string value =
                        side.get<std::string>();
                    if (value == "north") {
                        decoded.northBench = true;
                    } else if (value == "south") {
                        decoded.southBench = true;
                    } else {
                        throw std::runtime_error(
                            "bench_sides entries must be north or south.");
                    }
                }
            }
        }
        if (schemaVersion <= 5 && registration == root.end()) {
            decoded.terrainGridOrigin = {
                static_cast<std::int32_t>(std::llround(
                    decoded.sourceAnchorCm[0] /
                        kTerrainTileSizeCm -
                    static_cast<float>(decoded.boardCells[0]) *
                        0.5f)),
                static_cast<std::int32_t>(std::llround(
                    decoded.sourceAnchorCm[2] /
                        kTerrainTileSizeCm -
                    static_cast<float>(decoded.boardCells[1]) *
                        0.5f))};
            decoded.terrainElevationLevel =
                static_cast<std::int32_t>(std::llround(
                    decoded.sourceAnchorCm[1] /
                    kTerrainElevationStepCm));
        }
        const auto finiteArray =
            [](const std::array<float, 3> &values) {
                return std::all_of(
                    values.begin(),
                    values.end(),
                    [](float value) {
                        return std::isfinite(value);
                    });
            };
        std::set<std::string> deltaIds;
        std::set<std::string> deltaTargets;
        const auto deltaRecords =
            root.find("local_layout_deltas");
        if (deltaRecords != root.end()) {
            for (const auto &record : *deltaRecords) {
                LocalLayoutDelta delta;
                delta.id = record.at("id").get<std::string>();
                const auto &target = record.at("target");
                delta.targetKind =
                    target.at("kind").get<std::string>();
                delta.logicalName =
                    target.at("logical_name").get<std::string>();
                delta.recordIndex =
                    target.at("record_index")
                        .get<std::uint32_t>();
                const auto &expected =
                    record.at("expected_source_transform");
                delta.expectedSourceTranslationCm =
                    jsonFloatArray<3>(
                        expected.at("translation_cm"),
                        "expected translation_cm");
                delta.expectedSourceRotationDegrees =
                    jsonFloatArray<3>(
                        expected.at("rotation_degrees"),
                        "expected rotation_degrees");
                delta.expectedSourceScale =
                    jsonFloatArray<3>(
                        expected.at("scale"),
                        "expected scale");
                const auto &authored =
                    record.at("authored_transform");
                delta.translationCm =
                    jsonFloatArray<3>(
                        authored.at("translation_cm"),
                        "authored translation_cm");
                delta.rotationDegrees =
                    jsonFloatArray<3>(
                        authored.at("rotation_degrees"),
                        "authored rotation_degrees");
                delta.scale =
                    jsonFloatArray<3>(
                        authored.at("scale"),
                        "authored scale");
                delta.suppressed =
                    record.value("suppressed", false);
                delta.reason =
                    record.value("reason", std::string{});
                const std::string stableTarget =
                    delta.targetKind + "/" +
                    delta.logicalName + "/" +
                    std::to_string(
                        delta.recordIndex);
                const bool supportedTargetKind =
                    delta.targetKind ==
                        "buildmodel_vegetation_placement" ||
                    delta.targetKind ==
                        "encounter_grass_record" ||
                    delta.targetKind ==
                        "canonical_tree_instance" ||
                    delta.targetKind ==
                        "canonical_terrain_assembly" ||
                    delta.targetKind ==
                        "canonical_mesh_group";
                if (delta.id.empty() ||
                    !supportedTargetKind ||
                    delta.logicalName.empty() ||
                    !finiteArray(
                        delta.expectedSourceTranslationCm) ||
                    !finiteArray(
                        delta.expectedSourceRotationDegrees) ||
                    !finiteArray(delta.expectedSourceScale) ||
                    !finiteArray(delta.translationCm) ||
                    !finiteArray(delta.rotationDegrees) ||
                    !finiteArray(delta.scale) ||
                    std::any_of(
                        delta.expectedSourceScale.begin(),
                        delta.expectedSourceScale.end(),
                        [](float value) {
                            return value <= 0.0f;
                        }) ||
                    std::any_of(
                        delta.scale.begin(),
                        delta.scale.end(),
                        [](float value) {
                            return value <= 0.0f;
                        }) ||
                    !deltaIds.insert(delta.id).second ||
                    !deltaTargets.insert(stableTarget).second) {
                    return fail(
                        outError,
                        "Route 1 board-layout manifest contains an "
                        "invalid or duplicate local delta.");
                }
                decoded.localLayoutDeltas.push_back(
                    std::move(delta));
            }
        }
        if (const auto metadataRecords =
                root.find(
                    "hierarchy_metadata_overrides");
            metadataRecords != root.end()) {
            std::set<std::string> metadataTargets;
            for (const auto &record :
                 *metadataRecords) {
                LayoutObjectMetadataOverride metadata{
                    .stableId =
                        record.at("stable_id")
                            .get<std::string>(),
                    .displayName =
                        record.at("display_name")
                            .get<std::string>(),
                    .categoryPath =
                        record.at("category_path")
                            .get<std::string>()};
                if (metadata.stableId.empty() ||
                    metadata.displayName.empty() ||
                    metadata.categoryPath.empty() ||
                    !metadataTargets.insert(
                                        metadata.stableId)
                         .second) {
                    return fail(
                        outError,
                        "Route 1 board-layout manifest contains invalid hierarchy metadata.");
                }
                decoded.objectMetadataOverrides.push_back(
                    std::move(metadata));
            }
        }
        if (const auto authoredRecords =
                root.find("authored_prefab_instances");
            authoredRecords != root.end()) {
            std::set<std::string> authoredIds;
            for (const auto &record :
                 *authoredRecords) {
                const auto &source =
                    record.at("creation_transform");
                const auto &authoredTransform =
                    record.at("authored_transform");
                AuthoredPrefabInstance instance{
                    .stableId =
                        record.at("stable_id")
                            .get<std::string>(),
                    .prototypeStableId =
                        record.at("prototype_stable_id")
                            .get<std::string>(),
                    .prefabAssetId = record.value("prefab_asset_id", std::string{}),
                    .displayName =
                        record.at("display_name")
                            .get<std::string>(),
                    .categoryPath =
                        record.at("category_path")
                            .get<std::string>(),
                    .sourceTranslationCm =
                        jsonFloatArray<3>(
                            source.at("translation_cm"),
                            "authored prefab creation translation_cm"),
                    .sourceRotationDegrees =
                        jsonFloatArray<3>(
                            source.at("rotation_degrees"),
                            "authored prefab creation rotation_degrees"),
                    .sourceScale =
                        jsonFloatArray<3>(
                            source.at("scale"),
                            "authored prefab creation scale"),
                    .translationCm =
                        jsonFloatArray<3>(
                            authoredTransform.at("translation_cm"),
                            "authored prefab translation_cm"),
                    .rotationDegrees =
                        jsonFloatArray<3>(
                            authoredTransform.at("rotation_degrees"),
                            "authored prefab rotation_degrees"),
                    .scale =
                        jsonFloatArray<3>(
                            authoredTransform.at("scale"),
                            "authored prefab scale"),
                    .suppressed =
                        record.value("suppressed", false),
                    .reason =
                        record.value("reason", std::string{})};
                if (instance.stableId.empty() ||
                    instance.prototypeStableId.empty() ||
                    instance.displayName.empty() ||
                    instance.categoryPath.empty() ||
                    !finiteArray(instance.sourceTranslationCm) ||
                    !finiteArray(instance.sourceRotationDegrees) ||
                    !finiteArray(instance.sourceScale) ||
                    !finiteArray(instance.translationCm) ||
                    !finiteArray(instance.rotationDegrees) ||
                    !finiteArray(instance.scale) ||
                    std::any_of(
                        instance.sourceScale.begin(),
                        instance.sourceScale.end(),
                        [](float value) {
                            return value <= 0.0f;
                        }) ||
                    std::any_of(
                        instance.scale.begin(),
                        instance.scale.end(),
                        [](float value) {
                            return value <= 0.0f;
                        }) ||
                    !authoredIds.insert(
                                    instance.stableId)
                         .second) {
                    return fail(
                        outError,
                        "Route 1 board-layout manifest contains an invalid authored prefab instance.");
                }
                decoded.authoredPrefabInstances.push_back(
                    std::move(instance));
            }
        }
        decoded.declaredLocalDeltaCount =
            static_cast<std::uint32_t>(
                decoded.localLayoutDeltas.size());
        const bool routeProfile =
            isRoute1EnvironmentProfile(decoded.sourceProfileId);
        if (decoded.coordinateSystem !=
                "source_centimetres_xyz_y_up" ||
            !routeProfile ||
            !std::isfinite(decoded.sourceUnitsToWorld) ||
            decoded.sourceUnitsToWorld <= 0.0f ||
            !std::isfinite(decoded.yawDegrees) ||
            decoded.boardCells[0] == 0u ||
            decoded.boardCells[1] == 0u ||
            !std::isfinite(decoded.boardCellSizeWorld) ||
            decoded.boardCellSizeWorld < 0.25f ||
            decoded.boardCellSizeWorld > 4.0f ||
            decoded.benchSlots == 0u ||
            (!decoded.northBench && !decoded.southBench) ||
            !boardRegistrationMatchesTerrainGrid(decoded)) {
            return fail(
                outError,
                "Route 1 board-layout manifest has invalid source metadata "
                "or a board registration that is not bound to the terrain grid.");
        }
        out = std::move(decoded);
        return true;
    } catch (const std::exception &ex) {
        return fail(
            outError,
            "Invalid Route 1 board-layout manifest: " +
                std::string(ex.what()));
    }
}

void bindBoardLayoutToTerrainGrid(
    BoardLayoutTransform &layout) noexcept {
    layout.boardCellSizeWorld =
        kTerrainTileSizeCm * layout.sourceUnitsToWorld;
    layout.sourceAnchorCm = {
        (static_cast<float>(layout.terrainGridOrigin[0]) +
         static_cast<float>(layout.boardCells[0]) * 0.5f) *
            kTerrainTileSizeCm,
        static_cast<float>(layout.terrainElevationLevel) *
            kTerrainElevationStepCm,
        (static_cast<float>(layout.terrainGridOrigin[1]) +
         static_cast<float>(layout.boardCells[1]) * 0.5f) *
            kTerrainTileSizeCm};
    layout.worldAnchor[0] = 0.0f;
    layout.worldAnchor[2] = 0.0f;
    layout.yawDegrees = 0.0f;
}

std::array<std::int32_t, 2> northBenchTerrainGridOrigin(
    const BoardLayoutTransform &layout) noexcept {
    const std::int64_t centeredOffset =
        (static_cast<std::int64_t>(layout.boardCells[0]) -
         static_cast<std::int64_t>(layout.benchSlots)) /
        2;
    return {
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(
                layout.terrainGridOrigin[0]) +
            centeredOffset),
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(
                layout.terrainGridOrigin[1]) +
            static_cast<std::int64_t>(layout.boardCells[1]) +
            static_cast<std::int64_t>(layout.benchGapCells))};
}

std::array<std::int32_t, 2> southBenchTerrainGridOrigin(
    const BoardLayoutTransform &layout) noexcept {
    const std::int64_t centeredOffset =
        (static_cast<std::int64_t>(layout.boardCells[0]) -
         static_cast<std::int64_t>(layout.benchSlots)) /
        2;
    return {
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(
                layout.terrainGridOrigin[0]) +
            centeredOffset),
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(
                layout.terrainGridOrigin[1]) -
            static_cast<std::int64_t>(layout.benchGapCells) -
            1)};
}

std::string serializeBoardLayoutTransform(
    const BoardLayoutTransform &transform) {
    const auto cleanNumber = [](float value) {
        constexpr double precision = 1'000'000.0;
        return std::round(static_cast<double>(value) * precision) /
               precision;
    };
    const auto cleanVec3 = [&](const std::array<float, 3> &value) {
        return std::array<double, 3>{
            cleanNumber(value[0]),
            cleanNumber(value[1]),
            cleanNumber(value[2])};
    };
    nlohmann::json root{
        {"schema_version", 6},
        {"kind", "route1_environment_board_layout"},
        {"coordinate_system", transform.coordinateSystem},
        {"source_profile_id", transform.sourceProfileId},
        {"source_to_world",
         {
             {"source_units_to_world",
              cleanNumber(transform.sourceUnitsToWorld)},
             {"world_anchor",
              cleanVec3(transform.worldAnchor)},
             {"yaw_degrees",
              cleanNumber(transform.yawDegrees)},
         }},
        {"board_registration",
         {
             {"board_cells", transform.boardCells},
             {"terrain_grid_origin",
              transform.terrainGridOrigin},
             {"terrain_elevation_level",
              transform.terrainElevationLevel},
             {"terrain_tile_size_cm",
              cleanNumber(kTerrainTileSizeCm)},
             {"bench_slots", transform.benchSlots},
             {"bench_gap_cells", transform.benchGapCells},
             {"bench_sides", nlohmann::json::array()},
             {"intent",
              "register the source-centimetre qualification scene "
              "under the gameplay board and both bench rows"},
             {"status", "gameplay_footprint_terrain_grid_bound"},
         }},
        {"fidelity_contract",
         {
             {"source_scale_preserved", true},
             {"source_elevations_preserved", true},
             {"undeclared_source_transforms_changed", false},
             {"procedural_route_environment_contribution", false},
         }},
    };
    auto &benchSides =
        root["board_registration"]["bench_sides"];
    if (transform.northBench) {
        benchSides.push_back("north");
    }
    if (transform.southBench) {
        benchSides.push_back("south");
    }
    return root.dump(2) + '\n';
}

} // namespace game::runtime::route1_environment
