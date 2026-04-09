#include "game/runtime/session/SessionWorldBackdrop.h"

#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/video/VideoPreferences.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iterator>
#include <sstream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace game::runtime::session_world_backdrop {

namespace {

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

using RenderBuildClock = std::chrono::steady_clock;
using Color = std::array<float, 4>;

constexpr char kBackdropEvergreenTreeModelPath[] =
    "assets/models/environment/route_evergreen_tree.glb";
constexpr char kBackdropRoute1EnvironmentModelPath[] =
    "assets/models/environment/route1.glb";
constexpr char kBackdropRoute1PatternTextureKey[] =
    "__session_world_backdrop_route1_pattern_white__";
constexpr char kBackdropGrassTexturePath[] =
    "assets/textures/environment/grass_fill_2x2.png";
constexpr char kBackdropBoardDirtTexturePath[] =
    "assets/textures/environment/board_dirt_grass_border_4x4.png";
constexpr char kBackdropBoardLedgeTexturePath[] =
    "assets/textures/environment/ledge_front_wall_4x3.png";
constexpr Color kBackdropBlackFill = {0.0f, 0.0f, 0.0f, 1.0f};

// This is the extracted left-half of NatureRMXP band_00_group_01: a ready-to-
// use 4x4 grass-backed dirt/path set for the board.
constexpr int kBackdropBoardAtlasOriginXPx = 0;
constexpr int kBackdropBoardAtlasOriginYPx = 0;
constexpr int kBackdropBoardAtlasTileSizePx = 16;

struct AtlasTileCoord {
    int x = 0;
    int y = 0;
};

struct UvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

struct RouteShellStyle {
    shared_board_grid::VisualTheme boardTheme{};
    Color farFill{};
    Color lowerFill{};
    Color upperAccent{};
    Color plateauTop{};
    Color plateauSide{};
    Color distantGround{};
    Color path{};
    Color grass{};
    Color fence{};
    Color rock{};
    Color trunk{};
    Color leaves{};
    Color shrineStone{};
    float sideMargin = 4.0f;
    float backMargin = 4.0f;
    float frontMargin = 2.25f;
};

struct BackdropPropPlacement {
    float x = 0.0f;
    float z = 0.0f;
    float yawDeg = 0.0f;
    float scale = 1.0f;
};

struct BackdropPlayableBounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    float centerX = 0.0f;
    float centerZ = 0.0f;
    float width = 0.0f;
    float depth = 0.0f;
};

// Keep Route 1 transform tuning in one place while we dial the board alignment in.
constexpr float kBackdropRoute1ScaleMul = 3.25f;
constexpr float kBackdropRoute1CenterOffsetXCells = 0.0f;
constexpr float kBackdropRoute1CenterOffsetZCells = -3.0f;
constexpr float kBackdropRoute1CenterOffsetY = 3.55f;
constexpr float kBackdropRoute1YawDeg = 0.0f;

Color scaleColor(const Color& color, float scale) {
    return {
        std::clamp(color[0] * scale, 0.0f, 1.0f),
        std::clamp(color[1] * scale, 0.0f, 1.0f),
        std::clamp(color[2] * scale, 0.0f, 1.0f),
        color[3]
    };
}

Color withAlpha(const Color& color, float alpha) {
    return {
        color[0],
        color[1],
        color[2],
        std::clamp(alpha, 0.0f, 1.0f)
    };
}

void scaleOuterMargins(RouteShellStyle& style,
                       float sideScale,
                       float backScale,
                       float frontScale) {
    style.sideMargin *= sideScale;
    style.backMargin *= backScale;
    style.frontMargin *= frontScale;
}

std::array<float, 16> mat4ToArray(const glm::mat4& value) {
    std::array<float, 16> out{};
    const float* src = glm::value_ptr(value);
    std::copy(src, src + out.size(), out.begin());
    return out;
}

BackdropPlayableBounds computeBackdropPlayableBounds(
    const ProjectedBackdropArgs& args) {
    const float benchGapWorld = std::max(
        shared_board_grid::defaultVisualTheme().benchGapMin,
        args.worldCellSize * shared_board_grid::defaultVisualTheme().benchGapScale);
    const int benchSlots = std::max(1, args.benchSlots);
    const float benchMinX =
        -0.5f * static_cast<float>(benchSlots) * args.worldCellSize;
    const float benchMaxX =
        benchMinX + static_cast<float>(benchSlots) * args.worldCellSize;
    const float benchMinZ = args.boardMaxZ + benchGapWorld;
    const float benchMaxZ = benchMinZ + args.worldCellSize;

    BackdropPlayableBounds out{};
    out.minX = std::min(args.boardMinX, benchMinX);
    out.maxX = std::max(args.boardMaxX, benchMaxX);
    out.minZ = args.boardMinZ;
    out.maxZ = std::max(args.boardMaxZ, benchMaxZ);
    out.centerX = 0.5f * (out.minX + out.maxX);
    out.centerZ = 0.5f * (out.minZ + out.maxZ);
    out.width = std::max(0.001f, out.maxX - out.minX);
    out.depth = std::max(0.001f, out.maxZ - out.minZ);
    return out;
}

void copyWorldSceneMaterialToBatch(
    const IRenderBackend::WorldSceneMaterial& src,
    shared_world_batches::WorldIndexedBatch& dst) {
    dst.textureKey = src.textureKey;
    dst.textureCacheKey = src.textureCacheKey;
    dst.textureRgba = src.textureRgba;
    dst.textureWidth = src.textureWidth;
    dst.textureHeight = src.textureHeight;
    dst.textureWrapS = src.textureWrapS;
    dst.textureWrapT = src.textureWrapT;

    dst.normalTextureKey = src.normalTextureKey;
    dst.normalTextureCacheKey = src.normalTextureCacheKey;
    dst.normalTextureRgba = src.normalTextureRgba;
    dst.normalTextureWidth = src.normalTextureWidth;
    dst.normalTextureHeight = src.normalTextureHeight;
    dst.normalTextureWrapS = src.normalTextureWrapS;
    dst.normalTextureWrapT = src.normalTextureWrapT;

    dst.metallicRoughnessTextureKey = src.metallicRoughnessTextureKey;
    dst.metallicRoughnessTextureCacheKey = src.metallicRoughnessTextureCacheKey;
    dst.metallicRoughnessTextureRgba = src.metallicRoughnessTextureRgba;
    dst.metallicRoughnessTextureWidth = src.metallicRoughnessTextureWidth;
    dst.metallicRoughnessTextureHeight = src.metallicRoughnessTextureHeight;
    dst.metallicRoughnessTextureWrapS = src.metallicRoughnessTextureWrapS;
    dst.metallicRoughnessTextureWrapT = src.metallicRoughnessTextureWrapT;

    dst.occlusionTextureKey = src.occlusionTextureKey;
    dst.occlusionTextureCacheKey = src.occlusionTextureCacheKey;
    dst.occlusionTextureRgba = src.occlusionTextureRgba;
    dst.occlusionTextureWidth = src.occlusionTextureWidth;
    dst.occlusionTextureHeight = src.occlusionTextureHeight;
    dst.occlusionTextureWrapS = src.occlusionTextureWrapS;
    dst.occlusionTextureWrapT = src.occlusionTextureWrapT;

    dst.emissiveTextureKey = src.emissiveTextureKey;
    dst.emissiveTextureCacheKey = src.emissiveTextureCacheKey;
    dst.emissiveTextureRgba = src.emissiveTextureRgba;
    dst.emissiveTextureWidth = src.emissiveTextureWidth;
    dst.emissiveTextureHeight = src.emissiveTextureHeight;
    dst.emissiveTextureWrapS = src.emissiveTextureWrapS;
    dst.emissiveTextureWrapT = src.emissiveTextureWrapT;

    dst.alphaMode = src.alphaMode;
    dst.blendMode = src.blendMode;
    dst.materialMode = src.materialMode;
    dst.alphaCutoff = src.alphaCutoff;
    dst.normalScale = src.normalScale;
    dst.metallicFactor = src.metallicFactor;
    dst.roughnessFactor = src.roughnessFactor;
    dst.occlusionStrength = src.occlusionStrength;
    dst.emissiveFactorR = src.emissiveFactorR;
    dst.emissiveFactorG = src.emissiveFactorG;
    dst.emissiveFactorB = src.emissiveFactorB;
    dst.characterInkingEnabled = src.characterInkingEnabled;
}

bool appendBackdropModelTransform(
    const ProjectedBackdropArgs& args,
    session_render_scratch::RenderScratch& scratch,
    const char* modelPath,
    const glm::mat4& modelM,
    float sortDepth,
    render_model::MeshData* mesh = nullptr) {
    if (!args.supportsWorldIndexedMeshes || !args.ensureBackendMeshLoaded || !modelPath) {
        return false;
    }

    if (!mesh) {
        mesh = args.ensureBackendMeshLoaded(modelPath);
    }
    if (!mesh || mesh->vertices.empty() || mesh->indices.size() < 3u) {
        return false;
    }

    const std::size_t baseBatchCount =
        std::max<std::size_t>(1u, mesh->submeshBaseTextures.size());
    const std::vector<int> submeshNodeFallback = support::buildSubmeshNodeFallback(*mesh);
    const support::FastTexturedMaterialTemplateCache* materialCache =
        support::ensureFastTexturedMaterialTemplateCache(
            mesh,
            baseBatchCount,
            false,
            args.graphicsQuality);
    const support::FastTexturedMeshTemplateCache* meshCache =
        support::ensureFastTexturedMeshTemplateCache(
            mesh,
            submeshNodeFallback,
            baseBatchCount,
            false);
    if (!materialCache || !meshCache) {
        return false;
    }

    const std::size_t batchCount =
        std::min(materialCache->materials.size(), meshCache->batches.size());
    if (batchCount == 0u) {
        return false;
    }
    const std::array<float, 16> modelMatrix = mat4ToArray(modelM);

    for (std::size_t bi = 0; bi < batchCount; ++bi) {
        const auto& srcMeshBatch = meshCache->batches[bi];
        if (srcMeshBatch.gpuTemplateVertices.empty() || srcMeshBatch.indices.size() < 3u) {
            continue;
        }

        shared_world_batches::WorldIndexedBatch batch{};
        batch.sharedVertices = srcMeshBatch.gpuTemplateVertices.data();
        batch.sharedVertexCount = srcMeshBatch.gpuTemplateVertices.size();
        batch.sharedIndices = srcMeshBatch.indices.data();
        batch.sharedIndexCount = srcMeshBatch.indices.size();
        batch.geometryCacheKey = srcMeshBatch.geometryCacheKey;
        batch.modelMatrix = modelMatrix;
        batch.sortDepth = sortDepth;
        copyWorldSceneMaterialToBatch(materialCache->materials[bi], batch);
        scratch.worldIndexedBatches.push_back(std::move(batch));
    }

    return true;
}

