#include "game/editor/PokemonAutochessEditorSceneMutations.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace game::editor::scene_mutations {
namespace {

namespace route1 =
    game::runtime::lgpe_route1_runtime;

bool validSurfaceId(std::string_view surface) noexcept {
    return surface == "light_lawn" ||
        surface == "dark_lawn" ||
        surface == "dirt_path" ||
        surface == "empty";
}

bool validShapeId(std::string_view shape) noexcept {
    return shape == "flat" ||
        shape == "ramp_north" ||
        shape == "ramp_east" ||
        shape == "ramp_south" ||
        shape == "ramp_west";
}

bool validVariantForSurface(
    std::string_view surface,
    std::string_view variant) noexcept {
    if (variant == "auto") {
        return true;
    }
    if (surface != "dirt_path" ||
        !variant.starts_with("path_")) {
        return false;
    }
    const std::string_view digits = variant.substr(5u);
    std::uint32_t mask = 0u;
    const auto result = std::from_chars(
        digits.data(),
        digits.data() + digits.size(),
        mask);
    return result.ec == std::errc{} &&
        result.ptr == digits.data() + digits.size() &&
        mask <= 15u;
}

const route1::TerrainTileState* findTerrainTile(
    const std::vector<route1::TerrainTileState>& terrainTiles,
    std::int32_t gridX,
    std::int32_t gridZ) {
    const auto found = std::find_if(
        terrainTiles.begin(),
        terrainTiles.end(),
        [&](const auto& tile) {
            return tile.gridX == gridX && tile.gridZ == gridZ;
        });
    return found == terrainTiles.end() ? nullptr : &*found;
}

route1::AuthoredTerrainTile& ensureAuthoredTile(
    route1::BoardLayoutTransform& layout,
    const route1::TerrainTileState& source,
    std::int32_t gridX,
    std::int32_t gridZ,
    std::string_view terrainTileSetAssetId,
    std::string_view reason) {
    const auto found = std::find_if(
        layout.authoredTerrainTiles.begin(),
        layout.authoredTerrainTiles.end(),
        [&](const auto& tile) {
            return tile.gridX == gridX && tile.gridZ == gridZ;
        });
    if (found != layout.authoredTerrainTiles.end()) {
        return *found;
    }
    const std::string stableId =
        route1::route1TerrainTileStableId(gridX, gridZ);
    layout.authoredTerrainTiles.push_back(
        route1::AuthoredTerrainTile{
            .stableId = stableId,
            .displayName =
                "Terrain Tile (" + std::to_string(gridX) + ", " +
                std::to_string(gridZ) + ")",
            .categoryPath = "Environment/Terrain/Tiles",
            .tileSetAssetId = std::string(terrainTileSetAssetId),
            .gridX = gridX,
            .gridZ = gridZ,
            .elevationLevel = source.elevationLevel,
            .surface = source.surface,
            .shape = source.shape,
            .visualVariant = "auto",
            .reason = std::string(reason)});
    return layout.authoredTerrainTiles.back();
}

bool validateRequest(
    const engine::editor::EditorProjectTerrainTileEditRequest& request,
    const std::vector<route1::TerrainTileState>& terrainTiles,
    std::string_view operation,
    std::string_view requestedSurface,
    std::string_view requestedShape,
    std::string_view requestedVisualVariant,
    bool pasteTiles,
    std::string* outError) {
    const bool validOperation =
        operation == "create" || operation == "raise" ||
        operation == "lower" || operation == "terrace_raise" ||
        operation == "terrace_lower" ||
        operation == "flatten_tidy" ||
        operation == "tidy_surface" ||
        operation == "platform_set" ||
        operation == "swap_prefab" ||
        operation == "paste_tiles_relative" ||
        operation == "paste_tiles_exact" ||
        operation == "paint_surface" ||
        operation == "set_shape" ||
        operation == "restore_source";
    const bool validSurface = validSurfaceId(requestedSurface);
    const bool validShape = validShapeId(requestedShape);
    const bool validPlatformProfile = validShape ||
        requestedShape == "preserve" ||
        requestedShape == "source";
    const bool validRelativeElevationDelta =
        request.relativeElevationDelta >= -128 &&
        request.relativeElevationDelta <= 128;
    if (!validOperation ||
        !validRelativeElevationDelta ||
        ((operation == "flatten_tidy" ||
          operation == "platform_set") &&
         (request.targetElevationLevel < -128 ||
          request.targetElevationLevel > 128)) ||
        ((operation == "paint_surface" ||
          operation == "swap_prefab" ||
          operation == "platform_set") &&
         !validSurface) ||
        (operation == "platform_set" &&
         requestedSurface == "empty") ||
        (operation == "platform_set" &&
         !validPlatformProfile) ||
        ((operation == "set_shape" ||
          operation == "swap_prefab") &&
         !validShape) ||
        (operation == "swap_prefab" &&
         !validVariantForSurface(
             requestedSurface,
             requestedVisualVariant)) ||
        (operation == "swap_prefab" &&
         requestedVisualVariant != "auto" &&
         requestedShape != "flat") ||
        (request.relativeElevationDelta != 0 &&
         (operation != "swap_prefab" ||
          requestedShape != "flat" ||
          requestedSurface == "empty")) ||
        (requestedSurface == "empty" &&
         (requestedShape != "flat" ||
          requestedVisualVariant != "auto"))) {
        if (outError) {
            *outError = "The requested terrain-tile operation is invalid.";
        }
        return false;
    }
    if (!pasteTiles) {
        return true;
    }
    if (request.coordinateCount != 1u ||
        !request.stampTiles ||
        request.stampTileCount == 0u ||
        request.stampTileCount > 4096u) {
        if (outError) {
            *outError =
                "Tile paste requires one destination anchor and a non-empty bounded clipboard.";
        }
        return false;
    }
    std::set<std::pair<std::int32_t, std::int32_t>> stampOffsets;
    for (std::size_t index = 0u;
         index < request.stampTileCount;
         ++index) {
        const auto& stamp = request.stampTiles[index];
        const std::string_view surface =
            stamp.surface ? stamp.surface : "";
        const std::string_view shape =
            stamp.shape ? stamp.shape : "";
        const std::string_view variant =
            stamp.visualVariant ? stamp.visualVariant : "";
        const auto* sourceReference = stamp.hasSourceReference
            ? findTerrainTile(
                  terrainTiles,
                  stamp.sourceReference.gridX,
                  stamp.sourceReference.gridZ)
            : nullptr;
        const bool sourceReferenceValid =
            !stamp.hasSourceReference ||
            (sourceReference && sourceReference->sourceOccupied);
        if (stamp.offsetGridX < -512 ||
            stamp.offsetGridZ < -512 ||
            stamp.offsetGridX > 512 ||
            stamp.offsetGridZ > 512 ||
            stamp.relativeElevationLevel < -256 ||
            stamp.relativeElevationLevel > 256 ||
            stamp.absoluteElevationLevel < -128 ||
            stamp.absoluteElevationLevel > 128 ||
            !stampOffsets.emplace(
                stamp.offsetGridX,
                stamp.offsetGridZ).second ||
            !validSurfaceId(surface) ||
            !validShapeId(shape) ||
            !validVariantForSurface(surface, variant) ||
            !sourceReferenceValid ||
            (variant != "auto" && shape != "flat") ||
            (surface == "empty" &&
             (shape != "flat" || variant != "auto"))) {
            if (outError) {
                *outError =
                    "The copied terrain footprint contains an invalid tile state.";
            }
            return false;
        }
    }
    return true;
}

} // namespace

