#include "game/runtime/shared/scene/Route1TerrainPatchCooker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using game::runtime::route1_environment::TerrainTileState;

TerrainTileState tile(
    std::int32_t x,
    std::int32_t z,
    bool authored = false,
    std::int32_t elevation = 0) {
    TerrainTileState value;
    value.gridX = x;
    value.gridZ = z;
    value.sourceElevationLevel = 0;
    value.elevationLevel = elevation;
    value.sourceSurface = "light_lawn";
    value.surface = "light_lawn";
    value.sourceShape = "flat";
    value.shape = "flat";
    value.sourceOccupied = true;
    value.authored = authored;
    return value;
}

std::vector<TerrainTileState> rectangle(
    std::int32_t minimumX,
    std::int32_t maximumX,
    std::int32_t minimumZ,
    std::int32_t maximumZ) {
    std::vector<TerrainTileState> tiles;
    for (std::int32_t x = minimumX; x <= maximumX; ++x) {
        for (std::int32_t z = minimumZ; z <= maximumZ; ++z) {
            tiles.push_back(tile(x, z));
        }
    }
    return tiles;
}

TerrainTileState* find(
    std::vector<TerrainTileState>& tiles,
    std::int32_t x,
    std::int32_t z) {
    for (auto& candidate : tiles) {
        if (candidate.gridX == x && candidate.gridZ == z) {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace

bool test_route1_terrain_patch_cooker_contract(std::string& outFail) {
    namespace patch =
        game::runtime::route1_terrain_patch_v2;

    {
        const auto empty = patch::cook(rectangle(0, 3, 0, 3));
        if (!empty.regions.empty() || !empty.validation.valid) {
            outFail =
                "Terrain Patch V2 must not replace untouched source terrain.";
            return false;
        }
    }

    {
        auto tiles = rectangle(0, 3, 0, 3);
        find(tiles, 1, 1)->authored = true;
        const auto metadataOnly = patch::cook(tiles);
        if (!metadataOnly.regions.empty()) {
            outFail =
                "A source-identical authored tile must not grant Terrain Patch V2 permission to replace imported geometry.";
            return false;
        }
    }

    {
        auto tiles = rectangle(0, 3, 0, 3);
        for (const auto cell : {
                 patch::GridCell{1, 1}, patch::GridCell{1, 2},
                 patch::GridCell{2, 1}, patch::GridCell{2, 2}}) {
            auto* edited = find(tiles, cell.first, cell.second);
            edited->authored = true;
            edited->elevationLevel = 1;
        }
        const auto plan = patch::cook(tiles);
        if (!plan.validation.valid ||
            plan.regions.size() != 1u ||
            plan.coreCellCount != 4u ||
            plan.transitionCellCount != 12u ||
            plan.boundaryLoopCount != 2u ||
            plan.boundaryEdgeCount != 24u) {
            outFail =
                "A two-by-two edit must cook as one connected region with "
                "one source transition ring and two closed perimeters.";
            return false;
        }
    }

    {
        auto tiles = rectangle(0, 7, 0, 3);
        find(tiles, 2, 1)->authored = true;
        find(tiles, 2, 1)->elevationLevel = 1;
        find(tiles, 5, 1)->authored = true;
        find(tiles, 5, 1)->elevationLevel = 1;
        const auto plan = patch::cook(tiles);
        if (!plan.validation.valid || plan.regions.size() != 1u) {
            outFail =
                "Overlapping source-transition rings must merge into one "
                "regional cook instead of retaining independent tile seams.";
            return false;
        }
    }

    {
        // Route 1's recurring failure was the visible top-surface delimiter
        // from edited (16,-4) into source (16,-5). V2 must place both cells
        // inside one regional mesh; the outer source handoff may not run
        // through their shared edge.
        auto tiles = rectangle(14, 18, -7, -2);
        find(tiles, 16, -4)->authored = true;
        find(tiles, 16, -4)->elevationLevel = 1;
        find(tiles, 16, -5)->sourceElevationLevel = 1;
        find(tiles, 16, -5)->elevationLevel = 1;
        const auto plan = patch::cook(tiles);
        bool transitionIncluded = false;
        bool outerBoundarySplitsPair = false;
        for (const auto& region : plan.regions) {
            for (const auto& cell : region.cells) {
                transitionIncluded |=
                    cell.cell == patch::GridCell{16, -5} &&
                    cell.role == patch::CellRole::SourceTransition;
            }
            for (const auto& boundary : region.boundaries) {
                if (boundary.kind != patch::BoundaryKind::PatchToSource) {
                    continue;
                }
                for (const auto& edge : boundary.edges) {
                    outerBoundarySplitsPair |=
                        (edge.ownerCell == patch::GridCell{16, -4} &&
                         edge.outsideCell == patch::GridCell{16, -5}) ||
                        (edge.ownerCell == patch::GridCell{16, -5} &&
                         edge.outsideCell == patch::GridCell{16, -4});
                }
            }
        }
        if (!plan.validation.valid ||
            !transitionIncluded ||
            outerBoundarySplitsPair) {
            outFail =
                "The (16,-4)/(16,-5) Route 1 seam fixture must belong to "
                "one regional surface and hand off to source outside that pair.";
            return false;
        }
    }

    {
        auto tiles = rectangle(0, 4, 0, 4);
        for (auto& value : tiles) {
            if (!(value.gridX == 2 && value.gridZ == 2)) {
                value.authored = true;
                value.elevationLevel = 1;
            } else {
                value.surface = "empty";
                value.sourceSurface = "empty";
                value.sourceOccupied = false;
            }
        }
        const auto plan = patch::cook(tiles);
        if (!plan.validation.valid ||
            plan.regions.size() != 1u ||
            plan.boundaryLoopCount != 4u) {
            outFail =
                "Regional terrain with an interior void must retain closed "
                "outer and inner contours for both cook boundaries.";
            return false;
        }
    }

    return true;
}