[[maybe_unused]] bool appendBackdropModelPlacement(
    const ProjectedBackdropArgs& args,
    session_render_scratch::RenderScratch& scratch,
    const char* modelPath,
    const BackdropPropPlacement& placement) {
    if (!args.supportsWorldIndexedMeshes || !args.ensureBackendMeshLoaded || !modelPath) {
        return false;
    }

    auto* mesh = args.ensureBackendMeshLoaded(modelPath);
    if (!mesh || mesh->vertices.empty() || mesh->indices.size() < 3u) {
        return false;
    }

    const float uniformScale =
        std::max(0.01f, placement.scale) * std::max(0.01f, mesh->modelScaleFactor);
    const float liftY = -0.04f - mesh->boundsMin.y * uniformScale;
    glm::mat4 modelM(1.0f);
    modelM = glm::translate(modelM, glm::vec3(placement.x, liftY, placement.z));
    modelM = glm::rotate(modelM, glm::radians(placement.yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
    modelM = glm::scale(modelM, glm::vec3(uniformScale));
    const float sortDepth = placement.x * placement.x + placement.z * placement.z;
    return appendBackdropModelTransform(
        args,
        scratch,
        modelPath,
        modelM,
        sortDepth,
        mesh);
}

bool appendRoute1BackdropModel(const ProjectedBackdropArgs& args,
                               session_render_scratch::RenderScratch& scratch) {
    if (args.theme != ArenaBackdropTheme::Route1OpenRoad ||
        !args.enableBackdropTiles ||
        !args.supportsWorldIndexedMeshes ||
        !args.ensureBackendMeshLoaded) {
        return false;
    }

    auto* mesh = args.ensureBackendMeshLoaded(kBackdropRoute1EnvironmentModelPath);
    if (!mesh || mesh->vertices.empty() || mesh->indices.size() < 3u) {
        return false;
    }

    const float safeModelScaleFactor = std::max(0.01f, mesh->modelScaleFactor);
    const float meshWidth = std::max(0.001f, mesh->boundsMax.x - mesh->boundsMin.x);
    const float meshDepth = std::max(0.001f, mesh->boundsMax.z - mesh->boundsMin.z);
    const BackdropPlayableBounds playableBounds = computeBackdropPlayableBounds(args);
    const Route1BackdropTuningState& tuning = args.route1BackdropTuning;
    const float fittedScale =
        std::max(
            playableBounds.width / (meshWidth * safeModelScaleFactor),
            playableBounds.depth / (meshDepth * safeModelScaleFactor)) *
        std::max(0.01f, tuning.scaleMul);
    const float uniformScale = fittedScale * safeModelScaleFactor;
    const glm::vec3 meshCenter = 0.5f * (mesh->boundsMin + mesh->boundsMax);
    const glm::vec3 targetCenter(
        playableBounds.centerX + tuning.offsetXCells * args.worldCellSize,
        shared_board_grid::defaultVisualTheme().boardSurfaceY + tuning.offsetY,
        playableBounds.centerZ + tuning.offsetZCells * args.worldCellSize);

    glm::mat4 modelM(1.0f);
    modelM = glm::translate(modelM, targetCenter);
    modelM = glm::rotate(modelM, glm::radians(tuning.yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
    modelM = glm::scale(modelM, glm::vec3(uniformScale));
    modelM = glm::translate(modelM, -meshCenter);

    const float sortDepth =
        targetCenter.x * targetCenter.x + targetCenter.z * targetCenter.z;
    return appendBackdropModelTransform(
        args,
        scratch,
        kBackdropRoute1EnvironmentModelPath,
        modelM,
        sortDepth,
        mesh);
}

bool routeThemeUsesBoardTileOverlay(ArenaBackdropTheme theme) {
    return theme != ArenaBackdropTheme::Route1OpenRoad;
}

[[maybe_unused]] std::size_t estimatedBackdropTriangleCount(
    const game::runtime::render_model::MeshData& mesh) {
    return mesh.indices.size() / 3u;
}

shared_board_grid::VisualTheme makeBoardTheme(const Color& boardDark,
                                              const Color& boardLight,
                                              const Color& benchDark,
                                              const Color& benchLight,
                                              const Color& grid,
                                              const Color& fallbackBg,
                                              const Color& fallbackDark,
                                              const Color& fallbackLight,
                                              const Color& fallbackGrid) {
    shared_board_grid::VisualTheme theme = shared_board_grid::defaultVisualTheme();
    theme.boardCellDark = boardDark;
    theme.boardCellLight = boardLight;
    theme.benchCellDark = benchDark;
    theme.benchCellLight = benchLight;
    theme.gridLine = grid;
    theme.fallbackBoardBackground = fallbackBg;
    theme.fallbackBoardCellDark = fallbackDark;
    theme.fallbackBoardCellLight = fallbackLight;
    theme.fallbackGridLine = fallbackGrid;
    return theme;
}

const shared_board_grid::VisualTheme& plainBlackBoardTheme() {
    static const shared_board_grid::VisualTheme theme = makeBoardTheme(
        {0.02f, 0.02f, 0.02f, 0.92f},
        {0.05f, 0.05f, 0.05f, 0.92f},
        {0.03f, 0.03f, 0.03f, 0.92f},
        {0.06f, 0.06f, 0.06f, 0.92f},
        {0.16f, 0.16f, 0.16f, 0.96f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.03f, 0.03f, 0.03f, 0.96f},
        {0.06f, 0.06f, 0.06f, 0.96f},
        {0.14f, 0.14f, 0.14f, 0.98f});
    return theme;
}

const RouteShellStyle& routeShellStyle(ArenaBackdropTheme theme) {
    static const RouteShellStyle route1 = [] {
        RouteShellStyle style;
        style.boardTheme = makeBoardTheme(
            {0.08f, 0.12f, 0.09f, 0.0f},
            {0.13f, 0.17f, 0.11f, 0.0f},
            {0.09f, 0.13f, 0.10f, 0.0f},
            {0.14f, 0.18f, 0.12f, 0.0f},
            {0.82f, 0.87f, 0.76f, 0.94f},
            {0.06f, 0.08f, 0.06f, 0.0f},
            {0.10f, 0.15f, 0.10f, 0.0f},
            {0.14f, 0.18f, 0.12f, 0.0f},
            {0.29f, 0.40f, 0.26f, 0.96f});
        style.farFill = {0.29f, 0.43f, 0.21f, 1.0f};
        style.lowerFill = {0.37f, 0.56f, 0.26f, 1.0f};
        style.upperAccent = {0.46f, 0.64f, 0.34f, 0.80f};
        style.plateauTop = {0.24f, 0.39f, 0.19f, 0.98f};
        style.plateauSide = {0.18f, 0.27f, 0.13f, 0.98f};
        style.distantGround = {0.33f, 0.45f, 0.24f, 0.98f};
        style.path = {0.57f, 0.47f, 0.28f, 0.98f};
        style.grass = {0.31f, 0.53f, 0.22f, 0.98f};
        style.fence = {0.54f, 0.39f, 0.23f, 0.98f};
        style.rock = {0.38f, 0.37f, 0.31f, 0.98f};
        style.trunk = {0.36f, 0.23f, 0.12f, 0.98f};
        style.leaves = {0.21f, 0.46f, 0.18f, 0.98f};
        style.shrineStone = {0.55f, 0.57f, 0.50f, 0.98f};
        style.sideMargin = 4.5f;
        style.backMargin = 5.0f;
        style.frontMargin = 2.5f;
        scaleOuterMargins(style, 5.0f, 2.0f, 2.0f);
        return style;
    }();

    static const RouteShellStyle route22 = [] {
        RouteShellStyle style = route1;
        style.boardTheme = makeBoardTheme(
            {0.10f, 0.10f, 0.08f, 0.34f},
            {0.16f, 0.15f, 0.11f, 0.28f},
            {0.11f, 0.11f, 0.09f, 0.30f},
            {0.17f, 0.16f, 0.12f, 0.24f},
            {0.86f, 0.82f, 0.71f, 0.94f},
            {0.08f, 0.08f, 0.07f, 0.92f},
            {0.13f, 0.12f, 0.10f, 0.34f},
            {0.19f, 0.18f, 0.14f, 0.26f},
            {0.42f, 0.36f, 0.24f, 0.96f});
        style.farFill = {0.38f, 0.34f, 0.24f, 1.0f};
        style.lowerFill = {0.45f, 0.40f, 0.28f, 1.0f};
        style.upperAccent = {0.55f, 0.46f, 0.31f, 0.72f};
        style.plateauTop = {0.36f, 0.35f, 0.23f, 0.98f};
        style.plateauSide = {0.24f, 0.22f, 0.15f, 0.98f};
        style.distantGround = {0.42f, 0.39f, 0.28f, 0.98f};
        style.path = {0.58f, 0.46f, 0.29f, 0.98f};
        style.grass = {0.32f, 0.39f, 0.20f, 0.98f};
        style.rock = {0.45f, 0.42f, 0.38f, 0.98f};
        style.leaves = {0.28f, 0.35f, 0.20f, 0.98f};
        style.sideMargin = 4.75f;
        style.backMargin = 4.75f;
        scaleOuterMargins(style, 5.0f, 2.0f, 2.0f);
        return style;
    }();

    static const RouteShellStyle route2 = [] {
        RouteShellStyle style = route1;
        style.boardTheme = makeBoardTheme(
            {0.07f, 0.11f, 0.09f, 0.34f},
            {0.11f, 0.15f, 0.11f, 0.28f},
            {0.08f, 0.12f, 0.10f, 0.30f},
            {0.12f, 0.16f, 0.12f, 0.24f},
            {0.79f, 0.88f, 0.80f, 0.94f},
            {0.05f, 0.08f, 0.07f, 0.92f},
            {0.09f, 0.13f, 0.11f, 0.34f},
            {0.12f, 0.17f, 0.13f, 0.26f},
            {0.25f, 0.42f, 0.34f, 0.96f});
        style.farFill = {0.23f, 0.35f, 0.20f, 1.0f};
        style.lowerFill = {0.30f, 0.48f, 0.25f, 1.0f};
        style.upperAccent = {0.18f, 0.36f, 0.23f, 0.80f};
        style.plateauTop = {0.20f, 0.34f, 0.18f, 0.98f};
        style.plateauSide = {0.15f, 0.23f, 0.13f, 0.98f};
        style.distantGround = {0.28f, 0.42f, 0.23f, 0.98f};
        style.path = {0.42f, 0.34f, 0.21f, 0.98f};
        style.grass = {0.28f, 0.49f, 0.24f, 0.98f};
        style.leaves = {0.16f, 0.38f, 0.19f, 0.98f};
        style.sideMargin = 4.25f;
        style.backMargin = 4.5f;
        scaleOuterMargins(style, 5.0f, 2.0f, 2.0f);
        return style;
    }();

    static const RouteShellStyle viridian = [] {
        RouteShellStyle style = route2;
        style.boardTheme = makeBoardTheme(
            {0.06f, 0.09f, 0.08f, 0.34f},
            {0.09f, 0.12f, 0.09f, 0.28f},
            {0.07f, 0.10f, 0.09f, 0.30f},
            {0.10f, 0.13f, 0.11f, 0.24f},
            {0.77f, 0.86f, 0.80f, 0.94f},
            {0.04f, 0.06f, 0.05f, 0.92f},
            {0.07f, 0.11f, 0.09f, 0.34f},
            {0.10f, 0.14f, 0.11f, 0.26f},
            {0.23f, 0.39f, 0.32f, 0.96f});
        style.farFill = {0.11f, 0.18f, 0.11f, 1.0f};
        style.lowerFill = {0.18f, 0.28f, 0.17f, 1.0f};
        style.upperAccent = {0.10f, 0.20f, 0.12f, 0.88f};
        style.plateauTop = {0.15f, 0.24f, 0.14f, 0.98f};
        style.plateauSide = {0.10f, 0.17f, 0.11f, 0.98f};
        style.distantGround = {0.19f, 0.29f, 0.17f, 0.98f};
        style.path = {0.35f, 0.29f, 0.20f, 0.98f};
        style.grass = {0.18f, 0.34f, 0.18f, 0.98f};
        style.rock = {0.32f, 0.36f, 0.32f, 0.98f};
        style.trunk = {0.29f, 0.19f, 0.12f, 0.98f};
        style.leaves = {0.12f, 0.27f, 0.14f, 0.98f};
        style.shrineStone = {0.47f, 0.51f, 0.46f, 0.98f};
        style.sideMargin = 3.75f;
        style.backMargin = 4.0f;
        scaleOuterMargins(style, 5.0f, 2.0f, 2.0f);
        return style;
    }();

    static const RouteShellStyle route3 = [] {
        RouteShellStyle style = route22;
        style.boardTheme = makeBoardTheme(
            {0.10f, 0.10f, 0.10f, 0.34f},
            {0.15f, 0.14f, 0.13f, 0.28f},
            {0.11f, 0.11f, 0.11f, 0.30f},
            {0.16f, 0.15f, 0.14f, 0.24f},
            {0.86f, 0.83f, 0.80f, 0.94f},
            {0.08f, 0.08f, 0.08f, 0.92f},
            {0.13f, 0.12f, 0.12f, 0.34f},
            {0.18f, 0.17f, 0.16f, 0.26f},
            {0.38f, 0.34f, 0.31f, 0.96f});
        style.farFill = {0.31f, 0.28f, 0.24f, 1.0f};
        style.lowerFill = {0.39f, 0.34f, 0.28f, 1.0f};
        style.upperAccent = {0.26f, 0.29f, 0.20f, 0.70f};
        style.plateauTop = {0.36f, 0.33f, 0.27f, 0.98f};
        style.plateauSide = {0.23f, 0.20f, 0.17f, 0.98f};
        style.distantGround = {0.43f, 0.38f, 0.31f, 0.98f};
        style.path = {0.52f, 0.43f, 0.31f, 0.98f};
        style.grass = {0.24f, 0.32f, 0.18f, 0.98f};
        style.rock = {0.48f, 0.45f, 0.43f, 0.98f};
        style.leaves = {0.23f, 0.31f, 0.18f, 0.98f};
        style.sideMargin = 4.5f;
        style.backMargin = 4.75f;
        scaleOuterMargins(style, 5.0f, 2.0f, 2.0f);
        return style;
    }();

    switch (theme) {
        case ArenaBackdropTheme::Route22Foothills: return route22;
        case ArenaBackdropTheme::Route2ForestEdge: return route2;
        case ArenaBackdropTheme::ViridianForestShrine: return viridian;
        case ArenaBackdropTheme::Route3MountainPass: return route3;
        case ArenaBackdropTheme::Route1OpenRoad:
        case ArenaBackdropTheme::Default:
        default:
            return route1;
    }
}

void appendWorldQuadDoubleSided(std::vector<IRenderBackend::WorldTriangle>& out,
                                const glm::vec3& a,
                                const glm::vec3& b,
                                const glm::vec3& c,
                                const glm::vec3& d,
                                const Color& color) {
    auto pushTriangle = [&](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2) {
        IRenderBackend::WorldTriangle tri{};
        tri.x1 = p0.x; tri.y1 = p0.y; tri.z1 = p0.z;
        tri.x2 = p1.x; tri.y2 = p1.y; tri.z2 = p1.z;
        tri.x3 = p2.x; tri.y3 = p2.y; tri.z3 = p2.z;
        tri.r = color[0];
        tri.g = color[1];
        tri.b = color[2];
        tri.a = color[3];
        out.push_back(tri);
    };

    pushTriangle(a, b, c);
    pushTriangle(a, c, d);
    pushTriangle(d, c, b);
    pushTriangle(d, b, a);
}

void appendBox(std::vector<IRenderBackend::WorldTriangle>& out,
               float minX,
               float minY,
               float minZ,
               float maxX,
               float maxY,
               float maxZ,
               const Color& topColor,
               const Color& sideColor) {
    if (maxX <= minX || maxY <= minY || maxZ <= minZ) return;

    const glm::vec3 p000(minX, minY, minZ);
    const glm::vec3 p100(maxX, minY, minZ);
    const glm::vec3 p110(maxX, minY, maxZ);
    const glm::vec3 p010(minX, minY, maxZ);
    const glm::vec3 p001(minX, maxY, minZ);
    const glm::vec3 p101(maxX, maxY, minZ);
    const glm::vec3 p111(maxX, maxY, maxZ);
    const glm::vec3 p011(minX, maxY, maxZ);

    appendWorldQuadDoubleSided(out, p001, p101, p111, p011, topColor);
    appendWorldQuadDoubleSided(out, p000, p100, p101, p001, sideColor);
    appendWorldQuadDoubleSided(out, p100, p110, p111, p101, sideColor);
    appendWorldQuadDoubleSided(out, p110, p010, p011, p111, sideColor);
    appendWorldQuadDoubleSided(out, p010, p000, p001, p011, sideColor);
}

[[maybe_unused]] void appendTree(std::vector<IRenderBackend::WorldTriangle>& out,
                float x,
                float z,
                float trunkHeight,
                float trunkRadius,
                float canopyWidth,
                float canopyHeight,
                const Color& trunkColor,
                const Color& leafColor) {
    appendBox(
        out,
        x - trunkRadius,
        -0.04f,
        z - trunkRadius,
        x + trunkRadius,
        -0.04f + trunkHeight,
        z + trunkRadius,
        scaleColor(trunkColor, 1.08f),
        scaleColor(trunkColor, 0.72f));

    appendBox(
        out,
        x - canopyWidth,
        -0.04f + trunkHeight - canopyHeight * 0.25f,
        z - canopyWidth,
        x + canopyWidth,
        -0.04f + trunkHeight + canopyHeight * 0.45f,
        z + canopyWidth,
        scaleColor(leafColor, 1.05f),
        scaleColor(leafColor, 0.78f));

    appendBox(
        out,
        x - canopyWidth * 0.72f,
        -0.04f + trunkHeight + canopyHeight * 0.15f,
        z - canopyWidth * 0.72f,
        x + canopyWidth * 0.72f,
        -0.04f + trunkHeight + canopyHeight,
        z + canopyWidth * 0.72f,
        scaleColor(leafColor, 1.12f),
        scaleColor(leafColor, 0.84f));
}

[[maybe_unused]] void appendFenceLine(std::vector<IRenderBackend::WorldTriangle>& out,
                     float minX,
                     float maxX,
                     float z,
                     int postCount,
                     float postHeight,
                     const Color& color) {
    if (postCount < 2) return;
    const float span = maxX - minX;
    const float postRadius = 0.06f;
    for (int i = 0; i < postCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(postCount - 1);
        const float x = minX + span * t;
        appendBox(
            out,
            x - postRadius,
            -0.04f,
            z - postRadius,
            x + postRadius,
            -0.04f + postHeight,
            z + postRadius,
            scaleColor(color, 1.08f),
            scaleColor(color, 0.70f));
    }
}

void appendGrassPatch(std::vector<IRenderBackend::WorldTriangle>& out,
                      float minX,
                      float maxX,
                      float minZ,
                      float maxZ,
                      const Color& color) {
    appendBox(
        out,
        minX,
        -0.038f,
        minZ,
        maxX,
        -0.010f,
        maxZ,
        scaleColor(color, 1.05f),
        scaleColor(color, 0.72f));
}

[[maybe_unused]] void appendStoneMarker(std::vector<IRenderBackend::WorldTriangle>& out,
                       float x,
                       float z,
                       float width,
                       float height,
                       const Color& color) {
    appendBox(
        out,
        x - width * 0.5f,
        -0.04f,
        z - width * 0.35f,
        x + width * 0.5f,
        -0.04f + height,
        z + width * 0.35f,
        scaleColor(color, 1.08f),
        scaleColor(color, 0.72f));
}

bool themeUsesTexturedGrassGround(ArenaBackdropTheme theme) {
    switch (theme) {
        case ArenaBackdropTheme::Route1OpenRoad:
        case ArenaBackdropTheme::Route22Foothills:
        case ArenaBackdropTheme::Route2ForestEdge:
        case ArenaBackdropTheme::ViridianForestShrine:
        case ArenaBackdropTheme::Route3MountainPass:
            return true;
        case ArenaBackdropTheme::Default:
        default:
            return false;
    }
}

bool themeUsesTexturedBoardDirt(ArenaBackdropTheme theme) {
    switch (theme) {
        case ArenaBackdropTheme::Route1OpenRoad:
        case ArenaBackdropTheme::Route22Foothills:
        case ArenaBackdropTheme::Route2ForestEdge:
        case ArenaBackdropTheme::ViridianForestShrine:
        case ArenaBackdropTheme::Route3MountainPass:
            return true;
        case ArenaBackdropTheme::Default:
        default:
            return false;
    }
}

std::string makeGroundPatchGeometryKey(const char* label,
                                       ArenaBackdropTheme theme,
                                       float minX,
                                       float maxX,
                                       float minZ,
                                       float maxZ) {
    std::ostringstream out;
    out << "session_world_backdrop_grass_" << (label ? label : "patch")
        << "_theme_" << static_cast<int>(theme)
        << "_" << minX << "_" << maxX
        << "_" << minZ << "_" << maxZ;
    return out.str();
}

std::string makeBoardTilesGeometryKey(const ProjectedBackdropArgs& args) {
    std::ostringstream out;
    out << "session_world_backdrop_board_tiles_theme_"
        << static_cast<int>(args.theme)
        << "_rows_" << args.rows
        << "_cols_" << args.cols
        << "_cell_" << args.worldCellSize
        << "_minx_" << args.boardMinX
        << "_minz_" << args.boardMinZ
        << "_maxx_" << args.boardMaxX
        << "_maxz_" << args.boardMaxZ;
    return out.str();
}

std::string makeBenchTilesGeometryKey(const ProjectedBackdropArgs& args,
                                      float benchMinX,
                                      float benchMinZ,
                                      float benchMaxX,
                                      float benchMaxZ) {
    std::ostringstream out;
    out << "session_world_backdrop_bench_tiles_theme_"
        << static_cast<int>(args.theme)
        << "_benchslots_" << args.benchSlots
        << "_cell_" << args.worldCellSize
        << "_minx_" << benchMinX
        << "_minz_" << benchMinZ
        << "_maxx_" << benchMaxX
        << "_maxz_" << benchMaxZ;
    return out.str();
}

std::string makeRoute1PatternGeometryKey(const ProjectedBackdropArgs& args,
                                         const char* label,
                                         int rows,
                                         int cols,
                                         float minX,
                                         float minZ,
                                         float maxX,
                                         float maxZ) {
    std::ostringstream out;
    out << "session_world_backdrop_route1_pattern_"
        << (label ? label : "tiles")
        << "_theme_" << static_cast<int>(args.theme)
        << "_rows_" << rows
        << "_cols_" << cols
        << "_cell_" << args.worldCellSize
        << "_minx_" << minX
        << "_minz_" << minZ
        << "_maxx_" << maxX
        << "_maxz_" << maxZ;
    return out.str();
}

std::string makeBoardLedgeGeometryKey(const ProjectedBackdropArgs& args) {
    std::ostringstream out;
    out << "session_world_backdrop_board_ledge_theme_"
        << static_cast<int>(args.theme)
        << "_rows_" << args.rows
        << "_cols_" << args.cols
        << "_cell_" << args.worldCellSize
        << "_minx_" << args.boardMinX
        << "_minz_" << args.boardMinZ
        << "_maxx_" << args.boardMaxX
        << "_maxz_" << args.boardMaxZ;
    return out.str();
}

std::string makeBenchLedgeGeometryKey(const ProjectedBackdropArgs& args,
                                      float benchMinX,
                                      float benchMaxX,
                                      float benchMinZ,
                                      float benchMaxZ) {
    std::ostringstream out;
    out << "session_world_backdrop_bench_ledge_theme_"
        << static_cast<int>(args.theme)
        << "_benchslots_" << args.benchSlots
        << "_cell_" << args.worldCellSize
        << "_minx_" << benchMinX
        << "_minz_" << benchMinZ
        << "_maxx_" << benchMaxX
        << "_maxz_" << benchMaxZ;
    return out.str();
}

UvRect atlasTileUvRect(int atlasTileX,
                       int atlasTileY,
                       int textureWidth,
                       int textureHeight) {
    const float insetPx = 0.5f;
    const float x0 = static_cast<float>(
        kBackdropBoardAtlasOriginXPx + atlasTileX * kBackdropBoardAtlasTileSizePx);
    const float y0 = static_cast<float>(
        kBackdropBoardAtlasOriginYPx + atlasTileY * kBackdropBoardAtlasTileSizePx);
    const float x1 = x0 + static_cast<float>(kBackdropBoardAtlasTileSizePx);
    const float y1 = y0 + static_cast<float>(kBackdropBoardAtlasTileSizePx);
    const float texW = static_cast<float>(std::max(1, textureWidth));
    const float texH = static_cast<float>(std::max(1, textureHeight));

    return {
        (x0 + insetPx) / texW,
        (y0 + insetPx) / texH,
        (x1 - insetPx) / texW,
        (y1 - insetPx) / texH,
    };
}

UvRect atlasTileRectUvRect(int atlasTileX,
                           int atlasTileY,
                           int atlasTileW,
                           int atlasTileH,
                           int textureWidth,
                           int textureHeight) {
    const float insetPx = 0.5f;
    const float x0 = static_cast<float>(
        kBackdropBoardAtlasOriginXPx + atlasTileX * kBackdropBoardAtlasTileSizePx);
    const float y0 = static_cast<float>(
        kBackdropBoardAtlasOriginYPx + atlasTileY * kBackdropBoardAtlasTileSizePx);
    const float x1 =
        x0 + static_cast<float>(atlasTileW * kBackdropBoardAtlasTileSizePx);
    const float y1 =
        y0 + static_cast<float>(atlasTileH * kBackdropBoardAtlasTileSizePx);
    const float texW = static_cast<float>(std::max(1, textureWidth));
    const float texH = static_cast<float>(std::max(1, textureHeight));

    return {
        (x0 + insetPx) / texW,
        (y0 + insetPx) / texH,
        (x1 - insetPx) / texW,
        (y1 - insetPx) / texH,
    };
}

[[maybe_unused]] UvRect opaqueTextureUvRect(const game::runtime::SharedBackendTextureCacheEntry& tex) {
    if (tex.width <= 0 || tex.height <= 0 || tex.rgba.size() < 4u) {
        return {};
    }

    int minX = tex.width;
    int minY = tex.height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < tex.height; ++y) {
        for (int x = 0; x < tex.width; ++x) {
            const std::size_t pixelIndex =
                static_cast<std::size_t>((y * tex.width + x) * 4 + 3);
            if (pixelIndex >= tex.rgba.size()) {
                continue;
            }
            if (tex.rgba[pixelIndex] <= 16u) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY) {
        return {};
    }

    const float insetPx = 0.5f;
    const float texW = static_cast<float>(std::max(1, tex.width));
    const float texH = static_cast<float>(std::max(1, tex.height));
    return {
        (static_cast<float>(minX) + insetPx) / texW,
        (static_cast<float>(minY) + insetPx) / texH,
        (static_cast<float>(maxX + 1) - insetPx) / texW,
        (static_cast<float>(maxY + 1) - insetPx) / texH,
    };
}

AtlasTileCoord boardAtlasTileForCell(int row, int col, int rows, int cols) {
    const bool top = row == 0;
    const bool bottom = row == rows - 1;
    const bool left = col == 0;
    const bool right = col == cols - 1;

    if (top && left) return {0, 0};
    if (top && right) return {3, 0};
    if (bottom && left) return {0, 3};
    if (bottom && right) return {3, 3};

    if (top) return {1 + (col % 2), 0};
    if (bottom) return {1 + (col % 2), 3};
    if (left) return {0, 1 + (row % 2)};
    if (right) return {3, 1 + (row % 2)};

    return {1 + (col % 2), 1 + (row % 2)};
}

template <typename TileSelector>
bool appendTexturedTileBatch(const ProjectedBackdropArgs& args,
                             const char* geometryKey,
                             int rows,
                             int cols,
                             float minX,
                             float minZ,
                             float tileY,
                             TileSelector&& selectTile,
                             session_render_scratch::RenderScratch& scratch) {
    if (!args.supportsWorldIndexedMeshes || !args.ensureBackendTextureLoaded ||
        rows <= 0 || cols <= 0 || args.worldCellSize <= 0.0f || !geometryKey ||
        !themeUsesTexturedBoardDirt(args.theme)) {
        return false;
    }

    game::runtime::SharedBackendTextureCacheEntry* tex =
        args.ensureBackendTextureLoaded(kBackdropBoardDirtTexturePath, false);
    if (!tex || !tex->valid || tex->width <= 0 || tex->height <= 0 ||
        tex->rgba.empty()) {
        return false;
    }

    shared_world_batches::WorldIndexedBatch batch{};
    batch.geometryCacheKey = geometryKey;
    batch.textureKey = kBackdropBoardDirtTexturePath;
    batch.textureCacheKey = kBackdropBoardDirtTexturePath;
    batch.textureRgba = tex->rgba.data();
    batch.textureWidth = tex->width;
    batch.textureHeight = tex->height;
    batch.textureWrapS = 33071;
    batch.textureWrapT = 33071;
    // Keep board tiles on the plain textured path so route lighting and model
    // shading do not push their colors around.
    batch.alphaMode = 0u;
    batch.blendMode = 0u;
    batch.materialMode = 0u;
    batch.characterInkingEnabled = 0u;
    batch.metallicFactor = 0.0f;
    batch.roughnessFactor = 1.0f;
    batch.vertexColorMulR = 1.0f;
    batch.vertexColorMulG = 1.0f;
    batch.vertexColorMulB = 1.0f;
    batch.vertexColorMulA = 1.0f;
    batch.vertices.reserve(static_cast<std::size_t>(rows * cols * 4));
    batch.indices.reserve(static_cast<std::size_t>(rows * cols * 6));

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const float x0 =
                minX + static_cast<float>(col) * args.worldCellSize;
            const float z0 =
                minZ + static_cast<float>(row) * args.worldCellSize;
            const float x1 = x0 + args.worldCellSize;
            const float z1 = z0 + args.worldCellSize;
            const AtlasTileCoord atlasTile = selectTile(row, col, rows, cols);
            const UvRect uv = atlasTileUvRect(
                atlasTile.x, atlasTile.y, tex->width, tex->height);
            const std::uint32_t baseIndex =
                static_cast<std::uint32_t>(batch.vertices.size());

            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = x0, .y = tileY, .z = z0,
                .u = uv.u0, .v = uv.v0,
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
                .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
            });
            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = x1, .y = tileY, .z = z0,
                .u = uv.u1, .v = uv.v0,
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
                .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
            });
            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = x1, .y = tileY, .z = z1,
                .u = uv.u1, .v = uv.v1,
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
                .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
            });
            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = x0, .y = tileY, .z = z1,
                .u = uv.u0, .v = uv.v1,
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
                .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
            });

            batch.indices.push_back(baseIndex + 0u);
            batch.indices.push_back(baseIndex + 1u);
            batch.indices.push_back(baseIndex + 2u);
            batch.indices.push_back(baseIndex + 0u);
            batch.indices.push_back(baseIndex + 2u);
            batch.indices.push_back(baseIndex + 3u);
        }
    }

    scratch.worldIndexedBatches.push_back(std::move(batch));
    return true;
}