bool buildTerrainTileEdit(
    const engine::editor::EditorProjectTerrainTileEditRequest& request,
    const std::vector<route1::TerrainTileState>& terrainTiles,
    const route1::BoardLayoutTransform& currentLayout,
    std::string_view terrainTileSetAssetId,
    TerrainTileEditResult& outResult,
    std::string* outError) {
    outResult = {};
    if (!request.coordinates ||
        request.coordinateCount == 0u ||
        !request.operation) {
        if (outError) {
            *outError =
                "Terrain editing requires Route 1, an operation, and at least one selected tile.";
        }
        return false;
    }
    const std::string_view operation(request.operation);
    const std::string_view requestedSurface =
        request.surface ? request.surface : "";
    const std::string_view requestedShape =
        request.shape ? request.shape : "";
    const std::string_view requestedVisualVariant =
        request.visualVariant ? request.visualVariant : "";
    const bool pasteTilesRelative =
        operation == "paste_tiles_relative";
    const bool pasteTilesExact =
        operation == "paste_tiles_exact";
    const bool pasteTiles = pasteTilesRelative || pasteTilesExact;
    if (!validateRequest(
            request,
            terrainTiles,
            operation,
            requestedSurface,
            requestedShape,
            requestedVisualVariant,
            pasteTiles,
            outError)) {
        return false;
    }

    outResult.layout = currentLayout;
    auto& next = outResult.layout;
    std::set<std::pair<std::int32_t, std::int32_t>> visited;
    if (pasteTiles) {
        const auto anchor = request.coordinates[0];
        const auto* anchorTile = findTerrainTile(
            terrainTiles, anchor.gridX, anchor.gridZ);
        if (!anchorTile) {
            if (outError) {
                *outError =
                    "The paste anchor is outside the Route 1 authoring bounds.";
            }
            return false;
        }
        const std::int32_t destinationBaseLevel =
            anchorTile->elevationLevel;
        for (std::size_t index = 0u;
             index < request.stampTileCount;
             ++index) {
            const auto& stamp = request.stampTiles[index];
            const std::int32_t gridX =
                anchor.gridX + stamp.offsetGridX;
            const std::int32_t gridZ =
                anchor.gridZ + stamp.offsetGridZ;
            if (!visited.emplace(gridX, gridZ).second) {
                continue;
            }
            const auto* source =
                findTerrainTile(terrainTiles, gridX, gridZ);
            if (!source) {
                if (outError) {
                    *outError =
                        "The copied terrain footprint extends outside the Route 1 authoring bounds.";
                }
                return false;
            }
            auto& authored = ensureAuthoredTile(
                next,
                *source,
                gridX,
                gridZ,
                terrainTileSetAssetId,
                "terrain_tile_paste");
            authored.elevationLevel = pasteTilesExact
                ? stamp.absoluteElevationLevel
                : std::clamp(
                      destinationBaseLevel +
                          stamp.relativeElevationLevel,
                      -128,
                      128);
            authored.surface = stamp.surface;
            authored.shape = stamp.shape;
            authored.visualVariant = stamp.visualVariant;
            authored.sourceReference = stamp.hasSourceReference
                ? std::optional<std::array<std::int32_t, 2>>{
                      std::array<std::int32_t, 2>{
                          stamp.sourceReference.gridX,
                          stamp.sourceReference.gridZ}}
                : std::nullopt;
            authored.reason = "terrain_tile_paste";
        }
    }
    for (std::size_t index = 0u;
         !pasteTiles && index < request.coordinateCount;
         ++index) {
        const auto coordinate = request.coordinates[index];
        if (!visited.emplace(
                coordinate.gridX,
                coordinate.gridZ).second) {
            continue;
        }
        const auto* source = findTerrainTile(
            terrainTiles, coordinate.gridX, coordinate.gridZ);
        if (!source) {
            if (outError) {
                *outError =
                    "The selected terrain tile is outside the Route 1 authoring bounds.";
            }
            return false;
        }
        const auto authored = std::find_if(
            next.authoredTerrainTiles.begin(),
            next.authoredTerrainTiles.end(),
            [&](const auto& tile) {
                return tile.gridX == coordinate.gridX &&
                    tile.gridZ == coordinate.gridZ;
            });
        if (operation == "restore_source") {
            if (authored != next.authoredTerrainTiles.end()) {
                next.authoredTerrainTiles.erase(authored);
            }
            continue;
        }
        auto& edited = authored != next.authoredTerrainTiles.end()
            ? *authored
            : ensureAuthoredTile(
                  next,
                  *source,
                  coordinate.gridX,
                  coordinate.gridZ,
                  terrainTileSetAssetId,
                  "terrain_tile_authoring");
        // Ordinary authoring leaves exact source-reference mode so painting
        // or leveling the cell cannot be masked by canonical donor geometry.
        edited.sourceReference.reset();
        if (operation == "raise") {
            edited.elevationLevel = std::min(
                128, edited.elevationLevel + 1);
        } else if (operation == "lower") {
            edited.elevationLevel = std::max(
                -128, edited.elevationLevel - 1);
        } else if (operation == "terrace_raise") {
            edited.elevationLevel = std::min(
                128, edited.elevationLevel + 1);
            edited.shape = "flat";
            edited.visualVariant = "auto";
            edited.reason = "terrain_platform_authoring";
        } else if (operation == "terrace_lower") {
            edited.elevationLevel = std::max(
                -128, edited.elevationLevel - 1);
            edited.shape = "flat";
            edited.visualVariant = "auto";
            edited.reason = "terrain_platform_authoring";
        } else if (operation == "flatten_tidy") {
            edited.elevationLevel = request.targetElevationLevel;
            edited.shape = "flat";
            edited.visualVariant = "auto";
            edited.reason = "terrain_flatten_cleanup";
        } else if (operation == "platform_set") {
            edited.elevationLevel = request.targetElevationLevel;
            edited.surface = requestedSurface;
            if (requestedShape == "source") {
                edited.shape = source->sourceShape;
            } else if (requestedShape != "preserve") {
                edited.shape = requestedShape;
            }
            edited.visualVariant = "auto";
            edited.reason = "terrain_platform_profiled";
        } else if (operation == "tidy_surface") {
            edited.visualVariant = "auto";
            edited.reason = "terrain_surface_authoring";
        } else if (operation == "paint_surface") {
            edited.surface = requestedSurface;
            edited.visualVariant = "auto";
            if (edited.surface == "empty") {
                edited.shape = "flat";
            }
        } else if (operation == "set_shape") {
            if (edited.surface == "empty" &&
                requestedShape != "flat") {
                if (outError) {
                    *outError =
                        "An empty terrain cell cannot have a ramp shape.";
                }
                return false;
            }
            edited.shape = requestedShape;
            if (requestedShape != "flat") {
                edited.visualVariant = "auto";
            }
        } else if (operation == "swap_prefab") {
            edited.surface = requestedSurface;
            edited.shape = requestedShape;
            edited.visualVariant = requestedVisualVariant;
            if (request.relativeElevationDelta != 0) {
                edited.elevationLevel = std::clamp(
                    edited.elevationLevel +
                        request.relativeElevationDelta,
                    -128,
                    128);
                edited.reason = "terrain_platform_authoring";
            }
        }
    }
    outResult.affectedTileCount = visited.size();
    if (outError) {
        outError->clear();
    }
    return true;
}

