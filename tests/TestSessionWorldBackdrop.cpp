#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/world/SharedBoardGridBatches.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/glm.hpp>

bool test_session_world_backdrop_contract(std::string& outFail) {
    using game::runtime::session_render_scratch::RenderScratch;
    using game::runtime::session_world_backdrop::ProjectedBackdropArgs;
    using game::runtime::session_world_backdrop::composeProjectedBackdrop;

    auto makeProjectedDebug = [](bool supportsWorldTriangles3D,
                                 RenderScratch& scratch) {
        const glm::mat4 view(1.0f);
        const glm::mat4 proj(1.0f);
        const glm::vec4 viewport(0.0f, 0.0f, 1280.0f, 720.0f);
        return game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder(
            supportsWorldTriangles3D,
            view,
            proj,
            720,
            viewport,
            scratch.worldTriangles,
            scratch.world3DTriangles,
            scratch.lines);
    };

    auto makeArgs = [](bool supportsWorldTriangles3D) {
        ProjectedBackdropArgs args;
        args.supportsWorldTriangles3D = supportsWorldTriangles3D;
        args.enableBackdropTiles = true;
        args.rows = 4;
        args.cols = 8;
        args.benchSlots = 8;
        args.worldCellSize = 1.0f;
        args.boardMinX = -4.0f;
        args.boardMinZ = -2.0f;
        args.boardMaxX = 4.0f;
        args.boardMaxZ = 2.0f;
        args.boardX = 100.0f;
        args.boardY = 80.0f;
        args.boardW = 800.0f;
        args.boardH = 400.0f;
        args.cellW = 100.0f;
        args.cellH = 100.0f;
        args.line = 2.0f;
        args.theme = game::runtime::session_world_backdrop::ArenaBackdropTheme::Route1OpenRoad;
        return args;
    };

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        const ProjectedBackdropArgs args = makeArgs(true);
        const float firstComposeMs = composeProjectedBackdrop(args, projectedDebug, scratch);
        const std::size_t cachedBackgroundCount = scratch.worldBackgroundQuads.size();
        const std::size_t cachedTriangleCount = scratch.worldTriangles.size();
        const std::size_t cachedWorld3DCount = scratch.world3DTriangles.size();
        const std::size_t cachedIndexedBatchCount = scratch.worldIndexedBatches.size();
        const std::size_t cachedLineCount = scratch.lines.size();
        const std::string cachedIndexedGeometryKey =
            cachedIndexedBatchCount > 0u ? scratch.worldIndexedBatches.front().geometryCacheKey
                                         : std::string{};
        if (!scratch.projectedBackdropValid ||
            scratch.projectedBackdropWorldBackgroundQuadsCount != cachedBackgroundCount ||
            scratch.projectedBackdropWorldTrianglesCount != cachedTriangleCount ||
            scratch.projectedBackdropWorld3DTrianglesCount != cachedWorld3DCount ||
            scratch.projectedBackdropWorldIndexedBatchesCount != cachedIndexedBatchCount ||
            scratch.projectedBackdropLinesCount != cachedLineCount ||
            firstComposeMs < 0.0f) {
            outFail = "SessionWorldBackdrop should populate and cache projected backdrop geometry.";
            return false;
        }
        if (scratch.worldBackgroundQuads.empty() ||
            scratch.worldBackgroundQuads.front().r != 0.0f ||
            scratch.worldBackgroundQuads.front().g != 0.0f ||
            scratch.worldBackgroundQuads.front().b != 0.0f ||
            scratch.worldBackgroundQuads.front().a != 1.0f) {
            outFail =
                "SessionWorldBackdrop should use a solid black backdrop fill behind the route environment.";
            return false;
        }

        const float boardSurfaceY =
            game::runtime::shared_board_grid::defaultVisualTheme().boardSurfaceY;
        const float gridY =
            game::runtime::shared_board_grid::defaultVisualTheme().gridY;
        const auto isFlatAtY = [](const IRenderBackend::WorldTriangle& tri, float y) {
            return std::fabs(tri.y1 - y) <= 0.0001f &&
                   std::fabs(tri.y2 - y) <= 0.0001f &&
                   std::fabs(tri.y3 - y) <= 0.0001f;
        };

        const bool route1HasAnyBoardFill = std::any_of(
            scratch.world3DTriangles.begin(),
            scratch.world3DTriangles.end(),
            [&](const auto& tri) {
                return isFlatAtY(tri, boardSurfaceY);
            });
        if (route1HasAnyBoardFill) {
            outFail =
                "SessionWorldBackdrop Route 1 board cells should be grid-only with no fill geometry at board surface height.";
            return false;
        }

        const bool route1HasVisibleGrid = std::any_of(
            scratch.world3DTriangles.begin(),
            scratch.world3DTriangles.end(),
            [&](const auto& tri) {
                return isFlatAtY(tri, gridY) && tri.a > 0.001f;
            });
        if (!route1HasVisibleGrid) {
            outFail =
                "SessionWorldBackdrop Route 1 should keep visible board grid quads when board fills are hidden.";
            return false;
        }

        scratch.worldBackgroundQuads.push_back({});
        scratch.worldTriangles.push_back({});
        scratch.world3DTriangles.push_back({});
        scratch.worldIndexedBatches.push_back({});
        scratch.lines.push_back({});
        const float secondComposeMs = composeProjectedBackdrop(args, projectedDebug, scratch);
        if (scratch.worldBackgroundQuads.size() != cachedBackgroundCount ||
            scratch.worldTriangles.size() != cachedTriangleCount ||
            scratch.world3DTriangles.size() != cachedWorld3DCount ||
            scratch.worldIndexedBatches.size() != cachedIndexedBatchCount ||
            (cachedIndexedBatchCount > 0u &&
             scratch.worldIndexedBatches.front().geometryCacheKey != cachedIndexedGeometryKey) ||
            scratch.lines.size() != cachedLineCount ||
            secondComposeMs < 0.0f) {
            outFail = "SessionWorldBackdrop should reuse cached projected backdrop sizes for unchanged keys.";
            return false;
        }

        ProjectedBackdropArgs spacedBenchArgs = args;
        spacedBenchArgs.benchGapCells = 1;
        composeProjectedBackdrop(spacedBenchArgs, projectedDebug, scratch);
        if (!scratch.projectedBackdropValid ||
            scratch.projectedBackdropKey.benchGapCells != 1) {
            outFail =
                "SessionWorldBackdrop should invalidate cached geometry when the bench gap changes.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        args.supportsWorldIndexedMeshes = true;
        args.enableBackdropTiles = false;

        game::runtime::SharedBackendTextureCacheEntry boardTexture;
        boardTexture.attemptedLoad = true;
        boardTexture.valid = true;
        boardTexture.width = 512;
        boardTexture.height = 256;
        boardTexture.rgba.resize(
            static_cast<std::size_t>(boardTexture.width * boardTexture.height * 4),
            255u);

        game::runtime::SharedBackendTextureCacheEntry grassTexture;
        grassTexture.attemptedLoad = true;
        grassTexture.valid = true;
        grassTexture.width = 2;
        grassTexture.height = 2;
        grassTexture.rgba = {
            140, 200,  90, 255,
            118, 182,  74, 255,
            132, 194,  82, 255,
            150, 210, 100, 255,
        };

        args.ensureBackendTextureLoaded =
            [&](const std::string& texturePath, bool flipVertical)
                -> game::runtime::SharedBackendTextureCacheEntry* {
                if (!flipVertical &&
                    texturePath ==
                        "assets/textures/environment/board_dirt_grass_border_4x4.png") {
                    return &boardTexture;
                }
                if (!flipVertical &&
                    texturePath ==
                        "assets/textures/environment/grass_fill_2x2.png") {
                    return &grassTexture;
                }
                return nullptr;
            };
        composeProjectedBackdrop(args, projectedDebug, scratch);
        if (!scratch.worldIndexedBatches.empty()) {
            outFail =
                "SessionWorldBackdrop should skip backdrop mesh and tile overlays when backdrop tiles are disabled for VFX inspection.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        composeProjectedBackdrop(args, projectedDebug, scratch);
        args.theme = game::runtime::session_world_backdrop::ArenaBackdropTheme::ViridianForestShrine;
        composeProjectedBackdrop(args, projectedDebug, scratch);
        if (!scratch.projectedBackdropValid ||
            scratch.projectedBackdropKey.arenaBackdropTheme !=
                static_cast<int>(game::runtime::session_world_backdrop::ArenaBackdropTheme::ViridianForestShrine)) {
            outFail = "SessionWorldBackdrop should rebuild and retheme cached backdrop geometry when the route theme changes, even if the simplified scene keeps the same triangle counts.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        scratch.projectedBackdropValid = true;
        scratch.projectedBackdropWorldBackgroundQuadsCount = 3u;
        auto projectedDebug = makeProjectedDebug(false, scratch);
        const ProjectedBackdropArgs args = makeArgs(false);
        composeProjectedBackdrop(args, projectedDebug, scratch);
        if (scratch.projectedBackdropValid ||
            scratch.projectedBackdropWorldBackgroundQuadsCount != 0u ||
            scratch.projectedBackdropWorldTrianglesCount != 0u ||
            scratch.projectedBackdropWorld3DTrianglesCount != 0u ||
            scratch.projectedBackdropWorldIndexedBatchesCount != 0u ||
            scratch.projectedBackdropLinesCount != 0u) {
            outFail = "SessionWorldBackdrop should build noncached projected backdrop geometry when 3D world triangles are unavailable.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        args.supportsWorldIndexedMeshes = true;

        args.route1BackdropTuning =
            game::runtime::session_world_backdrop::defaultRoute1BackdropTuningState();
        args.route1BackdropTuning.scaleMul = 6.5f;
        args.route1BackdropTuning.offsetXCells = 1.5f;
        args.route1BackdropTuning.offsetY = 1.25f;
        args.route1BackdropTuning.offsetZCells = -0.5f;
        const std::string tuningText =
            game::runtime::session_world_backdrop::formatRoute1BackdropTuningState(
                args.route1BackdropTuning);
        if (tuningText.find("scale_mul=6.500") == std::string::npos ||
            tuningText.find("offset_x_cells=1.500") == std::string::npos ||
            tuningText.find("offset_y=1.250") == std::string::npos ||
            tuningText.find("offset_z_cells=-0.500") == std::string::npos) {
            outFail =
                "SessionWorldBackdrop should expose a stable Route 1 tuning format for terminal copy/paste.";
            return false;
        }

        composeProjectedBackdrop(args, projectedDebug, scratch);
        const auto route1BatchIt = std::find_if(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            [](const auto& batch) {
                return batch.geometryCacheKey.find("session_world_backdrop_route1_pattern_") !=
                       std::string::npos;
            });
        if (route1BatchIt == scratch.worldIndexedBatches.end()) {
            outFail =
                "SessionWorldBackdrop should append the Route 1 pattern overlay around the cooked environment.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        args.supportsWorldIndexedMeshes = true;

        game::runtime::SharedBackendTextureCacheEntry grassTexture;
        grassTexture.attemptedLoad = true;
        grassTexture.valid = true;
        grassTexture.width = 2;
        grassTexture.height = 2;
        grassTexture.rgba = {
            140, 200,  90, 255,
            118, 182,  74, 255,
            132, 194,  82, 255,
            150, 210, 100, 255,
        };

        args.ensureBackendTextureLoaded =
            [&](const std::string& texturePath, bool flipVertical)
                -> game::runtime::SharedBackendTextureCacheEntry* {
                if (!flipVertical &&
                    texturePath ==
                        "assets/textures/environment/grass_fill_2x2.png") {
                    return &grassTexture;
                }
                return nullptr;
            };

        composeProjectedBackdrop(args, projectedDebug, scratch);
        const bool foundGrassBatch = std::any_of(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            [](const auto& batch) {
                return batch.textureCacheKey ==
                           "assets/textures/environment/grass_fill_2x2.png" &&
                       batch.geometryCacheKey.find(
                           "session_world_backdrop_bench_tiles_") == std::string::npos;
            });
        if (foundGrassBatch) {
            outFail =
                "SessionWorldBackdrop should not append route grass geometry when the backdrop is simplified to just the board and bench.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        args.supportsWorldIndexedMeshes = true;

        game::runtime::SharedBackendTextureCacheEntry boardTexture;
        boardTexture.attemptedLoad = true;
        boardTexture.valid = true;
        boardTexture.width = 512;
        boardTexture.height = 256;
        boardTexture.rgba.resize(
            static_cast<std::size_t>(boardTexture.width * boardTexture.height * 4),
            255u);

        game::runtime::SharedBackendTextureCacheEntry grassTexture;
        grassTexture.attemptedLoad = true;
        grassTexture.valid = true;
        grassTexture.width = 2;
        grassTexture.height = 2;
        grassTexture.rgba = {
            140, 200,  90, 255,
            118, 182,  74, 255,
            132, 194,  82, 255,
            150, 210, 100, 255,
        };

        args.ensureBackendTextureLoaded =
            [&](const std::string& texturePath, bool flipVertical)
                -> game::runtime::SharedBackendTextureCacheEntry* {
                if (!flipVertical &&
                    texturePath ==
                        "assets/textures/environment/board_dirt_grass_border_4x4.png") {
                    return &boardTexture;
                }
                if (!flipVertical &&
                    texturePath ==
                        "assets/textures/environment/grass_fill_2x2.png") {
                    return &grassTexture;
                }
                return nullptr;
            };

        composeProjectedBackdrop(args, projectedDebug, scratch);
        const bool foundRoute1BoardOverlay = std::any_of(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            [](const auto& batch) {
                return batch.textureCacheKey ==
                           "assets/textures/environment/board_dirt_grass_border_4x4.png" &&
                       batch.geometryCacheKey.find(
                           "session_world_backdrop_board_tiles_") != std::string::npos;
            });
        const bool foundRoute1BenchOverlay = std::any_of(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            [](const auto& batch) {
                return batch.textureCacheKey ==
                           "assets/textures/environment/grass_fill_2x2.png" &&
                       batch.geometryCacheKey.find(
                           "session_world_backdrop_bench_tiles_") != std::string::npos;
            });
        const auto route1PatternBoardIt = std::find_if(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            [](const auto& batch) {
                return batch.textureCacheKey ==
                           "__session_world_backdrop_route1_pattern_white__" &&
                       batch.geometryCacheKey.find(
                           "session_world_backdrop_route1_pattern_board_") != std::string::npos;
            });
        const auto isRoute1BenchPattern = [](const auto& batch) {
            return batch.textureCacheKey ==
                       "__session_world_backdrop_route1_pattern_white__" &&
                   batch.geometryCacheKey.find(
                       "session_world_backdrop_route1_pattern_bench-") != std::string::npos;
        };
        const auto route1PatternBenchIt = std::find_if(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            isRoute1BenchPattern);
        const std::size_t route1PatternBenchCount =
            static_cast<std::size_t>(std::count_if(
                scratch.worldIndexedBatches.begin(),
                scratch.worldIndexedBatches.end(),
                isRoute1BenchPattern));
        if (foundRoute1BoardOverlay || foundRoute1BenchOverlay) {
            outFail =
                "SessionWorldBackdrop should hide textured board and bench tile overlays for the Route 1 authored environment.";
            return false;
        }
        if (route1PatternBoardIt == scratch.worldIndexedBatches.end() ||
            route1PatternBenchIt == scratch.worldIndexedBatches.end() ||
            route1PatternBenchCount != 2u) {
            outFail =
                "SessionWorldBackdrop should replace Route 1 tile textures with a translucent indexed-batch checker pattern.";
            return false;
        }
        if (route1PatternBoardIt->alphaMode != 2u ||
            route1PatternBoardIt->indices.size() !=
                static_cast<std::size_t>(args.rows * args.cols * 6) ||
            route1PatternBoardIt->vertices.empty() ||
            route1PatternBoardIt->vertices.front().a >= 0.35f ||
            route1PatternBoardIt->vertices.front().a <= 0.10f) {
            outFail =
                "SessionWorldBackdrop Route 1 board pattern should stay blended and visibly translucent so the authored environment still drives the color.";
            return false;
        }
        if (route1PatternBenchIt->alphaMode != 2u ||
            route1PatternBenchIt->indices.size() !=
                static_cast<std::size_t>(args.benchSlots * 6) ||
            route1PatternBenchIt->vertices.empty() ||
            route1PatternBenchIt->vertices.front().a >= 0.35f ||
            route1PatternBenchIt->vertices.front().a <= 0.10f) {
            outFail =
                "SessionWorldBackdrop Route 1 bench pattern should stay blended and visibly translucent like the main board overlay.";
            return false;
        }

        args.theme =
            game::runtime::session_world_backdrop::ArenaBackdropTheme::Route22Foothills;
        scratch = RenderScratch{};
        auto projectedDebugOtherRoute = makeProjectedDebug(true, scratch);
        composeProjectedBackdrop(args, projectedDebugOtherRoute, scratch);
        const auto boardBatchIt = std::find_if(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            [](const auto& batch) {
                return batch.textureCacheKey ==
                           "assets/textures/environment/board_dirt_grass_border_4x4.png" &&
                       batch.geometryCacheKey.find(
                           "session_world_backdrop_board_tiles_") != std::string::npos;
            });
        if (boardBatchIt == scratch.worldIndexedBatches.end()) {
            outFail =
                "SessionWorldBackdrop should still append the board dirt overlay when the backdrop is simplified to just the board and bench.";
            return false;
        }
        if (boardBatchIt->indices.size() !=
            static_cast<std::size_t>(args.rows * args.cols * 6)) {
            outFail =
                "SessionWorldBackdrop dirt board batch should still cover every board cell in the simplified backdrop.";
            return false;
        }

        const auto benchBatchIt = std::find_if(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            [](const auto& batch) {
                return batch.textureCacheKey ==
                           "assets/textures/environment/grass_fill_2x2.png" &&
                       batch.geometryCacheKey.find(
                           "session_world_backdrop_bench_tiles_") != std::string::npos;
            });
        if (benchBatchIt == scratch.worldIndexedBatches.end()) {
            outFail =
                "SessionWorldBackdrop should append a matching grass tile overlay for the bench in the simplified backdrop.";
            return false;
        }
        if (benchBatchIt->indices.size() !=
            static_cast<std::size_t>(args.benchSlots * 2 * 6)) {
            outFail =
                "SessionWorldBackdrop bench grass batch should cover every bench slot in the simplified backdrop.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        args.supportsWorldIndexedMeshes = true;

        game::runtime::SharedBackendTextureCacheEntry ledgeTexture;
        ledgeTexture.attemptedLoad = true;
        ledgeTexture.valid = true;
        ledgeTexture.width = 64;
        ledgeTexture.height = 48;
        ledgeTexture.rgba.resize(
            static_cast<std::size_t>(ledgeTexture.width * ledgeTexture.height * 4),
            255u);

        args.ensureBackendTextureLoaded =
            [&](const std::string& texturePath, bool flipVertical)
                -> game::runtime::SharedBackendTextureCacheEntry* {
                if (!flipVertical &&
                    texturePath ==
                        "assets/textures/environment/ledge_front_wall_4x3.png") {
                    return &ledgeTexture;
                }
                return nullptr;
            };

        composeProjectedBackdrop(args, projectedDebug, scratch);
        const bool foundLedgeBatch = std::any_of(
            scratch.worldIndexedBatches.begin(),
            scratch.worldIndexedBatches.end(),
            [](const auto& batch) {
                return batch.textureCacheKey ==
                    "assets/textures/environment/ledge_front_wall_4x3.png";
            });
        if (foundLedgeBatch) {
            outFail =
                "SessionWorldBackdrop should ignore route ledge overlays when the backdrop is simplified to just the board and bench.";
            return false;
        }
    }

    {
        RenderScratch scratch;
        auto projectedDebug = makeProjectedDebug(true, scratch);
        ProjectedBackdropArgs args = makeArgs(true);
        args.supportsWorldIndexedMeshes = true;
        args.graphicsQuality = 3;
        args.theme =
            game::runtime::session_world_backdrop::ArenaBackdropTheme::Route22Foothills;

        composeProjectedBackdrop(args, projectedDebug, scratch);
        if (!scratch.worldIndexedBatches.empty()) {
            outFail =
                "SessionWorldBackdrop should not append standalone route props when the authored Route 1 scene is inactive.";
            return false;
        }
    }

    {
        using game::runtime::session_world_backdrop::ArenaBackdropTheme;
        using game::runtime::session_world_backdrop::routeThemeUsesAuthoredRoute1Fallback;
        using game::runtime::session_world_backdrop::routeThemeFromScriptPath;

        if (routeThemeFromScriptPath("scripts/states/route22_shop.lua") !=
                ArenaBackdropTheme::Route22Foothills ||
            routeThemeFromScriptPath("scripts/states/route2_shop.lua") !=
                ArenaBackdropTheme::Route2ForestEdge ||
            routeThemeFromScriptPath("scripts/states/route1.lua") !=
                ArenaBackdropTheme::Route1OpenRoad ||
            routeThemeFromScriptPath("scripts/states/starter.lua") !=
                ArenaBackdropTheme::Route1OpenRoad) {
            outFail =
                "SessionWorldBackdrop should resolve planning and combat route scripts to the correct route themes.";
            return false;
        }
        if (!routeThemeUsesAuthoredRoute1Fallback(
                ArenaBackdropTheme::Route1OpenRoad) ||
            !routeThemeUsesAuthoredRoute1Fallback(
                ArenaBackdropTheme::Route22Foothills) ||
            !routeThemeUsesAuthoredRoute1Fallback(
                ArenaBackdropTheme::Route2ForestEdge) ||
            !routeThemeUsesAuthoredRoute1Fallback(
                ArenaBackdropTheme::ViridianForestShrine) ||
            !routeThemeUsesAuthoredRoute1Fallback(
                ArenaBackdropTheme::Route3MountainPass) ||
            routeThemeUsesAuthoredRoute1Fallback(
                ArenaBackdropTheme::Default)) {
            outFail =
                "SessionWorldBackdrop should temporarily resolve every route theme, but not the non-route default, to the authored Route 1 environment.";
            return false;
        }
    }

    return true;
}