bool appendTexturedBoardTiles(const ProjectedBackdropArgs& args,
                              session_render_scratch::RenderScratch& scratch) {
    const shared_board_grid::VisualTheme& boardTheme =
        routeShellStyle(args.theme).boardTheme;
    const float tileY = std::min(
        boardTheme.gridY - 0.0004f,
        boardTheme.boardSurfaceY + 0.0009f);

    const std::string geometryKey = makeBoardTilesGeometryKey(args);
    return appendTexturedTileBatch(
        args,
        geometryKey.c_str(),
        args.rows,
        args.cols,
        args.boardMinX,
        args.boardMinZ,
        tileY,
        [](int row, int col, int rows, int cols) {
            return boardAtlasTileForCell(row, col, rows, cols);
        },
        scratch);
}

bool appendTexturedBenchTiles(const ProjectedBackdropArgs& args,
                              session_render_scratch::RenderScratch& scratch) {
    if (args.benchSlots <= 0) {
        return false;
    }

    if (!args.supportsWorldIndexedMeshes || !args.ensureBackendTextureLoaded ||
        args.worldCellSize <= 0.0f || !themeUsesTexturedBoardDirt(args.theme)) {
        return false;
    }

    game::runtime::SharedBackendTextureCacheEntry* tex =
        args.ensureBackendTextureLoaded(kBackdropGrassTexturePath, false);
    if (!tex || !tex->valid || tex->width <= 0 || tex->height <= 0 ||
        tex->rgba.empty()) {
        return false;
    }

    const shared_board_grid::VisualTheme& boardTheme =
        routeShellStyle(args.theme).boardTheme;
    const float tileY = std::min(
        boardTheme.gridY - 0.0004f,
        boardTheme.boardSurfaceY + 0.0009f);
    const float benchGapWorld = std::max(
        shared_board_grid::defaultVisualTheme().benchGapMin,
        args.worldCellSize * shared_board_grid::defaultVisualTheme().benchGapScale);
    const float benchMinX =
        -0.5f * static_cast<float>(args.benchSlots) * args.worldCellSize;
    const float benchMaxX =
        benchMinX + static_cast<float>(args.benchSlots) * args.worldCellSize;
    const float benchMinZ = args.boardMaxZ + benchGapWorld;
    const float benchMaxZ = benchMinZ + args.worldCellSize;
    const std::string geometryKey =
        makeBenchTilesGeometryKey(args, benchMinX, benchMinZ, benchMaxX, benchMaxZ);
    shared_world_batches::WorldIndexedBatch batch{};
    batch.geometryCacheKey = geometryKey;
    batch.textureKey = kBackdropGrassTexturePath;
    batch.textureCacheKey = kBackdropGrassTexturePath;
    batch.textureRgba = tex->rgba.data();
    batch.textureWidth = tex->width;
    batch.textureHeight = tex->height;
    batch.textureWrapS = 33071;
    batch.textureWrapT = 33071;
    batch.alphaMode = 0u;
    batch.blendMode = 0u;
    batch.materialMode = 0u;
    batch.characterInkingEnabled = 0u;
    batch.metallicFactor = 0.0f;
    batch.roughnessFactor = 1.0f;
    batch.vertexColorMulR = 1.0f;
    batch.vertexColorMulG = 1.0f;
    batch.vertexColorMulB = 1.0f;
    batch.vertexColorMulA = 1.0f;
    batch.vertices.reserve(static_cast<std::size_t>(args.benchSlots * 4));
    batch.indices.reserve(static_cast<std::size_t>(args.benchSlots * 6));

    for (int slot = 0; slot < args.benchSlots; ++slot) {
        const float x0 = benchMinX + static_cast<float>(slot) * args.worldCellSize;
        const float z0 = benchMinZ;
        const float x1 = x0 + args.worldCellSize;
        const float z1 = benchMaxZ;
        const std::uint32_t baseIndex =
            static_cast<std::uint32_t>(batch.vertices.size());

        batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
            .x = x0, .y = tileY, .z = z0,
            .u = 0.0f, .v = 0.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
            .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
        });
        batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
            .x = x1, .y = tileY, .z = z0,
            .u = 1.0f, .v = 0.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
            .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
        });
        batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
            .x = x1, .y = tileY, .z = z1,
            .u = 1.0f, .v = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
            .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
        });
        batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
            .x = x0, .y = tileY, .z = z1,
            .u = 0.0f, .v = 1.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
            .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
        });

        batch.indices.push_back(baseIndex + 0u);
        batch.indices.push_back(baseIndex + 1u);
        batch.indices.push_back(baseIndex + 2u);
        batch.indices.push_back(baseIndex + 0u);
        batch.indices.push_back(baseIndex + 2u);
        batch.indices.push_back(baseIndex + 3u);
    }

    scratch.worldIndexedBatches.push_back(std::move(batch));
    return true;
}