route1::BoardLayoutTransform boardRegistrationFromCenter(
    const route1::BoardLayoutTransform& currentLayout,
    const std::array<float, 3>& requestedCenterCm,
    float terrainTileSizeCm,
    float terrainElevationStepCm) {
    auto next = currentLayout;
    next.terrainGridOrigin = {
        static_cast<std::int32_t>(std::llround(
            requestedCenterCm[0] / terrainTileSizeCm -
            static_cast<float>(next.boardCells[0]) * 0.5f)),
        static_cast<std::int32_t>(std::llround(
            requestedCenterCm[2] / terrainTileSizeCm -
            static_cast<float>(next.boardCells[1]) * 0.5f))};
    next.terrainElevationLevel =
        static_cast<std::int32_t>(std::llround(
            requestedCenterCm[1] / terrainElevationStepCm));
    route1::bindBoardLayoutToTerrainGrid(next);
    return next;
}

route1::BoardLayoutTransform defaultBoardRegistration(
    const route1::BoardLayoutTransform& currentLayout,
    const std::array<std::int32_t, 2>& defaultTerrainGridOrigin) {
    auto next = currentLayout;
    next.terrainGridOrigin = defaultTerrainGridOrigin;
    next.terrainElevationLevel = 0;
    route1::bindBoardLayoutToTerrainGrid(next);
    return next;
}

