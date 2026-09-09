#include "game/runtime/shared/scene/legacy/Route1SourceTerrainRules.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace game::runtime::route1_environment {
namespace {
constexpr float kTerrainTileSizeCm = 100.0f;
constexpr float kTerrainElevationStepCm = 50.0f;
} // namespace
bool route1TerrainPinsSourceSurface(
    const TerrainTileState &tile) noexcept {
    return tile.reason == "terrain_source_restore" &&
           tile.authored && tile.sourceOccupied &&
           !tile.sourceReference &&
           tile.elevationLevel == tile.sourceElevationLevel &&
           tile.shape == tile.sourceShape &&
           tile.surface == tile.sourceSurface &&
           tile.visualVariant == "auto" &&
           !tile.normalizeSourceTint;
}

bool route1TerrainBelongsToUnchangedSourceLedgeAssembly(
    const TerrainTileState &tile,
    const std::vector<TerrainTileState> &activeTiles,
    const std::vector<TerrainTileState> &sourceTiles) noexcept {
    // Imported lawn ledges are coupled assemblies: the high cap, leafy crown,
    // wall, foot, and low-side contact can cross the logical cell edge. If a
    // regional lawn rebuild claims either untouched side, the source carrier
    // meets a generated square and exposes a crease or tile rectangle. Keep
    // both sides source-owned whenever their shared ledge profile is still
    // exactly the one decoded from Route 1. Authored/new ledges remain on the
    // generated path because neither side may be an authored override.
    if (tile.authored || !tile.sourceOccupied || tile.sourceReference ||
        !tile.surface.ends_with("lawn") || tile.shape != "flat" ||
        tile.surface != tile.sourceSurface ||
        tile.shape != tile.sourceShape ||
        tile.elevationLevel != tile.sourceElevationLevel ||
        tile.visualVariant != "auto" || tile.normalizeSourceTint ||
        tile.cleanSuppressedEncounterGrassTint) {
        return false;
    }
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    const auto findAt = [](const auto &tiles,
                           std::int32_t gridX,
                           std::int32_t gridZ) {
        const auto found = std::find_if(
            tiles.begin(),
            tiles.end(),
            [&](const TerrainTileState &candidate) {
                return candidate.gridX == gridX &&
                       candidate.gridZ == gridZ;
            });
        return found == tiles.end() ? nullptr : &*found;
    };
    const auto sourceEquivalentFlatLawn =
        [&](const TerrainTileState *candidate) {
            if (!candidate || candidate->authored ||
                !candidate->sourceOccupied ||
                candidate->sourceReference ||
                !candidate->surface.ends_with("lawn") ||
                candidate->shape != "flat" ||
                candidate->surface != candidate->sourceSurface ||
                candidate->shape != candidate->sourceShape ||
                candidate->elevationLevel !=
                    candidate->sourceElevationLevel ||
                candidate->visualVariant != "auto" ||
                candidate->normalizeSourceTint ||
                candidate->cleanSuppressedEncounterGrassTint) {
                return false;
            }
            return findAt(
                       sourceTiles,
                       candidate->gridX,
                       candidate->gridZ) != nullptr;
        };
    if (!sourceEquivalentFlatLawn(&tile)) {
        return false;
    }
    const auto touchesChangedSourceMaterial =
        [&](const TerrainTileState &candidate) {
            for (const auto &direction : directions) {
                const auto *adjacent = findAt(
                    activeTiles,
                    candidate.gridX + direction[0],
                    candidate.gridZ + direction[1]);
                const auto *sourceAdjacent = findAt(
                    sourceTiles,
                    candidate.gridX + direction[0],
                    candidate.gridZ + direction[1]);
                const bool hasAdjacent = adjacent &&
                                         adjacent->surface != "empty" &&
                                         (adjacent->sourceOccupied || adjacent->authored);
                const bool hasSourceAdjacent = sourceAdjacent &&
                                               sourceAdjacent->surface != "empty" &&
                                               sourceAdjacent->sourceOccupied;
                if (hasAdjacent != hasSourceAdjacent ||
                    (hasAdjacent &&
                     (adjacent->sourceReference ||
                      adjacent->surface !=
                          sourceAdjacent->surface ||
                      (adjacent->authored &&
                       adjacent->surface !=
                           candidate.surface)))) {
                    return true;
                }
            }
            return false;
        };
    const auto unchangedLedgeNeighbor =
        [&](const TerrainTileState &candidate,
            std::size_t edge) -> const TerrainTileState * {
        if (!sourceEquivalentFlatLawn(&candidate)) {
            return nullptr;
        }
        const auto &direction = directions[edge];
        const auto *neighbor = findAt(
            activeTiles,
            candidate.gridX + direction[0],
            candidate.gridZ + direction[1]);
        if (!sourceEquivalentFlatLawn(neighbor)) {
            return nullptr;
        }
        const auto *sourceCandidate = findAt(
            sourceTiles, candidate.gridX, candidate.gridZ);
        const auto *sourceNeighbor = findAt(
            sourceTiles,
            candidate.gridX + direction[0],
            candidate.gridZ + direction[1]);
        if (!sourceCandidate || !sourceNeighbor) {
            return nullptr;
        }
        const auto activeProfile =
            route1TerrainSharedEdgeProfile(
                candidate, neighbor, edge);
        const auto sourceProfile =
            route1TerrainSharedEdgeProfile(
                *sourceCandidate, sourceNeighbor, edge);
        return activeProfile.tileLevels ==
                           sourceProfile.tileLevels &&
                       activeProfile.neighborLevels ==
                           sourceProfile.neighborLevels &&
                       sourceProfile.tileLevels !=
                           sourceProfile.neighborLevels
                   ? neighbor
                   : nullptr;
    };
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        tangents{{
            {1, 0},
            {0, -1},
            {-1, 0},
            {0, 1},
        }};
    const auto unchangedLedgeEdgeCount =
        [&](const TerrainTileState &candidate) {
            std::size_t count = 0u;
            for (std::size_t candidateEdge = 0u;
                 candidateEdge < directions.size();
                 ++candidateEdge) {
                if (unchangedLedgeNeighbor(
                        candidate, candidateEdge)) {
                    ++count;
                }
            }
            return count;
        };
    for (std::size_t edge = 0u; edge < directions.size(); ++edge) {
        const auto *initialNeighbor =
            unchangedLedgeNeighbor(tile, edge);
        if (!initialNeighbor ||
            unchangedLedgeEdgeCount(tile) != 1u ||
            unchangedLedgeEdgeCount(*initialNeighbor) != 1u) {
            continue;
        }
        bool cleanStraightRun = true;
        for (const std::int32_t sign : {-1, 1}) {
            const TerrainTileState *runTile = &tile;
            while (runTile) {
                const auto *runNeighbor =
                    unchangedLedgeNeighbor(*runTile, edge);
                if (!runNeighbor) {
                    break;
                }
                if (unchangedLedgeEdgeCount(*runTile) != 1u ||
                    unchangedLedgeEdgeCount(*runNeighbor) != 1u) {
                    break;
                }
                if (touchesChangedSourceMaterial(*runTile) ||
                    touchesChangedSourceMaterial(*runNeighbor)) {
                    cleanStraightRun = false;
                    break;
                }
                runTile = findAt(
                    activeTiles,
                    runTile->gridX + tangents[edge][0] * sign,
                    runTile->gridZ + tangents[edge][1] * sign);
            }
            if (!cleanStraightRun) {
                break;
            }
        }
        if (cleanStraightRun) {
            return true;
        }
    }

    // A turn in an otherwise untouched ledge has two ledge edges, so the
    // straight-run rule above intentionally cannot classify the turn cell.
    // Leaving just that cap/contact pair in generated ownership still cuts a
    // square into both adjoining imported runs. Admit the turn when this edge
    // is attached to at least one clean straight source segment. We inspect
    // the complete attached segment so a nearby authored material change
    // continues to keep the whole affected run on the generated path.
    for (std::size_t edge = 0u; edge < directions.size(); ++edge) {
        const auto *initialNeighbor =
            unchangedLedgeNeighbor(tile, edge);
        if (!initialNeighbor ||
            touchesChangedSourceMaterial(tile) ||
            touchesChangedSourceMaterial(*initialNeighbor)) {
            continue;
        }
        bool foundStraightContinuation = false;
        bool cleanStraightContinuation = true;
        for (const std::int32_t sign : {-1, 1}) {
            const TerrainTileState *runTile = findAt(
                activeTiles,
                tile.gridX + tangents[edge][0] * sign,
                tile.gridZ + tangents[edge][1] * sign);
            while (runTile) {
                const auto *runNeighbor =
                    unchangedLedgeNeighbor(*runTile, edge);
                if (!runNeighbor ||
                    unchangedLedgeEdgeCount(*runTile) != 1u ||
                    unchangedLedgeEdgeCount(*runNeighbor) != 1u) {
                    break;
                }
                foundStraightContinuation = true;
                if (touchesChangedSourceMaterial(*runTile) ||
                    touchesChangedSourceMaterial(*runNeighbor)) {
                    cleanStraightContinuation = false;
                    break;
                }
                runTile = findAt(
                    activeTiles,
                    runTile->gridX + tangents[edge][0] * sign,
                    runTile->gridZ + tangents[edge][1] * sign);
            }
            if (!cleanStraightContinuation) {
                break;
            }
        }
        if (foundStraightContinuation && cleanStraightContinuation) {
            return true;
        }
    }
    return false;
}