template <typename ColorSelector>
bool appendRoute1PatternBatch(const ProjectedBackdropArgs& args,
                              const char* label,
                              int rows,
                              int cols,
                              float minX,
                              float minZ,
                              float tileY,
                              float sortDepth,
                              ColorSelector&& selectColor,
                              session_render_scratch::RenderScratch& scratch) {
    if (args.theme != ArenaBackdropTheme::Route1OpenRoad ||
        !args.supportsWorldIndexedMeshes || rows <= 0 || cols <= 0 ||
        args.worldCellSize <= 0.0f || !label) {
        return false;
    }

    const float maxX = minX + static_cast<float>(cols) * args.worldCellSize;
    const float maxZ = minZ + static_cast<float>(rows) * args.worldCellSize;
    shared_world_batches::WorldIndexedBatch batch{};
    batch.geometryCacheKey = makeRoute1PatternGeometryKey(
        args,
        label,
        rows,
        cols,
        minX,
        minZ,
        maxX,
        maxZ);
    batch.textureKey = kBackdropRoute1PatternTextureKey;
    batch.textureCacheKey = kBackdropRoute1PatternTextureKey;
    batch.ownedTextureRgba = {255u, 255u, 255u, 255u};
    batch.textureRgba = batch.ownedTextureRgba.data();
    batch.textureWidth = 1;
    batch.textureHeight = 1;
    batch.textureWrapS = 33071;
    batch.textureWrapT = 33071;
    batch.alphaMode = 2u;
    batch.blendMode = 0u;
    batch.materialMode = 0u;
    batch.characterInkingEnabled = 0u;
    batch.metallicFactor = 0.0f;
    batch.roughnessFactor = 1.0f;
    batch.vertexColorMulR = 1.0f;
    batch.vertexColorMulG = 1.0f;
    batch.vertexColorMulB = 1.0f;
    batch.vertexColorMulA = 1.0f;
    batch.sortDepth = sortDepth;
    batch.vertices.reserve(static_cast<std::size_t>(rows * cols * 4));
    batch.indices.reserve(static_cast<std::size_t>(rows * cols * 6));

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const float x0 =
                minX + static_cast<float>(col) * args.worldCellSize;
            const float z0 =
                minZ + static_cast<float>(row) * args.worldCellSize;
            const float x1 = x0 + args.worldCellSize;
            const float z1 = z0 + args.worldCellSize;
            const Color color = selectColor(row, col, rows, cols);
            const std::uint32_t baseIndex =
                static_cast<std::uint32_t>(batch.vertices.size());

            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = x0, .y = tileY, .z = z0,
                .u = 0.0f, .v = 0.0f,
                .r = color[0], .g = color[1], .b = color[2], .a = color[3],
                .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
                .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
            });
            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = x1, .y = tileY, .z = z0,
                .u = 1.0f, .v = 0.0f,
                .r = color[0], .g = color[1], .b = color[2], .a = color[3],
                .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
                .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
            });
            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = x1, .y = tileY, .z = z1,
                .u = 1.0f, .v = 1.0f,
                .r = color[0], .g = color[1], .b = color[2], .a = color[3],
                .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
                .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
            });
            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                .x = x0, .y = tileY, .z = z1,
                .u = 0.0f, .v = 1.0f,
                .r = color[0], .g = color[1], .b = color[2], .a = color[3],
                .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
                .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
            });

            batch.indices.push_back(baseIndex + 0u);
            batch.indices.push_back(baseIndex + 1u);
            batch.indices.push_back(baseIndex + 2u);
            batch.indices.push_back(baseIndex + 0u);
            batch.indices.push_back(baseIndex + 2u);
            batch.indices.push_back(baseIndex + 3u);
        }
    }

    scratch.worldIndexedBatches.push_back(std::move(batch));
    return true;
}

