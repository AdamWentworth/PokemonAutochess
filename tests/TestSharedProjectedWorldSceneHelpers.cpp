#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

bool test_shared_projected_world_scene_helpers_contract(std::string& outFail) {
    using game::runtime::shared_projected_scene::resolveModelMesh;

    {
        using game::runtime::shared_board_grid::Config;

        struct CapturedQuad {
            float minZ = 0.0f;
            float maxZ = 0.0f;
            float red = 0.0f;
        };

        Config cfg;
        cfg.supportsWorldTriangles3D = true;
        cfg.rows = 4;
        cfg.cols = 8;
        cfg.benchSlots = 8;
        cfg.benchGapCells = 0;
        cfg.worldCellSize = 1.0f;
        cfg.boardMinX = -4.0f;
        cfg.boardMinZ = -2.0f;
        cfg.boardMaxX = 4.0f;
        cfg.boardMaxZ = 2.0f;

        std::vector<IRenderBackend::DebugTriangle> worldTriangles;
        std::vector<IRenderBackend::WorldTriangle> world3DTriangles;
        std::vector<IRenderBackend::DebugQuad> backgroundQuads;
        std::vector<IRenderBackend::DebugLine> lines;
        std::vector<CapturedQuad> captured;
        const auto captureWorldQuad =
            [&](const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c,
                const glm::vec3& d,
                float red,
                float,
                float,
                float) {
                captured.push_back({
                    std::min({a.z, b.z, c.z, d.z}),
                    std::max({a.z, b.z, c.z, d.z}),
                    red});
            };
        const auto noProjectedQuad =
            [](const glm::vec3&,
               const glm::vec3&,
               const glm::vec3&,
               const glm::vec3&,
               float,
               float,
               float,
               float) {};
        const auto noProjectedLine =
            [](const glm::vec3&,
               const glm::vec3&,
               float,
               float,
               float,
               float,
               float) {};
        const auto hasBenchQuad = [&](float minZ, float maxZ) {
            return std::any_of(
                captured.begin(),
                captured.end(),
                [&](const CapturedQuad& quad) {
                    const bool benchColor =
                        std::fabs(quad.red - 0.075f) <= 0.0001f ||
                        std::fabs(quad.red - 0.105f) <= 0.0001f;
                    return benchColor &&
                        std::fabs(quad.minZ - minZ) <= 0.0001f &&
                        std::fabs(quad.maxZ - maxZ) <= 0.0001f;
                });
        };
        const auto append = [&] {
            game::runtime::shared_board_grid::appendBoardAndBench(
                cfg,
                worldTriangles,
                world3DTriangles,
                backgroundQuads,
                lines,
                captureWorldQuad,
                noProjectedQuad,
                noProjectedLine);
        };

        append();
        if (!hasBenchQuad(2.0f, 3.0f) || !hasBenchQuad(-3.0f, -2.0f)) {
            outFail =
                "Zero-gap board geometry must place both bench rows directly against the board.";
            return false;
        }

        captured.clear();
        cfg.benchGapCells = 1;
        append();
        if (!hasBenchQuad(3.0f, 4.0f) || !hasBenchQuad(-4.0f, -3.0f)) {
            outFail =
                "Board geometry must preserve an explicitly configured bench gap.";
            return false;
        }

        cfg.benchGapCells = 0;
        cfg.emitFlatGrid = false;
        cfg.sampleSurfaceHeight =
            [](float worldX, float worldZ, float& outWorldY) {
                outWorldY = 1.0f + worldX * 0.10f + worldZ * 0.05f;
                return true;
            };
        std::vector<
            game::runtime::shared_world_batches::WorldIndexedBatch>
            terrainGridBatches;
        game::runtime::shared_board_grid::
            appendTerrainConformingBoardAndBench(
                cfg, terrainGridBatches);
        if (terrainGridBatches.size() != 1u ||
            terrainGridBatches.front().alphaMode != 2u ||
            terrainGridBatches.front().vertices.empty()) {
            outFail =
                "Terrain-conforming gameplay grids must use one translucent indexed-world batch.";
            return false;
        }
        float minimumY = 1000.0f;
        float minimumAlpha = 1.0f;
        float maximumAlpha = 0.0f;
        float maximumZ = -1000.0f;
        for (const auto& vertex :
             terrainGridBatches.front().vertices) {
            minimumY = std::min(minimumY, vertex.y);
            minimumAlpha = std::min(minimumAlpha, vertex.a);
            maximumAlpha = std::max(maximumAlpha, vertex.a);
            maximumZ = std::max(maximumZ, vertex.z);
        }
        if (minimumY < 0.40f || maximumZ < 2.99f ||
            maximumAlpha <= minimumAlpha) {
            outFail =
                "Terrain-conforming gameplay grids must follow sampled elevations, include the bench, and distinguish mild tile seams from stronger boundaries.";
            return false;
        }
    }

    {
        GameDataDb dataDb;
        PokemonInstance unit;
        unit.name = "missingno";
        unit.backendModelPath = "assets/models/cached_backend_path.glb";

        game::runtime::render_model::MeshData mesh;
        mesh.indices = {0u, 1u, 2u};

        std::string seenPath;
        const auto* resolved = resolveModelMesh(
            unit,
            dataDb,
            [&](const std::string& modelPath) -> game::runtime::render_model::MeshData* {
                seenPath = modelPath;
                return &mesh;
            });
        if (resolved != &mesh || seenPath != unit.backendModelPath) {
            outFail = "resolveModelMesh should prefer cached backendModelPath";
            return false;
        }
    }

    {
        GameDataDb dataDb;
        PokemonInstance unit;
        unit.name = "missingno";
        unit.animIndexCacheSourceModelPath = "assets/models/cached_anim_source.glb";

        game::runtime::render_model::MeshData mesh;
        mesh.indices = {0u, 1u, 2u};

        std::string seenPath;
        const auto* resolved = resolveModelMesh(
            unit,
            dataDb,
            [&](const std::string& modelPath) -> game::runtime::render_model::MeshData* {
                seenPath = modelPath;
                return &mesh;
            });
        if (resolved != &mesh || seenPath != unit.animIndexCacheSourceModelPath) {
            outFail = "resolveModelMesh should fall back to cached animation source model path";
            return false;
        }
    }

    {
        GameDataDb dataDb;
        PokemonInstance unit;
        unit.name = "missingno";
        unit.backendAnimDurationsSourceModelPath = "assets/models/cached_duration_source.glb";

        game::runtime::render_model::MeshData mesh;
        mesh.indices = {0u, 1u, 2u};

        std::string seenPath;
        const auto* resolved = resolveModelMesh(
            unit,
            dataDb,
            [&](const std::string& modelPath) -> game::runtime::render_model::MeshData* {
                seenPath = modelPath;
                return &mesh;
            });
        if (resolved != &mesh || seenPath != unit.backendAnimDurationsSourceModelPath) {
            outFail = "resolveModelMesh should fall back to cached backend animation duration model path";
            return false;
        }
    }

    return true;
}