bool route1TerrainUsesExactSourceSurfaceOverride(
    const TerrainTileState &tile,
    const std::vector<TerrainTileState> &activeTiles,
    const std::vector<TerrainTileState> &sourceTiles) noexcept {
    // These controls affect runtime ownership or rendering, not the imported
    // material-19 surface itself. Keep the original triangles authoritative
    // and, when necessary, resubmit them under the requested shadow policy.
    // Encounter-grass edits are excluded because their source Color0 field is
    // deliberately normalized after the grass carrier is removed/restored.
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    const auto findAt = [](const auto &tiles,
                           std::int32_t gridX,
                           std::int32_t gridZ) {
        const auto found = std::find_if(
            tiles.begin(),
            tiles.end(),
            [&](const TerrainTileState &candidate) {
                return candidate.gridX == gridX &&
                       candidate.gridZ == gridZ;
            });
        return found == tiles.end() ? nullptr : &*found;
    };
    const auto hasSurface = [](const TerrainTileState *candidate) {
        return candidate && candidate->surface != "empty" &&
               (candidate->sourceOccupied || candidate->authored);
    };
    const auto *sourceTile = findAt(
        sourceTiles, tile.gridX, tile.gridZ);
    if (!sourceTile) {
        return false;
    }
    if (route1TerrainPinsSourceSurface(tile)) {
        return true;
    }
    const auto sourceLedgeBoundaryCount =
        [&](const TerrainTileState &sourceCandidate) {
            std::size_t boundaryCount = 0u;
            for (std::size_t edge = 0u;
                 edge < directions.size();
                 ++edge) {
                const auto direction = directions[edge];
                const auto *sourceNeighbor = findAt(
                    sourceTiles,
                    sourceCandidate.gridX + direction[0],
                    sourceCandidate.gridZ + direction[1]);
                if (!hasSurface(sourceNeighbor)) {
                    continue;
                }
                const auto sourceProfile =
                    route1TerrainSharedEdgeProfile(
                        sourceCandidate, sourceNeighbor, edge);
                if (sourceProfile.tileLevels !=
                    sourceProfile.neighborLevels) {
                    ++boundaryCount;
                }
            }
            return boundaryCount;
        };
    const auto sourceBoundaryCount =
        sourceLedgeBoundaryCount(*sourceTile);
    const bool hasSourceLedgeBoundary = sourceBoundaryCount != 0u;
    const bool preservesAuthoredSourceSurface =
        tile.authored && !tile.rebuildContinuousMaterialFields;
    // Source corner assemblies can cross the V2 region boundary by one cell.
    // Extend exact ownership only between adjacent flat cells that both carry
    // a two-sided source ledge; ordinary flat lawn, ramps, and straight ledge
    // runs remain on their existing regional generation path.
    const bool adjoinsIntactTransitionLedgeAssembly =
        tile.shape == "flat" &&
        sourceBoundaryCount >= 2u &&
        std::any_of(
            activeTiles.begin(),
            activeTiles.end(),
            [&](const TerrainTileState &candidate) {
                const auto *sourceCandidate = findAt(
                    sourceTiles,
                    candidate.gridX,
                    candidate.gridZ);
                return std::abs(candidate.gridX - tile.gridX) +
                               std::abs(candidate.gridZ - tile.gridZ) ==
                           1 &&
                       !candidate.authored &&
                       candidate.terrainPatchV2RegionId != 0u &&
                       !candidate.terrainPatchV2Core &&
                       candidate.rebuildContinuousMaterialFields &&
                       !candidate.sourceLedgeCarrierDisplaced &&
                       candidate.sourceOccupied &&
                       !candidate.sourceReference &&
                       candidate.elevationLevel ==
                           candidate.sourceElevationLevel &&
                       candidate.shape == candidate.sourceShape &&
                       candidate.surface == candidate.sourceSurface &&
                       candidate.surface == tile.surface &&
                       sourceCandidate &&
                       sourceLedgeBoundaryCount(*sourceCandidate) >= 2u;
            });
    const bool extendsIntactTransitionLedgeAssembly =
        tile.terrainPatchV2RegionId == 0u &&
        adjoinsIntactTransitionLedgeAssembly;
    const bool preservesIntactTransitionLedgeAssembly =
        !tile.authored &&
        (tile.terrainPatchV2RegionId != 0u ||
         extendsIntactTransitionLedgeAssembly) &&
        !tile.terrainPatchV2Core &&
        tile.rebuildContinuousMaterialFields &&
        !tile.sourceLedgeCarrierDisplaced;
    const bool sourceEquivalent =
        (preservesAuthoredSourceSurface ||
         preservesIntactTransitionLedgeAssembly) &&
        tile.sourceOccupied &&
        !tile.sourceReference &&
        tile.elevationLevel == tile.sourceElevationLevel &&
        tile.shape == tile.sourceShape &&
        tile.surface == tile.sourceSurface &&
        tile.visualVariant == "auto" &&
        !tile.normalizeSourceTint &&
        !tile.cleanSuppressedEncounterGrassTint &&
        !tile.reason.starts_with("terrain_encounter_grass_");
    if (!sourceEquivalent) {
        return false;
    }
    for (std::size_t edge = 0u;
         edge < directions.size();
         ++edge) {
        const auto direction = directions[edge];
        const auto *activeNeighbor = findAt(
            activeTiles,
            tile.gridX + direction[0],
            tile.gridZ + direction[1]);
        const auto *sourceNeighbor = findAt(
            sourceTiles,
            tile.gridX + direction[0],
            tile.gridZ + direction[1]);
        if (hasSurface(activeNeighbor) != hasSurface(sourceNeighbor) &&
            !extendsIntactTransitionLedgeAssembly) {
            return false;
        }
        if (!hasSurface(activeNeighbor) || !sourceNeighbor) {
            continue;
        }
        const auto activeProfile = route1TerrainSharedEdgeProfile(
            tile, activeNeighbor, edge);
        const auto sourceProfile = route1TerrainSharedEdgeProfile(
            *sourceTile, sourceNeighbor, edge);
        const bool changesSourceMaterialOnly =
            (activeNeighbor->cleanSuppressedEncounterGrassTint &&
             !activeNeighbor->regionalMaterialHandoffOnly) ||
            activeNeighbor->surface != sourceNeighbor->surface;
        if (activeNeighbor->sourceReference ||
            (!extendsIntactTransitionLedgeAssembly &&
             (changesSourceMaterialOnly ||
              activeProfile.tileLevels !=
                  sourceProfile.tileLevels ||
              activeProfile.neighborLevels !=
                  sourceProfile.neighborLevels))) {
            return false;
        }
    }
    const auto neighborTouchesChangedMaterialField =
        [&](const TerrainTileState &candidate) {
            const auto *sourceCandidate = findAt(
                sourceTiles,
                candidate.gridX,
                candidate.gridZ);
            if (!sourceCandidate || candidate.sourceReference ||
                candidate.surface != sourceCandidate->surface) {
                return true;
            }
            for (std::size_t candidateEdge = 0u;
                 candidateEdge < directions.size();
                 ++candidateEdge) {
                const auto candidateDirection =
                    directions[candidateEdge];
                const auto *activeAdjacent = findAt(
                    activeTiles,
                    candidate.gridX + candidateDirection[0],
                    candidate.gridZ + candidateDirection[1]);
                const auto *sourceAdjacent = findAt(
                    sourceTiles,
                    candidate.gridX + candidateDirection[0],
                    candidate.gridZ + candidateDirection[1]);
                if (hasSurface(activeAdjacent) !=
                    hasSurface(sourceAdjacent)) {
                    return true;
                }
                if (!hasSurface(activeAdjacent)) {
                    continue;
                }
                if (activeAdjacent->sourceReference ||
                    activeAdjacent->surface !=
                        sourceAdjacent->surface) {
                    return true;
                }
            }
            return false;
        };
    if (preservesIntactTransitionLedgeAssembly &&
        hasSourceLedgeBoundary &&
        !extendsIntactTransitionLedgeAssembly) {
        // A straight source ledge cap can sit between several regenerated
        // regional cells even when none of its own four boundaries changed.
        // Preserving that one square as exact source geometry leaves a
        // source-coloured island through the dirt ribbon, raised lawn, and
        // lower contact field. Follow each source-compatible neighbor by one
        // boundary ring: if that neighbor touches an active/source material
        // change, both sides of an unchanged ledge belong to the same
        // regenerated material field. The cliff/fringe carrier remains
        // source-owned; only material-19's top and foot-contact ground are
        // regenerated. Isolated compound source corners such as (26,-13)
        // have no nearby material change and retain their exact cap.
        for (std::size_t edge = 0u;
             edge < directions.size();
             ++edge) {
            const auto direction = directions[edge];
            const auto *activeNeighbor = findAt(
                activeTiles,
                tile.gridX + direction[0],
                tile.gridZ + direction[1]);
            const auto *sourceNeighbor = findAt(
                sourceTiles,
                tile.gridX + direction[0],
                tile.gridZ + direction[1]);
            if (!hasSurface(activeNeighbor) || !sourceNeighbor ||
                activeNeighbor->surface != tile.surface ||
                sourceNeighbor->surface != sourceTile->surface) {
                continue;
            }
            const auto activeProfile =
                route1TerrainSharedEdgeProfile(
                    tile, activeNeighbor, edge);
            const auto sourceProfile =
                route1TerrainSharedEdgeProfile(
                    *sourceTile, sourceNeighbor, edge);
            const bool preservesSourceEdgeProfile =
                activeProfile.tileLevels == sourceProfile.tileLevels &&
                activeProfile.neighborLevels ==
                    sourceProfile.neighborLevels;
            if (preservesSourceEdgeProfile &&
                neighborTouchesChangedMaterialField(
                    *activeNeighbor)) {
                return false;
            }
        }
    }
    // A V2 transition field normally needs its generated regional material
    // handoff. The exception is an unchanged imported ledge cell whose
    // cliff/fringe cleanup carrier is still authoritative. Its material-19
    // cap, leafy crown, wall, and foot are one coupled source assembly; mixing
    // a generated square cap with the imported curved corner produces the
    // diagonal flap and black wedge seen at cells such as South Clearing
    // (26,-13). Preserve the exact cap only for that ledge-bearing case.
    return !preservesIntactTransitionLedgeAssembly ||
           hasSourceLedgeBoundary;
}