bool appendRoute1PatternOverlay(const ProjectedBackdropArgs& args,
                                session_render_scratch::RenderScratch& scratch) {
    if (args.theme != ArenaBackdropTheme::Route1OpenRoad ||
        !args.enableBackdropTiles || !args.supportsWorldIndexedMeshes) {
        return false;
    }

    const shared_board_grid::VisualTheme& boardTheme =
        routeShellStyle(args.theme).boardTheme;
    const float gridGap = std::max(0.0012f, boardTheme.gridY - boardTheme.boardSurfaceY);
    const float tileY = std::clamp(
        boardTheme.boardSurfaceY + gridGap * 0.45f,
        boardTheme.boardSurfaceY + 0.0006f,
        boardTheme.gridY - 0.0006f);
    const BackdropPlayableBounds playableBounds = computeBackdropPlayableBounds(args);
    const float sortDepth =
        playableBounds.centerX * playableBounds.centerX +
        playableBounds.centerZ * playableBounds.centerZ;

    const Color boardDark = withAlpha(boardTheme.boardCellDark, 0.24f);
    const Color boardLight = withAlpha(boardTheme.boardCellLight, 0.14f);
    const Color benchDark = withAlpha(boardTheme.benchCellDark, 0.22f);
    const Color benchLight = withAlpha(boardTheme.benchCellLight, 0.13f);

    bool appended = appendRoute1PatternBatch(
        args,
        "board",
        args.rows,
        args.cols,
        args.boardMinX,
        args.boardMinZ,
        tileY,
        sortDepth,
        [&](int row, int col, int, int) {
            return ((row + col) % 2 == 0) ? boardDark : boardLight;
        },
        scratch);

    if (args.benchSlots > 0) {
        const float benchGapWorld = std::max(
            shared_board_grid::defaultVisualTheme().benchGapMin,
            args.worldCellSize * shared_board_grid::defaultVisualTheme().benchGapScale);
        const float benchMinX =
            -0.5f * static_cast<float>(args.benchSlots) * args.worldCellSize;
        const float benchMinZ = args.boardMaxZ + benchGapWorld;
        appended = appendRoute1PatternBatch(
            args,
            "bench",
            1,
            args.benchSlots,
            benchMinX,
            benchMinZ,
            tileY,
            sortDepth,
            [&](int row, int col, int, int) {
                return ((row + col) % 2 == 0) ? benchDark : benchLight;
            },
            scratch) || appended;
    }

    return appended;
}

bool appendTexturedBoardLedgeWalls(const ProjectedBackdropArgs& args,
                                   session_render_scratch::RenderScratch& scratch) {
    if (!args.supportsWorldIndexedMeshes || !args.ensureBackendTextureLoaded ||
        args.boardMaxX <= args.boardMinX || args.boardMaxZ <= args.boardMinZ ||
        !themeUsesTexturedBoardDirt(args.theme)) {
        return false;
    }

    game::runtime::SharedBackendTextureCacheEntry* tex =
        args.ensureBackendTextureLoaded(kBackdropBoardLedgeTexturePath, false);
    if (!tex || !tex->valid || tex->width <= 0 || tex->height <= 0 ||
        tex->rgba.empty()) {
        return false;
    }

    const shared_board_grid::VisualTheme& boardTheme =
        routeShellStyle(args.theme).boardTheme;
    const float topY = boardTheme.boardSurfaceY + 0.0015f;
    const UvRect leftUv = atlasTileRectUvRect(0, 0, 1, 3, tex->width, tex->height);
    const UvRect midUvA = atlasTileRectUvRect(1, 0, 1, 3, tex->width, tex->height);
    const UvRect midUvB = atlasTileRectUvRect(2, 0, 1, 3, tex->width, tex->height);
    const UvRect rightUv = atlasTileRectUvRect(3, 0, 1, 3, tex->width, tex->height);

    const auto appendLedgeRing =
        [&](const std::string& geometryKey,
            float minX,
            float maxX,
            float minZ,
            float maxZ,
            float bottomY,
            float wallOffset,
            int horizontalSegments,
            int verticalSegments) {
            if (bottomY >= topY) {
                return;
            }

            shared_world_batches::WorldIndexedBatch batch{};
            batch.geometryCacheKey = geometryKey;
            batch.textureKey = kBackdropBoardLedgeTexturePath;
            batch.textureCacheKey = kBackdropBoardLedgeTexturePath;
            batch.textureRgba = tex->rgba.data();
            batch.textureWidth = tex->width;
            batch.textureHeight = tex->height;
            batch.textureWrapS = 33071;
            batch.textureWrapT = 33071;
            batch.alphaMode = 1u;
            batch.blendMode = 0u;
            batch.materialMode = 0u;
            batch.alphaCutoff = 0.04f;
            batch.characterInkingEnabled = 0u;
            batch.metallicFactor = 0.0f;
            batch.roughnessFactor = 1.0f;
            batch.vertexColorMulR = 1.0f;
            batch.vertexColorMulG = 1.0f;
            batch.vertexColorMulB = 1.0f;
            batch.vertexColorMulA = 1.0f;
            batch.vertices.reserve(192u);
            batch.indices.reserve(288u);

            const auto appendWallSegment = [&](float x0,
                                               float z0,
                                               float x1,
                                               float z1,
                                               const UvRect& uv) {
                const std::uint32_t baseIndex =
                    static_cast<std::uint32_t>(batch.vertices.size());
                batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                    .x = x0, .y = bottomY, .z = z0,
                    .u = uv.u0, .v = uv.v1,
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    .nx = 0.0f, .ny = 0.0f, .nz = 1.0f,
                    .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
                });
                batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                    .x = x1, .y = bottomY, .z = z1,
                    .u = uv.u1, .v = uv.v1,
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    .nx = 0.0f, .ny = 0.0f, .nz = 1.0f,
                    .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
                });
                batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                    .x = x1, .y = topY, .z = z1,
                    .u = uv.u1, .v = uv.v0,
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    .nx = 0.0f, .ny = 0.0f, .nz = 1.0f,
                    .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
                });
                batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                    .x = x0, .y = topY, .z = z0,
                    .u = uv.u0, .v = uv.v0,
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    .nx = 0.0f, .ny = 0.0f, .nz = 1.0f,
                    .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
                });

                batch.indices.push_back(baseIndex + 0u);
                batch.indices.push_back(baseIndex + 1u);
                batch.indices.push_back(baseIndex + 2u);
                batch.indices.push_back(baseIndex + 0u);
                batch.indices.push_back(baseIndex + 2u);
                batch.indices.push_back(baseIndex + 3u);
            };

            const auto uvForSegment =
                [&](int segmentIndex, int segmentCount) -> const UvRect& {
                    if (segmentCount <= 1) {
                        return midUvA;
                    }
                    if (segmentIndex == 0) {
                        return leftUv;
                    }
                    if (segmentIndex == segmentCount - 1) {
                        return rightUv;
                    }
                    return ((segmentIndex % 2) == 0) ? midUvA : midUvB;
                };

            const auto appendHorizontalEdge =
                [&](float x0, float x1, float z, bool reverse, int segmentCount) {
                    const int segments = std::max(1, segmentCount);
                    const float segmentWidth = (x1 - x0) / static_cast<float>(segments);
                    for (int i = 0; i < segments; ++i) {
                        const int logicalIndex = reverse ? (segments - 1 - i) : i;
                        const float segX0 = x0 + static_cast<float>(i) * segmentWidth;
                        const float segX1 = segX0 + segmentWidth;
                        appendWallSegment(segX0, z, segX1, z, uvForSegment(logicalIndex, segments));
                    }
                };

            const auto appendVerticalEdge =
                [&](float x, float z0, float z1, bool reverse, int segmentCount) {
                    const int segments = std::max(1, segmentCount);
                    const float segmentDepth = (z1 - z0) / static_cast<float>(segments);
                    for (int i = 0; i < segments; ++i) {
                        const int logicalIndex = reverse ? (segments - 1 - i) : i;
                        const float segZ0 = z0 + static_cast<float>(i) * segmentDepth;
                        const float segZ1 = segZ0 + segmentDepth;
                        appendWallSegment(x, segZ0, x, segZ1, uvForSegment(logicalIndex, segments));
                    }
                };

            appendHorizontalEdge(minX, maxX, maxZ + wallOffset, false, horizontalSegments);
            appendHorizontalEdge(minX, maxX, minZ - wallOffset, true, horizontalSegments);
            appendVerticalEdge(minX - wallOffset, minZ, maxZ, false, verticalSegments);
            appendVerticalEdge(maxX + wallOffset, minZ, maxZ, true, verticalSegments);

            scratch.worldIndexedBatches.push_back(std::move(batch));
        };

    appendLedgeRing(
        makeBoardLedgeGeometryKey(args),
        args.boardMinX,
        args.boardMaxX,
        args.boardMinZ,
        args.boardMaxZ,
        topY - 0.22f,
        0.015f,
        args.cols,
        args.rows);

    const int benchSlots = std::max(1, args.benchSlots);
    const float benchGapWorld = std::max(
        shared_board_grid::defaultVisualTheme().benchGapMin,
        args.worldCellSize * shared_board_grid::defaultVisualTheme().benchGapScale);
    const float benchMinX = -0.5f * static_cast<float>(benchSlots) * args.worldCellSize;
    const float benchMaxX = benchMinX + static_cast<float>(benchSlots) * args.worldCellSize;
    const float benchMinZ = args.boardMaxZ + benchGapWorld;
    const float benchMaxZ = benchMinZ + args.worldCellSize;

    appendLedgeRing(
        makeBenchLedgeGeometryKey(args, benchMinX, benchMaxX, benchMinZ, benchMaxZ),
        benchMinX,
        benchMaxX,
        benchMinZ,
        benchMaxZ,
        topY - 0.18f,
        0.015f,
        benchSlots,
        1);

    return true;
}