bool sameBoardRegistration(
    const route1::BoardLayoutTransform& left,
    const route1::BoardLayoutTransform& right) noexcept {
    return left.terrainGridOrigin == right.terrainGridOrigin &&
        left.terrainElevationLevel == right.terrainElevationLevel;
}

route1::BoardLayoutTransform importedSceneBaseline(
    const route1::BoardLayoutTransform& currentLayout) {
    auto baseline = currentLayout;
    baseline.localLayoutDeltas.clear();
    baseline.objectMetadataOverrides.clear();
    baseline.authoredPrefabInstances.clear();
    baseline.authoredTerrainTiles.clear();
    baseline.declaredLocalDeltaCount = 0u;
    return baseline;
}

bool buildBoardClearancePlan(
    const commands::BoardClearanceRequest& request,
    const route1::BoardLayoutTransform& currentLayout,
    const std::vector<route1::LayoutObject>& objects,
    const std::vector<route1::TerrainTileState>& terrainTiles,
    const BoardClearanceConfig& config,
    BoardClearancePlan& outPlan,
    std::string* outError) {
    outPlan = {};
    const glm::mat4 worldFromSource = glm::make_mat4(
        route1::worldFromSourceMatrix(currentLayout).data());
    const float paddingWorld =
        std::max(0.0f, request.paddingCells) *
        config.boardCellSizeWorld;
    const float boardHalfWidth =
        static_cast<float>(currentLayout.boardCells[0]) *
        config.boardCellSizeWorld * 0.5f;
    const float boardHalfDepth =
        static_cast<float>(currentLayout.boardCells[1]) *
        config.boardCellSizeWorld * 0.5f;
    struct Footprint {
        float minX = 0.0f;
        float maxX = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
    };
    std::vector<Footprint> footprints{{
        -boardHalfWidth - paddingWorld,
        boardHalfWidth + paddingWorld,
        -boardHalfDepth - paddingWorld,
        boardHalfDepth + paddingWorld}};
    const float benchGapWorld =
        static_cast<float>(currentLayout.benchGapCells) *
        config.boardCellSizeWorld;
    const float benchHalfWidth =
        static_cast<float>(currentLayout.benchSlots) *
        config.boardCellSizeWorld * 0.5f;
    if (currentLayout.northBench) {
        footprints.push_back({
            -benchHalfWidth - paddingWorld,
            benchHalfWidth + paddingWorld,
            boardHalfDepth + benchGapWorld - paddingWorld,
            boardHalfDepth + benchGapWorld +
                config.boardCellSizeWorld + paddingWorld});
    }
    if (currentLayout.southBench) {
        footprints.push_back({
            -benchHalfWidth - paddingWorld,
            benchHalfWidth + paddingWorld,
            -boardHalfDepth - benchGapWorld -
                config.boardCellSizeWorld - paddingWorld,
            -boardHalfDepth - benchGapWorld + paddingWorld});
    }
    const auto overlapsFootprint =
        [&](float minX, float maxX,
            float minZ, float maxZ) {
            return std::any_of(
                footprints.begin(),
                footprints.end(),
                [&](const Footprint& footprint) {
                    return maxX >= footprint.minX &&
                        minX <= footprint.maxX &&
                        maxZ >= footprint.minZ &&
                        minZ <= footprint.maxZ;
                });
        };
    const auto intersectsBoard =
        [&](const route1::LayoutObject& object) {
            glm::vec3 minimum(
                std::numeric_limits<float>::max());
            glm::vec3 maximum(
                std::numeric_limits<float>::lowest());
            for (std::uint32_t corner = 0u;
                 corner < 8u;
                 ++corner) {
                const glm::vec4 world =
                    worldFromSource * glm::vec4(
                        (corner & 1u) != 0u
                            ? object.boundsMaximumCm[0]
                            : object.boundsMinimumCm[0],
                        (corner & 2u) != 0u
                            ? object.boundsMaximumCm[1]
                            : object.boundsMinimumCm[1],
                        (corner & 4u) != 0u
                            ? object.boundsMaximumCm[2]
                            : object.boundsMinimumCm[2],
                        1.0f);
                minimum = glm::min(minimum, glm::vec3(world));
                maximum = glm::max(maximum, glm::vec3(world));
            }
            return overlapsFootprint(
                minimum.x,
                maximum.x,
                minimum.z,
                maximum.z);
        };

    for (const auto& object : objects) {
        if (object.suppressed ||
            object.stableId == config.groundPrototypeStableId ||
            object.prefabAssetId == config.groundPrefabAssetId ||
            !intersectsBoard(object)) {
            continue;
        }
        const bool terrain =
            object.targetKind == "canonical_terrain_assembly";
        const bool ramp = terrain &&
            object.categoryPath.find("/Ramps") !=
                std::string::npos;
        const bool exactVegetation =
            object.targetKind == "canonical_tree_instance" ||
            object.targetKind == "encounter_grass_record" ||
            object.targetKind ==
                "buildmodel_vegetation_placement" ||
            (object.authored &&
             object.categoryPath.rfind(
                 "Environment/Vegetation",
                 0u) == 0u);
        const bool aggregateVegetation =
            object.targetKind == "canonical_mesh_group" &&
            object.categoryPath.rfind(
                "Environment/Vegetation",
                0u) == 0u;
        const bool objectObstruction =
            object.categoryPath.rfind(
                "Environment/Props",
                0u) == 0u ||
            (object.authored && !terrain && !exactVegetation);
        if (ramp && request.retainRamps) {
            ++outPlan.result.retainedRampCount;
            continue;
        }
        if (aggregateVegetation) {
            ++outPlan.result.skippedUnsafeAggregateCount;
            continue;
        }
        if (terrain && request.clearTerrain &&
            request.addGroundInfill) {
            continue;
        }
        if (terrain && request.clearTerrain) {
            outPlan.suppressStableIds.push_back(object.stableId);
            ++outPlan.result.suppressedTerrainCount;
        } else if (exactVegetation &&
                   request.clearVegetation) {
            outPlan.suppressStableIds.push_back(object.stableId);
            ++outPlan.result.suppressedVegetationCount;
        } else if (objectObstruction &&
                   request.clearObjects) {
            outPlan.suppressStableIds.push_back(object.stableId);
            ++outPlan.result.suppressedObjectCount;
        }
    }

    if (request.addGroundInfill) {
        for (const auto& sourceTile : terrainTiles) {
            glm::vec2 minimum(
                std::numeric_limits<float>::max());
            glm::vec2 maximum(
                std::numeric_limits<float>::lowest());
            for (std::uint32_t corner = 0u;
                 corner < 4u;
                 ++corner) {
                const glm::vec4 world =
                    worldFromSource * glm::vec4(
                        (static_cast<float>(sourceTile.gridX) +
                         ((corner & 1u) != 0u ? 1.0f : 0.0f)) *
                            config.terrainTileSizeCm,
                        static_cast<float>(
                            sourceTile.elevationLevel) *
                            config.terrainElevationStepCm,
                        (static_cast<float>(sourceTile.gridZ) +
                         ((corner & 2u) != 0u ? 1.0f : 0.0f)) *
                            config.terrainTileSizeCm,
                        1.0f);
                minimum = glm::min(
                    minimum, glm::vec2(world.x, world.z));
                maximum = glm::max(
                    maximum, glm::vec2(world.x, world.z));
            }
            if (!overlapsFootprint(
                    minimum.x,
                    maximum.x,
                    minimum.y,
                    maximum.y)) {
                continue;
            }
            outPlan.groundInfillTiles.push_back(
                route1::AuthoredTerrainTile{
                    .stableId =
                        route1::route1TerrainTileStableId(
                            sourceTile.gridX,
                            sourceTile.gridZ),
                    .displayName =
                        "Board Ground Tile (" +
                        std::to_string(sourceTile.gridX) + ", " +
                        std::to_string(sourceTile.gridZ) + ")",
                    .categoryPath =
                        "Environment/Terrain/Gameplay Board",
                    .tileSetAssetId =
                        std::string(config.terrainTileSetAssetId),
                    .gridX = sourceTile.gridX,
                    .gridZ = sourceTile.gridZ,
                    .elevationLevel =
                        currentLayout.terrainElevationLevel,
                    .surface = "light_lawn",
                    .shape = "flat",
                    .reason =
                        "autochess_board_ground_infill"});
        }
        if (outPlan.groundInfillTiles.empty()) {
            if (outError) {
                *outError =
                    "The autochess board footprint did not overlap the Route 1 terrain grid.";
            }
            return false;
        }
        outPlan.result.groundInfillCreated = true;
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

} // namespace game::editor::scene_mutations