bool route1TerrainUsesRegionalExactSourceLawnMaterial(
    std::string_view sceneId,
    const TerrainTileState &tile,
    const std::vector<TerrainTileState> &activeTiles,
    const std::vector<TerrainTileState> &sourceTiles) noexcept {
    // An unchanged transition-ring ledge must retain its complete imported
    // geometry assembly. Its material-19 lawn cap is independent, however:
    // leaving that cap on the original per-cell UV1/Color0 field makes the
    // upper part of the source tile visible as a straight rectangle beside
    // regenerated lawn. Keep the imported geometry, wall, and fringe exact,
    // but let the cap's complete lawn material and shading basis continue
    // from the final neighboring regional field.
    return route1UsesRegionalTerrainMaterialField(sceneId) &&
           tile.surface == "light_lawn" &&
           !tile.terrainPatchV2Core &&
           tile.rebuildContinuousMaterialFields &&
           route1TerrainUsesExactSourceSurfaceOverride(
               tile, activeTiles, sourceTiles);
}

bool route1TerrainCanPreserveRelativeSourceGeometry(
    const TerrainTileState &tile,
    const std::vector<TerrainTileState> &activeTiles,
    const std::vector<TerrainTileState> &sourceTiles) noexcept {
    if (!tile.sourceOccupied || tile.sourceReference ||
        tile.shape != tile.sourceShape) {
        return false;
    }
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    const auto findAt = [](const auto &tiles,
                           std::int32_t gridX,
                           std::int32_t gridZ) {
        const auto found = std::find_if(
            tiles.begin(),
            tiles.end(),
            [&](const TerrainTileState &candidate) {
                return candidate.gridX == gridX &&
                       candidate.gridZ == gridZ;
            });
        return found == tiles.end() ? nullptr : &*found;
    };
    const auto hasSurface = [](const TerrainTileState *candidate) {
        return candidate && candidate->surface != "empty" &&
               (candidate->sourceOccupied || candidate->authored);
    };
    const auto *sourceTile = findAt(
        sourceTiles, tile.gridX, tile.gridZ);
    if (!sourceTile) {
        return false;
    }
    for (std::size_t edge = 0u; edge < directions.size(); ++edge) {
        const auto direction = directions[edge];
        const auto *activeNeighbor = findAt(
            activeTiles,
            tile.gridX + direction[0],
            tile.gridZ + direction[1]);
        const auto *sourceNeighbor = findAt(
            sourceTiles,
            tile.gridX + direction[0],
            tile.gridZ + direction[1]);
        if (hasSurface(activeNeighbor) != hasSurface(sourceNeighbor)) {
            return false;
        }
        if (!hasSurface(activeNeighbor)) {
            continue;
        }
        const auto activeProfile = route1TerrainSharedEdgeProfile(
            tile, activeNeighbor, edge);
        const auto sourceProfile = route1TerrainSharedEdgeProfile(
            *sourceTile, sourceNeighbor, edge);
        for (std::size_t endpoint = 0u; endpoint < 2u; ++endpoint) {
            const auto activeDifference =
                activeProfile.tileLevels[endpoint] -
                activeProfile.neighborLevels[endpoint];
            const auto sourceDifference =
                sourceProfile.tileLevels[endpoint] -
                sourceProfile.neighborLevels[endpoint];
            if (activeDifference != sourceDifference) {
                return false;
            }
        }
    }
    return true;
}