void appendRaisedBoardPlatform(const ProjectedBackdropArgs& args,
                               session_render_scratch::RenderScratch& scratch) {
    if (!args.supportsWorldTriangles3D || !themeUsesTexturedBoardDirt(args.theme)) {
        return;
    }

    auto& world3DTriangles = scratch.world3DTriangles;
    const RouteShellStyle& style = routeShellStyle(args.theme);
    const shared_board_grid::VisualTheme& boardTheme = style.boardTheme;
    const float topY = boardTheme.boardSurfaceY - 0.0005f;
    const float boardCliffStartY = topY - 0.22f;
    const float benchCliffStartY = topY - 0.18f;
    const float lowerGroundY = -0.315f;
    const Color cliffColorA = scaleColor(style.plateauSide, 0.88f);
    const Color cliffColorB = scaleColor(style.rock, 0.92f);
    const Color cliffColorC = scaleColor(style.rock, 0.78f);

    struct CliffProfile {
        float minX = 0.0f;
        float maxX = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
        float y = 0.0f;
    };

    const auto appendCliffBand =
        [&](const CliffProfile& upper,
            const CliffProfile& lower,
            const Color& frontColor,
            const Color& sideColor) {
            appendWorldQuadDoubleSided(
                world3DTriangles,
                glm::vec3(upper.minX, upper.y, upper.maxZ),
                glm::vec3(upper.maxX, upper.y, upper.maxZ),
                glm::vec3(lower.maxX, lower.y, lower.maxZ),
                glm::vec3(lower.minX, lower.y, lower.maxZ),
                frontColor);
            appendWorldQuadDoubleSided(
                world3DTriangles,
                glm::vec3(upper.maxX, upper.y, upper.minZ),
                glm::vec3(upper.minX, upper.y, upper.minZ),
                glm::vec3(lower.minX, lower.y, lower.minZ),
                glm::vec3(lower.maxX, lower.y, lower.minZ),
                frontColor);
            appendWorldQuadDoubleSided(
                world3DTriangles,
                glm::vec3(upper.minX, upper.y, upper.minZ),
                glm::vec3(upper.minX, upper.y, upper.maxZ),
                glm::vec3(lower.minX, lower.y, lower.maxZ),
                glm::vec3(lower.minX, lower.y, lower.minZ),
                sideColor);
            appendWorldQuadDoubleSided(
                world3DTriangles,
                glm::vec3(upper.maxX, upper.y, upper.maxZ),
                glm::vec3(upper.maxX, upper.y, upper.minZ),
                glm::vec3(lower.maxX, lower.y, lower.minZ),
                glm::vec3(lower.maxX, lower.y, lower.maxZ),
                sideColor);
        };

    const auto appendCliffOutcrop =
        [&](float minX,
            float minY,
            float minZ,
            float maxX,
            float maxY,
            float maxZ,
            float topScale,
            float sideScale) {
            appendBox(
                world3DTriangles,
                minX,
                minY,
                minZ,
                maxX,
                maxY,
                maxZ,
                scaleColor(style.rock, topScale),
                scaleColor(style.rock, sideScale));
        };

    const auto appendRockCliffSkirt =
        [&](float minX,
            float maxX,
            float minZ,
            float maxZ,
            float startY,
            float leftExtent,
            float rightExtent,
            float backExtent,
            float frontExtent,
            bool addFrontOutcrops,
            bool addSideOutcrops) {
            const CliffProfile lip{
                minX - 0.02f,
                maxX + 0.02f,
                minZ - 0.02f,
                maxZ + 0.02f,
                startY};
            const CliffProfile shelfA{
                minX - leftExtent * 0.28f,
                maxX + rightExtent * 0.24f,
                minZ - backExtent * 0.24f,
                maxZ + frontExtent * 0.18f,
                startY - 0.08f};
            const CliffProfile shelfB{
                minX - leftExtent * 0.58f,
                maxX + rightExtent * 0.52f,
                minZ - backExtent * 0.56f,
                maxZ + frontExtent * 0.40f,
                startY - 0.17f};
            const CliffProfile base{
                minX - leftExtent,
                maxX + rightExtent,
                minZ - backExtent,
                maxZ + frontExtent,
                lowerGroundY};

            appendCliffBand(lip, shelfA, cliffColorA, scaleColor(cliffColorA, 0.92f));
            appendCliffBand(shelfA, shelfB, cliffColorB, scaleColor(cliffColorB, 0.90f));
            appendCliffBand(shelfB, base, cliffColorC, scaleColor(cliffColorC, 0.88f));

            if (addFrontOutcrops) {
                appendCliffOutcrop(
                    minX - leftExtent * 0.18f,
                    startY - 0.17f,
                    maxZ + frontExtent * 0.12f,
                    minX + 0.95f,
                    startY - 0.06f,
                    maxZ + frontExtent * 0.42f,
                    1.04f,
                    0.72f);
                appendCliffOutcrop(
                    maxX - 1.15f,
                    startY - 0.23f,
                    maxZ + frontExtent * 0.20f,
                    maxX + rightExtent * 0.24f,
                    startY - 0.10f,
                    maxZ + frontExtent * 0.54f,
                    1.02f,
                    0.70f);
            }
            if (addSideOutcrops) {
                appendCliffOutcrop(
                    minX - leftExtent * 0.72f,
                    startY - 0.21f,
                    minZ + 0.35f,
                    minX - leftExtent * 0.34f,
                    startY - 0.09f,
                    minZ + 1.45f,
                    1.00f,
                    0.72f);
                appendCliffOutcrop(
                    maxX + rightExtent * 0.32f,
                    startY - 0.19f,
                    maxZ - 1.45f,
                    maxX + rightExtent * 0.74f,
                    startY - 0.08f,
                    maxZ - 0.30f,
                    1.03f,
                    0.71f);
            }
        };

    appendRockCliffSkirt(
        args.boardMinX,
        args.boardMaxX,
        args.boardMinZ,
        args.boardMaxZ,
        boardCliffStartY,
        0.86f,
        0.94f,
        0.80f,
        0.64f,
        true,
        true);

    const int benchSlots = std::max(1, args.benchSlots);
    const float benchGapWorld = std::max(
        shared_board_grid::defaultVisualTheme().benchGapMin,
        args.worldCellSize * shared_board_grid::defaultVisualTheme().benchGapScale);
    const float benchMinX = -0.5f * static_cast<float>(benchSlots) * args.worldCellSize;
    const float benchMaxX = benchMinX + static_cast<float>(benchSlots) * args.worldCellSize;
    const float benchMinZ = args.boardMaxZ + benchGapWorld;
    const float benchMaxZ = benchMinZ + args.worldCellSize;

    appendRockCliffSkirt(
        benchMinX,
        benchMaxX,
        benchMinZ,
        benchMaxZ,
        benchCliffStartY,
        0.44f,
        0.44f,
        0.28f,
        0.34f,
        true,
        false);
}

bool appendTexturedGroundPatch(const ProjectedBackdropArgs& args,
                               session_render_scratch::RenderScratch& scratch,
                               const char* label,
                               float minX,
                               float maxX,
                               float minZ,
                               float maxZ,
                               float y = -0.039f) {
    if (!args.supportsWorldIndexedMeshes || !args.ensureBackendTextureLoaded ||
        !label || maxX <= minX || maxZ <= minZ) {
        return false;
    }

    game::runtime::SharedBackendTextureCacheEntry* tex =
        args.ensureBackendTextureLoaded(kBackdropGrassTexturePath, false);
    if (!tex || !tex->valid || tex->width <= 0 || tex->height <= 0 ||
        tex->rgba.empty()) {
        return false;
    }

    shared_world_batches::WorldIndexedBatch batch{};
    batch.geometryCacheKey = makeGroundPatchGeometryKey(
        label, args.theme, minX, maxX, minZ, maxZ);
    batch.textureKey = kBackdropGrassTexturePath;
    batch.textureCacheKey = kBackdropGrassTexturePath;
    batch.textureRgba = tex->rgba.data();
    batch.textureWidth = tex->width;
    batch.textureHeight = tex->height;
    batch.textureWrapS = 10497;
    batch.textureWrapT = 10497;
    // Keep backdrop ground tiles on the plain textured path so world lighting
    // and model shading do not alter the authored atlas colors.
    batch.alphaMode = 0u;
    batch.blendMode = 0u;
    batch.materialMode = 0u;
    batch.characterInkingEnabled = 0u;
    batch.metallicFactor = 0.0f;
    batch.roughnessFactor = 1.0f;
    batch.vertexColorMulR = 1.0f;
    batch.vertexColorMulG = 1.0f;
    batch.vertexColorMulB = 1.0f;
    batch.vertexColorMulA = 1.0f;
    const float tileWorldSpan = std::max(0.001f, args.worldCellSize * 2.0f);
    const float repeatU = std::max(1.0f, (maxX - minX) / tileWorldSpan);
    const float repeatV = std::max(1.0f, (maxZ - minZ) / tileWorldSpan);

    batch.vertices = {
        IRenderBackend::WorldMeshVertex{
            .x = minX, .y = y, .z = minZ,
            .u = 0.0f, .v = 0.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
            .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
        },
        IRenderBackend::WorldMeshVertex{
            .x = maxX, .y = y, .z = minZ,
            .u = repeatU, .v = 0.0f,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
            .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
        },
        IRenderBackend::WorldMeshVertex{
            .x = maxX, .y = y, .z = maxZ,
            .u = repeatU, .v = repeatV,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
            .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
        },
        IRenderBackend::WorldMeshVertex{
            .x = minX, .y = y, .z = maxZ,
            .u = 0.0f, .v = repeatV,
            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            .nx = 0.0f, .ny = 1.0f, .nz = 0.0f,
            .tx = 1.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f,
        },
    };
    batch.indices = {0u, 1u, 2u, 0u, 2u, 3u};

    scratch.worldIndexedBatches.push_back(std::move(batch));
    return true;
}