float route1TerrainProfileHeightCm(
    const TerrainTileState &tile,
    float localX,
    float localZ) noexcept {
    localX = std::clamp(localX, 0.0f, 1.0f);
    localZ = std::clamp(localZ, 0.0f, 1.0f);
    float rampHeight = 0.0f;
    if (tile.shape == "ramp_north") {
        rampHeight = localZ * kTerrainElevationStepCm;
    } else if (tile.shape == "ramp_east") {
        rampHeight = localX * kTerrainElevationStepCm;
    } else if (tile.shape == "ramp_south") {
        rampHeight = (1.0f - localZ) *
                     kTerrainElevationStepCm;
    } else if (tile.shape == "ramp_west") {
        rampHeight = (1.0f - localX) *
                     kTerrainElevationStepCm;
    }
    return static_cast<float>(tile.elevationLevel) *
               kTerrainElevationStepCm +
           rampHeight;
}

bool route1TerrainSourceBoundaryInvalidated(
    const TerrainTileState &editedTile,
    const TerrainTileState *editedNeighbor,
    const TerrainTileState &sourceTile,
    const TerrainTileState *sourceNeighbor,
    std::size_t edge) noexcept {
    const auto editedProfile = route1TerrainSharedEdgeProfile(
        editedTile, editedNeighbor, edge);
    const auto sourceProfile = route1TerrainSharedEdgeProfile(
        sourceTile, sourceNeighbor, edge);
    return editedProfile.tileLevels != sourceProfile.tileLevels ||
           editedProfile.neighborLevels != sourceProfile.neighborLevels;
}

bool route1TerrainSourcePatchNeedsBoundarySpill(
    const TerrainTileState &tile,
    const TerrainTileState *neighbor,
    std::size_t edge) noexcept {
    const auto profile = route1TerrainSharedEdgeProfile(
        tile,
        neighbor,
        edge);
    return profile.tileLevels != profile.neighborLevels;
}

bool route1TerrainCleanupCarrierEntersNeighbor(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return false;
    }
    constexpr float penetrationToleranceCm = 0.01f;
    if (deltaX != 0) {
        const float boundaryX = static_cast<float>(
                                    std::max(ownerCell[0], neighboringCell[0])) *
                                kTerrainTileSizeCm;
        return std::any_of(
            positionsCm.begin(),
            positionsCm.end(),
            [&](const auto &position) {
                return deltaX > 0
                           ? position[0] >
                                 boundaryX + penetrationToleranceCm
                           : position[0] <
                                 boundaryX - penetrationToleranceCm;
            });
    }
    const float boundaryZ = static_cast<float>(
                                std::max(ownerCell[1], neighboringCell[1])) *
                            kTerrainTileSizeCm;
    return std::any_of(
        positionsCm.begin(),
        positionsCm.end(),
        [&](const auto &position) {
            return deltaZ > 0
                       ? position[2] >
                             boundaryZ + penetrationToleranceCm
                       : position[2] <
                             boundaryZ - penetrationToleranceCm;
        });
}