void appendRouteTexturedGrassGround(const ProjectedBackdropArgs& args,
                                    session_render_scratch::RenderScratch& scratch,
                                    float outerMinX,
                                    float outerMaxX,
                                    float outerMinZ,
                                    float outerMaxZ) {
    if (!themeUsesTexturedGrassGround(args.theme)) {
        return;
    }

    (void)appendTexturedGroundPatch(
        args,
        scratch,
        "session_world_backdrop_grass_outer_far",
        outerMinX - 3.0f,
        outerMaxX + 3.0f,
        outerMinZ - 3.0f,
        outerMaxZ + 3.0f,
        -0.615f);
    (void)appendTexturedGroundPatch(
        args,
        scratch,
        "session_world_backdrop_grass_outer",
        outerMinX,
        outerMaxX,
        outerMinZ,
        outerMaxZ,
        -0.315f);
}

[[maybe_unused]] bool appendAuthoredBackdropTrees(const ProjectedBackdropArgs& args,
                                 session_render_scratch::RenderScratch& scratch,
                                 ArenaBackdropTheme theme,
                                 float outerMinX,
                                 float outerMaxX,
                                 float playableMinZ,
                                 float playableMaxZ) {
    if (!args.supportsWorldIndexedMeshes || !args.ensureBackendMeshLoaded) {
        return false;
    }

    auto* treeMesh = args.ensureBackendMeshLoaded(kBackdropEvergreenTreeModelPath);
    if (!treeMesh || treeMesh->vertices.empty() || treeMesh->indices.size() < 3u) {
        return false;
    }

    const std::size_t treeTriangleCount =
        std::max<std::size_t>(1u, estimatedBackdropTriangleCount(*treeMesh));
    const std::size_t triangleBudget =
        authoredTreeTriangleBudgetForGraphicsQuality(args.graphicsQuality);
    if (triangleBudget < treeTriangleCount) {
        return false;
    }

    auto appendPlacementsWithinBudget =
        [&](const BackdropPropPlacement* placements, std::size_t count) {
            std::size_t accumulatedTriangles = 0u;
            bool appendedAny = false;
            for (std::size_t i = 0; i < count; ++i) {
                if (accumulatedTriangles + treeTriangleCount > triangleBudget) {
                    break;
                }
                if (appendBackdropModelPlacement(
                        args,
                        scratch,
                        kBackdropEvergreenTreeModelPath,
                        placements[i])) {
                    appendedAny = true;
                    accumulatedTriangles += treeTriangleCount;
                }
            }
            return appendedAny;
        };

    bool appendedAny = false;

    switch (theme) {
        case ArenaBackdropTheme::Route22Foothills: {
            const BackdropPropPlacement placements[] = {
                {outerMinX + 1.15f, playableMinZ - 3.30f, -18.0f, 5.20f},
                {outerMaxX - 1.20f, playableMinZ - 3.10f, 22.0f, 5.10f},
            };
            appendedAny |= appendPlacementsWithinBudget(
                placements, std::size(placements));
            break;
        }
        case ArenaBackdropTheme::Route2ForestEdge: {
            const BackdropPropPlacement placements[] = {
                {outerMinX + 0.85f, playableMinZ - 3.00f, 12.0f, 5.80f},
                {outerMaxX - 0.90f, playableMinZ - 3.00f, 24.0f, 5.80f},
                {outerMinX + 2.20f, playableMinZ - 3.35f, -22.0f, 6.00f},
                {outerMaxX - 2.25f, playableMinZ - 3.30f, -16.0f, 6.00f},
                {0.0f, playableMinZ - 3.70f, 18.0f, 6.80f},
            };
            appendedAny |= appendPlacementsWithinBudget(
                placements, std::size(placements));
            break;
        }
        case ArenaBackdropTheme::ViridianForestShrine: {
            const BackdropPropPlacement placements[] = {
                {outerMinX + 0.65f, playableMinZ - 2.90f, 8.0f, 6.20f},
                {outerMaxX - 0.65f, playableMinZ - 2.95f, 10.0f, 6.20f},
                {outerMinX + 1.95f, playableMinZ - 3.55f, -18.0f, 6.60f},
                {outerMaxX - 1.95f, playableMinZ - 3.45f, -24.0f, 6.60f},
                {outerMinX + 3.35f, playableMinZ - 3.75f, 28.0f, 6.90f},
                {outerMaxX - 3.35f, playableMinZ - 3.78f, 16.0f, 6.90f},
                {0.0f, playableMinZ - 4.10f, -8.0f, 8.20f},
            };
            appendedAny |= appendPlacementsWithinBudget(
                placements, std::size(placements));
            break;
        }
        case ArenaBackdropTheme::Route3MountainPass: {
            const BackdropPropPlacement placements[] = {
                {outerMinX + 1.10f, playableMinZ - 3.10f, -12.0f, 4.60f},
                {outerMaxX - 1.10f, playableMinZ - 3.05f, 14.0f, 4.60f},
            };
            appendedAny |= appendPlacementsWithinBudget(
                placements, std::size(placements));
            break;
        }
        case ArenaBackdropTheme::Route1OpenRoad:
        case ArenaBackdropTheme::Default:
        default: {
            const BackdropPropPlacement placements[] = {
                {outerMinX + 1.05f, playableMinZ - 2.85f, 14.0f, 5.20f},
                {outerMaxX - 1.05f, playableMinZ - 2.70f, -18.0f, 5.10f},
                {0.0f, playableMinZ - 3.55f, -6.0f, 6.80f},
                {outerMinX + 0.95f, playableMaxZ + 1.35f, 8.0f, 4.40f},
                {outerMaxX - 0.95f, playableMaxZ + 1.20f, -12.0f, 4.40f},
            };
            appendedAny |= appendPlacementsWithinBudget(
                placements, std::size(placements));
            break;
        }
    }

    return appendedAny;
}

[[maybe_unused]] void appendOpenRoadProps(std::vector<IRenderBackend::WorldTriangle>& out,
                         const RouteShellStyle& style,
                         float outerMinX,
                         float outerMaxX,
                         float outerMinZ,
                         float playableMinZ,
                         float playableMaxZ,
                         bool useProceduralTrees) {
    appendFenceLine(out, outerMinX + 0.8f, outerMaxX - 0.8f, outerMinZ + 0.7f, 7, 0.75f, style.fence);
    if (useProceduralTrees) {
        appendTree(out, outerMinX + 0.9f, playableMinZ - 2.4f, 1.3f, 0.12f, 0.55f, 0.75f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.9f, playableMinZ - 2.2f, 1.25f, 0.12f, 0.52f, 0.72f, style.trunk, style.leaves);
        appendTree(out, outerMinX + 0.8f, playableMaxZ + 1.4f, 1.15f, 0.11f, 0.48f, 0.65f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.8f, playableMaxZ + 1.2f, 1.12f, 0.11f, 0.48f, 0.65f, style.trunk, style.leaves);
    }
}

[[maybe_unused]] void appendFoothillProps(std::vector<IRenderBackend::WorldTriangle>& out,
                         const RouteShellStyle& style,
                         float outerMinX,
                         float outerMaxX,
                         float playableMinZ,
                         float playableMaxZ,
                         bool useProceduralTrees) {
    appendBox(out, outerMinX + 0.3f, -0.04f, playableMinZ - 3.2f, outerMinX + 1.7f, 0.55f, playableMinZ - 1.8f, scaleColor(style.rock, 1.06f), scaleColor(style.rock, 0.74f));
    appendBox(out, outerMaxX - 2.0f, -0.04f, playableMinZ - 3.6f, outerMaxX - 0.4f, 0.78f, playableMinZ - 2.1f, scaleColor(style.rock, 1.02f), scaleColor(style.rock, 0.70f));
    appendBox(out, outerMaxX - 1.8f, -0.04f, playableMaxZ + 0.6f, outerMaxX - 0.6f, 0.42f, playableMaxZ + 1.7f, scaleColor(style.rock, 1.04f), scaleColor(style.rock, 0.72f));
    if (useProceduralTrees) {
        appendTree(out, outerMinX + 1.2f, playableMaxZ + 1.1f, 1.0f, 0.11f, 0.42f, 0.58f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 1.1f, playableMaxZ + 1.0f, 1.0f, 0.11f, 0.42f, 0.58f, style.trunk, style.leaves);
    }
}

[[maybe_unused]] void appendForestEdgeProps(std::vector<IRenderBackend::WorldTriangle>& out,
                           const RouteShellStyle& style,
                           float outerMinX,
                           float outerMaxX,
                           float playableMinZ,
                           float playableMaxZ,
                           bool useProceduralTrees) {
    (void)playableMaxZ;
    if (useProceduralTrees) {
        appendTree(out, outerMinX + 0.7f, playableMinZ - 2.7f, 1.45f, 0.12f, 0.58f, 0.78f, style.trunk, style.leaves);
        appendTree(out, outerMinX + 2.0f, playableMinZ - 3.0f, 1.5f, 0.12f, 0.60f, 0.82f, style.trunk, style.leaves);
        appendTree(out, 0.0f, playableMinZ - 3.2f, 1.55f, 0.13f, 0.64f, 0.85f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 2.0f, playableMinZ - 2.95f, 1.48f, 0.12f, 0.60f, 0.80f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.7f, playableMinZ - 2.6f, 1.42f, 0.12f, 0.58f, 0.76f, style.trunk, style.leaves);
    }
}

[[maybe_unused]] void appendViridianProps(std::vector<IRenderBackend::WorldTriangle>& out,
                         const RouteShellStyle& style,
                         float outerMinX,
                         float outerMaxX,
                         float playableMinZ,
                         float playableMaxZ,
                         bool useProceduralTrees) {
    if (useProceduralTrees) {
        appendTree(out, outerMinX + 0.5f, playableMinZ - 2.5f, 1.7f, 0.13f, 0.66f, 0.92f, style.trunk, style.leaves);
        appendTree(out, outerMinX + 1.7f, playableMinZ - 3.1f, 1.8f, 0.13f, 0.70f, 0.96f, style.trunk, style.leaves);
        appendTree(out, outerMinX + 3.1f, playableMinZ - 3.3f, 1.85f, 0.14f, 0.72f, 0.98f, style.trunk, style.leaves);
        appendTree(out, 0.0f, playableMinZ - 3.45f, 1.9f, 0.14f, 0.75f, 1.02f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 3.1f, playableMinZ - 3.35f, 1.85f, 0.14f, 0.72f, 0.98f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 1.7f, playableMinZ - 3.0f, 1.8f, 0.13f, 0.70f, 0.96f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.5f, playableMinZ - 2.55f, 1.7f, 0.13f, 0.66f, 0.92f, style.trunk, style.leaves);
        appendTree(out, outerMinX + 0.6f, playableMaxZ + 1.0f, 1.45f, 0.12f, 0.56f, 0.76f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.6f, playableMaxZ + 1.0f, 1.45f, 0.12f, 0.56f, 0.76f, style.trunk, style.leaves);
    }
    appendStoneMarker(out, -0.7f, playableMinZ - 2.0f, 0.32f, 0.95f, style.shrineStone);
    appendStoneMarker(out, 0.7f, playableMinZ - 2.0f, 0.32f, 0.95f, style.shrineStone);
    appendStoneMarker(out, 0.0f, playableMinZ - 2.35f, 0.72f, 0.28f, style.shrineStone);
}

[[maybe_unused]] void appendMountainPassProps(std::vector<IRenderBackend::WorldTriangle>& out,
                             const RouteShellStyle& style,
                             float outerMinX,
                             float outerMaxX,
                             float playableMinZ,
                             float playableMaxZ,
                             bool useProceduralTrees) {
    appendBox(out, outerMinX + 0.2f, -0.04f, playableMinZ - 3.6f, outerMinX + 2.0f, 0.95f, playableMinZ - 1.7f, scaleColor(style.rock, 1.05f), scaleColor(style.rock, 0.70f));
    appendBox(out, outerMaxX - 2.0f, -0.04f, playableMinZ - 3.7f, outerMaxX - 0.2f, 1.05f, playableMinZ - 1.8f, scaleColor(style.rock, 1.06f), scaleColor(style.rock, 0.72f));
    appendBox(out, outerMinX + 2.5f, -0.04f, playableMinZ - 3.0f, outerMinX + 4.1f, 0.62f, playableMinZ - 2.0f, scaleColor(style.rock, 1.02f), scaleColor(style.rock, 0.72f));
    appendBox(out, outerMaxX - 4.1f, -0.04f, playableMinZ - 3.0f, outerMaxX - 2.5f, 0.62f, playableMinZ - 2.0f, scaleColor(style.rock, 1.02f), scaleColor(style.rock, 0.72f));
    if (useProceduralTrees) {
        appendTree(out, outerMinX + 0.9f, playableMaxZ + 1.0f, 1.05f, 0.11f, 0.44f, 0.60f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.9f, playableMaxZ + 1.0f, 1.05f, 0.11f, 0.44f, 0.60f, style.trunk, style.leaves);
    }
}