bool route1TerrainCleanupCarrierWithinBoundaryBand(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return false;
    }
    // The decoded cliff profile bows at most 25 cm away from its owning
    // metre edge. A small source-coordinate tolerance retains its paired
    // triangle without claiming unrelated foliage deeper in the neighbor.
    constexpr float boundaryBandCm = 25.5f;
    constexpr float boundaryToleranceCm = 0.01f;
    const auto insideBand = [&](float coordinate, float boundary,
                                std::int32_t direction) {
        const float signedDistance =
            static_cast<float>(direction) * (coordinate - boundary);
        return signedDistance >= -boundaryToleranceCm &&
               signedDistance <= boundaryBandCm;
    };
    if (deltaX != 0) {
        const float boundaryX = static_cast<float>(
                                    std::max(ownerCell[0], neighboringCell[0])) *
                                kTerrainTileSizeCm;
        return std::all_of(
            positionsCm.begin(),
            positionsCm.end(),
            [&](const auto &position) {
                return insideBand(position[0], boundaryX, deltaX);
            });
    }
    const float boundaryZ = static_cast<float>(
                                std::max(ownerCell[1], neighboringCell[1])) *
                            kTerrainTileSizeCm;
    return std::all_of(
        positionsCm.begin(),
        positionsCm.end(),
        [&](const auto &position) {
            return insideBand(position[2], boundaryZ, deltaZ);
        });
}