void appendRouteArenaShell(const ProjectedBackdropArgs& args,
                           session_render_scratch::RenderScratch& scratch) {
    if (!args.supportsWorldTriangles3D) return;

    auto& world3DTriangles = scratch.world3DTriangles;
    const RouteShellStyle& style = routeShellStyle(args.theme);
    const float benchGapWorld = std::max(
        shared_board_grid::defaultVisualTheme().benchGapMin,
        args.worldCellSize * shared_board_grid::defaultVisualTheme().benchGapScale);
    const int benchSlots = std::max(1, args.benchSlots);
    const float benchMinX = -0.5f * static_cast<float>(benchSlots) * args.worldCellSize;
    const float benchMaxX = benchMinX + static_cast<float>(benchSlots) * args.worldCellSize;
    const float benchMinZ = args.boardMaxZ + benchGapWorld;
    const float benchMaxZ = benchMinZ + args.worldCellSize;

    const float playableMinX = std::min(args.boardMinX, benchMinX);
    const float playableMaxX = std::max(args.boardMaxX, benchMaxX);
    const float playableMinZ = args.boardMinZ;
    const float playableMaxZ = std::max(args.boardMaxZ, benchMaxZ);

    const float outerMinX = playableMinX - style.sideMargin;
    const float outerMaxX = playableMaxX + style.sideMargin;
    const float outerMinZ = playableMinZ - style.backMargin;
    const float outerMaxZ = playableMaxZ + style.frontMargin;

    appendBox(
        world3DTriangles,
        outerMinX - 3.0f,
        -0.86f,
        outerMinZ - 3.0f,
        outerMaxX + 3.0f,
        -0.62f,
        outerMaxZ + 3.0f,
        scaleColor(style.distantGround, 1.02f),
        scaleColor(style.distantGround, 0.72f));

    appendBox(
        world3DTriangles,
        outerMinX,
        -0.56f,
        outerMinZ,
        outerMaxX,
        -0.32f,
        outerMaxZ,
        scaleColor(style.plateauTop, 1.04f),
        scaleColor(style.plateauSide, 0.92f));

    if (!themeUsesTexturedGrassGround(args.theme)) {
        appendGrassPatch(
            world3DTriangles,
            outerMinX + 0.4f,
            outerMaxX - 0.4f,
            playableMaxZ + 0.2f,
            outerMaxZ - 0.4f,
            style.grass);
    }

    appendRouteTexturedGrassGround(
        args,
        scratch,
        outerMinX,
        outerMaxX,
        outerMinZ,
        outerMaxZ);
}

void appendRouteBackdropFill(const ProjectedBackdropArgs& args,
                             session_render_scratch::RenderScratch& scratch) {
    auto& quads = scratch.worldBackgroundQuads;

    IRenderBackend::DebugQuad full{};
    full.x = 0.0f;
    full.y = 0.0f;
    full.w = static_cast<float>(args.drawableW > 0 ? args.drawableW : 1920);
    full.h = static_cast<float>(args.drawableH > 0 ? args.drawableH : 1080);
    full.r = kBackdropBlackFill[0];
    full.g = kBackdropBlackFill[1];
    full.b = kBackdropBlackFill[2];
    full.a = kBackdropBlackFill[3];
    quads.push_back(full);
}

session_render_scratch::ProjectedBackdropCacheKey makeProjectedBackdropKey(
    const ProjectedBackdropArgs& args) {
    session_render_scratch::ProjectedBackdropCacheKey key{};
    key.supportsWorldTriangles3D = args.supportsWorldTriangles3D;
    key.supportsWorldIndexedMeshes = args.supportsWorldIndexedMeshes;
    key.enableBackdropTiles = args.enableBackdropTiles;
    key.rows = args.rows;
    key.cols = args.cols;
    key.benchSlots = args.benchSlots;
    key.graphicsQuality = args.graphicsQuality;
    key.worldCellSize = args.worldCellSize;
    key.boardMinX = args.boardMinX;
    key.boardMinZ = args.boardMinZ;
    key.boardMaxX = args.boardMaxX;
    key.boardMaxZ = args.boardMaxZ;
    key.drawableW = args.drawableW;
    key.drawableH = args.drawableH;
    key.boardX = args.boardX;
    key.boardY = args.boardY;
    key.boardW = args.boardW;
    key.boardH = args.boardH;
    key.cellW = args.cellW;
    key.cellH = args.cellH;
    key.line = args.line;
    key.arenaBackdropTheme = static_cast<int>(args.theme);
    key.route1BackdropScaleMul = args.route1BackdropTuning.scaleMul;
    key.route1BackdropOffsetXCells = args.route1BackdropTuning.offsetXCells;
    key.route1BackdropOffsetY = args.route1BackdropTuning.offsetY;
    key.route1BackdropOffsetZCells = args.route1BackdropTuning.offsetZCells;
    key.route1BackdropYawDeg = args.route1BackdropTuning.yawDeg;
    return key;
}

shared_board_grid::Config makeBoardGridConfig(const ProjectedBackdropArgs& args) {
    shared_board_grid::Config cfg = shared_projected_scene::makeBoardGridConfig(
        args.supportsWorldTriangles3D,
        args.rows,
        args.cols,
        args.benchSlots,
        args.worldCellSize,
        args.boardMinX,
        args.boardMinZ,
        args.boardMaxX,
        args.boardMaxZ,
        args.boardX,
        args.boardY,
        args.boardW,
        args.boardH,
        args.cellW,
        args.cellH,
        args.line);
    cfg.visualTheme = args.enableBackdropTiles
        ? &routeShellStyle(args.theme).boardTheme
        : &plainBlackBoardTheme();
    return cfg;
}

void appendBackdropGeometry(const ProjectedBackdropArgs& args,
                            shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
                            session_render_scratch::RenderScratch& scratch) {
    appendRouteBackdropFill(args, scratch);
    (void)appendRoute1BackdropModel(args, scratch);

    const shared_board_grid::Config boardGridCfg = makeBoardGridConfig(args);
    shared_projected_scene::appendBoardAndBench(
        boardGridCfg,
        scratch.worldTriangles,
        scratch.world3DTriangles,
        scratch.worldBackgroundQuads,
        scratch.lines,
        projectedDebug);
    if (appendRoute1PatternOverlay(args, scratch)) {
        return;
    }
    if (args.enableBackdropTiles && routeThemeUsesBoardTileOverlay(args.theme)) {
        (void)appendTexturedBoardTiles(args, scratch);
        (void)appendTexturedBenchTiles(args, scratch);
    }
}

} // namespace

Route1BackdropTuningState defaultRoute1BackdropTuningState() {
    return Route1BackdropTuningState{
        .enabled = false,
        .scaleMul = kBackdropRoute1ScaleMul,
        .offsetXCells = kBackdropRoute1CenterOffsetXCells,
        .offsetY = kBackdropRoute1CenterOffsetY,
        .offsetZCells = kBackdropRoute1CenterOffsetZCells,
        .yawDeg = kBackdropRoute1YawDeg,
    };
}

std::string formatRoute1BackdropTuningState(const Route1BackdropTuningState& state) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "[Backdrop][Route1Tune] enabled=" << (state.enabled ? 1 : 0)
        << " scale_mul=" << state.scaleMul
        << " offset_x_cells=" << state.offsetXCells
        << " offset_y=" << state.offsetY
        << " offset_z_cells=" << state.offsetZCells
        << " yaw_deg=" << state.yawDeg;
    return out.str();
}

ArenaBackdropTheme routeThemeFromScriptPath(const std::string& stateScriptPath) {
    if (stateScriptPath.find("viridian_forest") != std::string::npos) {
        return ArenaBackdropTheme::ViridianForestShrine;
    }
    if (stateScriptPath.find("route3") != std::string::npos) {
        return ArenaBackdropTheme::Route3MountainPass;
    }
    if (stateScriptPath.find("route22") != std::string::npos) {
        return ArenaBackdropTheme::Route22Foothills;
    }
    if (stateScriptPath.find("route2") != std::string::npos) {
        return ArenaBackdropTheme::Route2ForestEdge;
    }
    if (stateScriptPath.find("route1") != std::string::npos ||
        stateScriptPath.find("starter") != std::string::npos) {
        return ArenaBackdropTheme::Route1OpenRoad;
    }
    return ArenaBackdropTheme::Default;
}

std::size_t authoredTreeTriangleBudgetForGraphicsQuality(int graphicsQuality) {
    using game::video::GraphicsQuality;

    switch (static_cast<GraphicsQuality>(
        game::video::sanitizeGraphicsQuality(graphicsQuality))) {
        case GraphicsQuality::Low:
            return 25000u;
        case GraphicsQuality::Medium:
            return 50000u;
        case GraphicsQuality::High:
            return 90000u;
        case GraphicsQuality::Ultra:
        default:
            return 140000u;
    }
}

float composeProjectedBackdrop(const ProjectedBackdropArgs& args,
                               shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
                               session_render_scratch::RenderScratch& scratch) {
    const auto composeStart = RenderBuildClock::now();

    if (args.supportsWorldTriangles3D) {
        const session_render_scratch::ProjectedBackdropCacheKey projectedBackdropKey =
            makeProjectedBackdropKey(args);
        if (!scratch.projectedBackdropValid ||
            !(scratch.projectedBackdropKey == projectedBackdropKey)) {
            scratch.worldBackgroundQuads.clear();
            scratch.worldTriangles.clear();
            scratch.world3DTriangles.clear();
            scratch.worldIndexedBatches.clear();
            scratch.lines.clear();

            appendBackdropGeometry(args, projectedDebug, scratch);

            scratch.projectedBackdropValid = true;
            scratch.projectedBackdropKey = projectedBackdropKey;
            scratch.projectedBackdropWorldBackgroundQuadsCount =
                scratch.worldBackgroundQuads.size();
            scratch.projectedBackdropWorldTrianglesCount = scratch.worldTriangles.size();
            scratch.projectedBackdropWorld3DTrianglesCount = scratch.world3DTriangles.size();
            scratch.projectedBackdropWorldIndexedBatchesCount =
                scratch.worldIndexedBatches.size();
            scratch.projectedBackdropLinesCount = scratch.lines.size();
        } else {
            scratch.worldBackgroundQuads.resize(
                scratch.projectedBackdropWorldBackgroundQuadsCount);
            scratch.worldTriangles.resize(scratch.projectedBackdropWorldTrianglesCount);
            scratch.world3DTriangles.resize(
                scratch.projectedBackdropWorld3DTrianglesCount);
            scratch.worldIndexedBatches.resize(
                scratch.projectedBackdropWorldIndexedBatchesCount);
            scratch.lines.resize(scratch.projectedBackdropLinesCount);
        }
    } else {
        session_render_scratch::invalidateProjectedBackdrop(scratch);
        scratch.worldIndexedBatches.clear();
        appendBackdropGeometry(args, projectedDebug, scratch);
    }

    return static_cast<float>(
        std::chrono::duration<double, std::milli>(
            RenderBuildClock::now() - composeStart).count());
}

} // namespace game::runtime::session_world_backdrop