bool route1TerrainCleanupCarrierIntersectsBoundaryBand(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return false;
    }
    constexpr float boundaryBandCm = 25.5f;
    constexpr float boundaryToleranceCm = 0.01f;
    float minimumDistance = std::numeric_limits<float>::max();
    float maximumDistance = std::numeric_limits<float>::lowest();
    if (deltaX != 0) {
        const float boundaryX = static_cast<float>(
                                    std::max(ownerCell[0], neighboringCell[0])) *
                                kTerrainTileSizeCm;
        for (const auto &position : positionsCm) {
            const float distance = static_cast<float>(deltaX) *
                                   (position[0] - boundaryX);
            minimumDistance = std::min(minimumDistance, distance);
            maximumDistance = std::max(maximumDistance, distance);
        }
    } else {
        const float boundaryZ = static_cast<float>(
                                    std::max(ownerCell[1], neighboringCell[1])) *
                                kTerrainTileSizeCm;
        for (const auto &position : positionsCm) {
            const float distance = static_cast<float>(deltaZ) *
                                   (position[2] - boundaryZ);
            minimumDistance = std::min(minimumDistance, distance);
            maximumDistance = std::max(maximumDistance, distance);
        }
    }
    return maximumDistance >= -boundaryToleranceCm &&
           minimumDistance <= boundaryBandCm;
}

bool route1TerrainCleanupCarrierWithinRebuiltBoundaryCorridor(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return false;
    }
    // Source cliff/fringe carriers bow toward both sides of their logical
    // metre edge. The convex crown recovered from LGPE reaches about 31 cm
    // into the raised owner cell, so the earlier outward-only 25.5 cm test
    // left one broad source triangle crossing an otherwise rebuilt corner.
    // Require the complete triangle to stay in this narrow two-sided
    // corridor; unrelated terrain deeper in either cell remains canonical.
    constexpr float boundaryCorridorCm = 32.5f;
    constexpr float boundaryToleranceCm = 0.01f;
    const float boundary = deltaX != 0
                               ? static_cast<float>(
                                     std::max(ownerCell[0], neighboringCell[0])) *
                                     kTerrainTileSizeCm
                               : static_cast<float>(
                                     std::max(ownerCell[1], neighboringCell[1])) *
                                     kTerrainTileSizeCm;
    return std::all_of(
        positionsCm.begin(),
        positionsCm.end(),
        [&](const auto &position) {
            const float coordinate = deltaX != 0
                                         ? position[0]
                                         : position[2];
            return std::abs(coordinate - boundary) <=
                   boundaryCorridorCm + boundaryToleranceCm;
        });
}

bool route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    float boundaryCeilingCm) noexcept {
    constexpr float heightToleranceCm = 0.01f;
    return std::all_of(
        positionsCm.begin(),
        positionsCm.end(),
        [&](const auto &position) {
            return position[1] <=
                   boundaryCeilingCm + heightToleranceCm;
        });
}

bool route1TerrainCleanupCarrierIntersectsCellFootprint(
    const std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &cell) noexcept {
    using Point = std::array<float, 2>;
    std::array<Point, 8> polygon{};
    std::size_t polygonSize = positionsCm.size();
    for (std::size_t index = 0u; index < positionsCm.size(); ++index) {
        polygon[index] = {positionsCm[index][0], positionsCm[index][2]};
    }

    const auto clip = [&](std::size_t axis,
                          float boundaryCm,
                          bool keepLess) {
        if (polygonSize == 0u) {
            return;
        }
        const auto inside = [&](const Point &point) {
            return keepLess
                       ? point[axis] <= boundaryCm
                       : point[axis] >= boundaryCm;
        };
        std::array<Point, 8> output{};
        std::size_t outputSize = 0u;
        const auto append = [&](const Point &point) {
            if (outputSize < output.size()) {
                output[outputSize++] = point;
            }
        };
        Point previous = polygon[polygonSize - 1u];
        bool previousInside = inside(previous);
        for (std::size_t index = 0u; index < polygonSize; ++index) {
            const Point current = polygon[index];
            const bool currentInside = inside(current);
            if (currentInside != previousInside) {
                const float denominator =
                    current[axis] - previous[axis];
                if (std::abs(denominator) > 1.0e-6f) {
                    const float weight = std::clamp(
                        (boundaryCm - previous[axis]) /
                            denominator,
                        0.0f,
                        1.0f);
                    append({std::lerp(previous[0], current[0], weight),
                            std::lerp(previous[1], current[1], weight)});
                }
            }
            if (currentInside) {
                append(current);
            }
            previous = current;
            previousInside = currentInside;
        }
        polygon = output;
        polygonSize = outputSize;
    };

    const float minimumX =
        static_cast<float>(cell[0]) * kTerrainTileSizeCm;
    const float maximumX = minimumX + kTerrainTileSizeCm;
    const float minimumZ =
        static_cast<float>(cell[1]) * kTerrainTileSizeCm;
    const float maximumZ = minimumZ + kTerrainTileSizeCm;
    clip(0u, minimumX, false);
    clip(0u, maximumX, true);
    clip(1u, minimumZ, false);
    clip(1u, maximumZ, true);
    if (polygonSize < 3u) {
        return false;
    }

    double doubleArea = 0.0;
    for (std::size_t index = 0u; index < polygonSize; ++index) {
        const auto &left = polygon[index];
        const auto &right = polygon[(index + 1u) % polygonSize];
        doubleArea += static_cast<double>(left[0]) * right[1] -
                      static_cast<double>(right[0]) * left[1];
    }
    // Merely touching a shared grid line does not transfer ownership. Only a
    // positive-area overlap means the imported carrier occupies this cell.
    constexpr double kMinimumDoubleAreaCm2 = 1.0e-4;
    return std::abs(doubleArea) > kMinimumDoubleAreaCm2;
}

void route1TerrainClampCleanupCarrierToOwnedCell(
    std::array<std::array<float, 3>, 3> &positionsCm,
    const std::array<std::int32_t, 2> &ownerCell,
    const std::array<std::int32_t, 2> &neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return;
    }
    if (deltaX != 0) {
        const float boundaryX = static_cast<float>(
                                    std::max(ownerCell[0], neighboringCell[0])) *
                                kTerrainTileSizeCm;
        for (auto &position : positionsCm) {
            position[0] = deltaX > 0
                              ? std::min(position[0], boundaryX)
                              : std::max(position[0], boundaryX);
        }
        return;
    }
    const float boundaryZ = static_cast<float>(
                                std::max(ownerCell[1], neighboringCell[1])) *
                            kTerrainTileSizeCm;
    for (auto &position : positionsCm) {
        position[2] = deltaZ > 0
                          ? std::min(position[2], boundaryZ)
                          : std::max(position[2], boundaryZ);
    }
}

bool route1TerrainMaskUsesAnyVertexOwnership(
    bool exactSourceReference) noexcept {
    // Ordinary authored replacements own every carrier touching the edited
    // cell so no source slivers survive. Exact source transplants instead
    // share their perimeter with canonical neighboring cells; assigning
    // those triangles by centroid keeps the canonical corner/fringe carrier
    // without also retaining the donor spill outside the transplant.
    return !exactSourceReference;
}

} // namespace game::runtime::route1_environment
