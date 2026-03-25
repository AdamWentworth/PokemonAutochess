#include "game/runtime/session/SessionWorldBackdrop.h"

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/video/VideoPreferences.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>

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

Color scaleColor(const Color& color, float scale) {
    return {
        std::clamp(color[0] * scale, 0.0f, 1.0f),
        std::clamp(color[1] * scale, 0.0f, 1.0f),
        std::clamp(color[2] * scale, 0.0f, 1.0f),
        color[3]
    };
}

std::array<float, 16> mat4ToArray(const glm::mat4& value) {
    std::array<float, 16> out{};
    const float* src = glm::value_ptr(value);
    std::copy(src, src + out.size(), out.begin());
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

bool appendBackdropModelPlacement(
    const ProjectedBackdropArgs& args,
    session_render_scratch::RenderScratch& scratch,
    const char* modelPath,
    const BackdropPropPlacement& placement,
    float groundY = -0.04f) {
    if (!args.supportsWorldIndexedMeshes || !args.ensureBackendMeshLoaded || !modelPath) {
        return false;
    }

    auto* mesh = args.ensureBackendMeshLoaded(modelPath);
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

    const float uniformScale =
        std::max(0.01f, placement.scale) * std::max(0.01f, mesh->modelScaleFactor);
    const float liftY = groundY - mesh->boundsMin.y * uniformScale;
    glm::mat4 modelM(1.0f);
    modelM = glm::translate(modelM, glm::vec3(placement.x, liftY, placement.z));
    modelM = glm::rotate(modelM, glm::radians(placement.yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
    modelM = glm::scale(modelM, glm::vec3(uniformScale));
    const std::array<float, 16> modelMatrix = mat4ToArray(modelM);
    const float sortDepth = placement.x * placement.x + placement.z * placement.z;

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

std::size_t estimatedBackdropTriangleCount(
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

const RouteShellStyle& routeShellStyle(ArenaBackdropTheme theme) {
    static const RouteShellStyle route1 = [] {
        RouteShellStyle style;
        style.boardTheme = makeBoardTheme(
            {0.08f, 0.12f, 0.09f, 0.34f},
            {0.13f, 0.17f, 0.11f, 0.28f},
            {0.09f, 0.13f, 0.10f, 0.30f},
            {0.14f, 0.18f, 0.12f, 0.24f},
            {0.82f, 0.87f, 0.76f, 0.94f},
            {0.06f, 0.08f, 0.06f, 0.92f},
            {0.10f, 0.15f, 0.10f, 0.34f},
            {0.14f, 0.18f, 0.12f, 0.26f},
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

void appendTree(std::vector<IRenderBackend::WorldTriangle>& out,
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

void appendFenceLine(std::vector<IRenderBackend::WorldTriangle>& out,
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

void appendStoneMarker(std::vector<IRenderBackend::WorldTriangle>& out,
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

bool appendAuthoredBackdropTrees(const ProjectedBackdropArgs& args,
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

void appendOpenRoadProps(std::vector<IRenderBackend::WorldTriangle>& out,
                         const RouteShellStyle& style,
                         float outerMinX,
                         float outerMaxX,
                         float outerMinZ,
                         float playableMinZ,
                         float playableMaxZ,
                         bool useProceduralTrees) {
    appendBox(
        out,
        outerMinX + 0.6f,
        -0.039f,
        playableMinZ - 2.6f,
        outerMaxX - 0.6f,
        -0.015f,
        playableMinZ - 1.2f,
        scaleColor(style.path, 1.05f),
        scaleColor(style.path, 0.78f));
    appendFenceLine(out, outerMinX + 0.8f, outerMaxX - 0.8f, outerMinZ + 0.7f, 7, 0.75f, style.fence);
    if (useProceduralTrees) {
        appendTree(out, outerMinX + 0.9f, playableMinZ - 2.4f, 1.3f, 0.12f, 0.55f, 0.75f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.9f, playableMinZ - 2.2f, 1.25f, 0.12f, 0.52f, 0.72f, style.trunk, style.leaves);
        appendTree(out, outerMinX + 0.8f, playableMaxZ + 1.4f, 1.15f, 0.11f, 0.48f, 0.65f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.8f, playableMaxZ + 1.2f, 1.12f, 0.11f, 0.48f, 0.65f, style.trunk, style.leaves);
    }
    appendGrassPatch(out, outerMinX + 1.1f, outerMinX + 2.3f, playableMaxZ + 0.8f, playableMaxZ + 1.6f, style.grass);
    appendGrassPatch(out, outerMaxX - 2.3f, outerMaxX - 1.1f, playableMaxZ + 0.7f, playableMaxZ + 1.5f, style.grass);
}

void appendFoothillProps(std::vector<IRenderBackend::WorldTriangle>& out,
                         const RouteShellStyle& style,
                         float outerMinX,
                         float outerMaxX,
                         float playableMinZ,
                         float playableMaxZ,
                         bool useProceduralTrees) {
    appendBox(
        out,
        outerMinX + 0.5f,
        -0.039f,
        playableMinZ - 2.0f,
        outerMaxX - 1.4f,
        -0.016f,
        playableMinZ - 0.9f,
        scaleColor(style.path, 1.03f),
        scaleColor(style.path, 0.76f));
    appendBox(out, outerMinX + 0.3f, -0.04f, playableMinZ - 3.2f, outerMinX + 1.7f, 0.55f, playableMinZ - 1.8f, scaleColor(style.rock, 1.06f), scaleColor(style.rock, 0.74f));
    appendBox(out, outerMaxX - 2.0f, -0.04f, playableMinZ - 3.6f, outerMaxX - 0.4f, 0.78f, playableMinZ - 2.1f, scaleColor(style.rock, 1.02f), scaleColor(style.rock, 0.70f));
    appendBox(out, outerMaxX - 1.8f, -0.04f, playableMaxZ + 0.6f, outerMaxX - 0.6f, 0.42f, playableMaxZ + 1.7f, scaleColor(style.rock, 1.04f), scaleColor(style.rock, 0.72f));
    if (useProceduralTrees) {
        appendTree(out, outerMinX + 1.2f, playableMaxZ + 1.1f, 1.0f, 0.11f, 0.42f, 0.58f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 1.1f, playableMaxZ + 1.0f, 1.0f, 0.11f, 0.42f, 0.58f, style.trunk, style.leaves);
    }
}

void appendForestEdgeProps(std::vector<IRenderBackend::WorldTriangle>& out,
                           const RouteShellStyle& style,
                           float outerMinX,
                           float outerMaxX,
                           float playableMinZ,
                           float playableMaxZ,
                           bool useProceduralTrees) {
    appendBox(
        out,
        outerMinX + 1.2f,
        -0.039f,
        playableMinZ - 1.9f,
        outerMaxX - 1.2f,
        -0.017f,
        playableMinZ - 1.1f,
        scaleColor(style.path, 1.02f),
        scaleColor(style.path, 0.74f));
    if (useProceduralTrees) {
        appendTree(out, outerMinX + 0.7f, playableMinZ - 2.7f, 1.45f, 0.12f, 0.58f, 0.78f, style.trunk, style.leaves);
        appendTree(out, outerMinX + 2.0f, playableMinZ - 3.0f, 1.5f, 0.12f, 0.60f, 0.82f, style.trunk, style.leaves);
        appendTree(out, 0.0f, playableMinZ - 3.2f, 1.55f, 0.13f, 0.64f, 0.85f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 2.0f, playableMinZ - 2.95f, 1.48f, 0.12f, 0.60f, 0.80f, style.trunk, style.leaves);
        appendTree(out, outerMaxX - 0.7f, playableMinZ - 2.6f, 1.42f, 0.12f, 0.58f, 0.76f, style.trunk, style.leaves);
    }
    appendGrassPatch(out, outerMinX + 0.8f, outerMinX + 2.4f, playableMaxZ + 0.5f, playableMaxZ + 1.5f, style.grass);
    appendGrassPatch(out, outerMaxX - 2.4f, outerMaxX - 0.8f, playableMaxZ + 0.5f, playableMaxZ + 1.4f, style.grass);
}

void appendViridianProps(std::vector<IRenderBackend::WorldTriangle>& out,
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

void appendMountainPassProps(std::vector<IRenderBackend::WorldTriangle>& out,
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
    appendBox(out, outerMinX + 1.0f, -0.039f, playableMinZ - 1.8f, outerMaxX - 1.0f, -0.016f, playableMinZ - 0.9f, scaleColor(style.path, 1.02f), scaleColor(style.path, 0.76f));
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
        -0.42f,
        outerMinZ - 3.0f,
        outerMaxX + 3.0f,
        -0.28f,
        outerMaxZ + 3.0f,
        scaleColor(style.distantGround, 1.02f),
        scaleColor(style.distantGround, 0.72f));

    appendBox(
        world3DTriangles,
        outerMinX,
        -0.24f,
        outerMinZ,
        outerMaxX,
        -0.04f,
        outerMaxZ,
        scaleColor(style.plateauTop, 1.04f),
        scaleColor(style.plateauSide, 0.92f));

    appendGrassPatch(
        world3DTriangles,
        outerMinX + 0.4f,
        outerMaxX - 0.4f,
        playableMaxZ + 0.2f,
        outerMaxZ - 0.4f,
        style.grass);

    const bool hasAuthoredTrees = appendAuthoredBackdropTrees(
        args,
        scratch,
        args.theme,
        outerMinX,
        outerMaxX,
        playableMinZ,
        playableMaxZ);

    switch (args.theme) {
        case ArenaBackdropTheme::Route22Foothills:
            appendFoothillProps(
                world3DTriangles,
                style,
                outerMinX,
                outerMaxX,
                playableMinZ,
                playableMaxZ,
                !hasAuthoredTrees);
            break;
        case ArenaBackdropTheme::Route2ForestEdge:
            appendForestEdgeProps(
                world3DTriangles,
                style,
                outerMinX,
                outerMaxX,
                playableMinZ,
                playableMaxZ,
                !hasAuthoredTrees);
            break;
        case ArenaBackdropTheme::ViridianForestShrine:
            appendViridianProps(
                world3DTriangles,
                style,
                outerMinX,
                outerMaxX,
                playableMinZ,
                playableMaxZ,
                !hasAuthoredTrees);
            break;
        case ArenaBackdropTheme::Route3MountainPass:
            appendMountainPassProps(
                world3DTriangles,
                style,
                outerMinX,
                outerMaxX,
                playableMinZ,
                playableMaxZ,
                !hasAuthoredTrees);
            break;
        case ArenaBackdropTheme::Route1OpenRoad:
        case ArenaBackdropTheme::Default:
        default:
            appendOpenRoadProps(
                world3DTriangles,
                style,
                outerMinX,
                outerMaxX,
                outerMinZ,
                playableMinZ,
                playableMaxZ,
                !hasAuthoredTrees);
            break;
    }
}

void appendRouteBackdropFill(const ProjectedBackdropArgs& args,
                             session_render_scratch::RenderScratch& scratch) {
    const RouteShellStyle& style = routeShellStyle(args.theme);
    auto& quads = scratch.worldBackgroundQuads;

    IRenderBackend::DebugQuad full{};
    full.x = 0.0f;
    full.y = 0.0f;
    full.w = static_cast<float>(args.drawableW > 0 ? args.drawableW : 1920);
    full.h = static_cast<float>(args.drawableH > 0 ? args.drawableH : 1080);
    full.r = style.farFill[0];
    full.g = style.farFill[1];
    full.b = style.farFill[2];
    full.a = style.farFill[3];
    quads.push_back(full);

    IRenderBackend::DebugQuad lower = full;
    lower.y = args.boardY + args.boardH * 0.18f;
    lower.h = std::max(0.0f, full.h - lower.y);
    lower.r = style.lowerFill[0];
    lower.g = style.lowerFill[1];
    lower.b = style.lowerFill[2];
    lower.a = style.lowerFill[3];
    quads.push_back(lower);

    IRenderBackend::DebugQuad accent = full;
    accent.y = 0.0f;
    accent.h = std::max(0.0f, args.boardY + args.boardH * 0.30f);
    accent.r = style.upperAccent[0];
    accent.g = style.upperAccent[1];
    accent.b = style.upperAccent[2];
    accent.a = style.upperAccent[3];
    quads.push_back(accent);
}

session_render_scratch::ProjectedBackdropCacheKey makeProjectedBackdropKey(
    const ProjectedBackdropArgs& args) {
    session_render_scratch::ProjectedBackdropCacheKey key{};
    key.supportsWorldTriangles3D = args.supportsWorldTriangles3D;
    key.supportsWorldIndexedMeshes = args.supportsWorldIndexedMeshes;
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
    cfg.visualTheme = &routeShellStyle(args.theme).boardTheme;
    return cfg;
}

void appendBackdropGeometry(const ProjectedBackdropArgs& args,
                            shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
                            session_render_scratch::RenderScratch& scratch) {
    appendRouteBackdropFill(args, scratch);
    appendRouteArenaShell(args, scratch);

    const shared_board_grid::Config boardGridCfg = makeBoardGridConfig(args);
    shared_projected_scene::appendBoardAndBench(
        boardGridCfg,
        scratch.worldTriangles,
        scratch.world3DTriangles,
        scratch.worldBackgroundQuads,
        scratch.lines,
        projectedDebug);
}

} // namespace

ArenaBackdropTheme routeThemeFromScriptPath(const std::string& stateScriptPath) {
    if (stateScriptPath.find("viridian_forest") != std::string::npos) {
        return ArenaBackdropTheme::ViridianForestShrine;
    }
    if (stateScriptPath.find("route3") != std::string::npos) {
        return ArenaBackdropTheme::Route3MountainPass;
    }
    if (stateScriptPath.find("route2") != std::string::npos) {
        return ArenaBackdropTheme::Route2ForestEdge;
    }
    if (stateScriptPath.find("route22") != std::string::npos) {
        return ArenaBackdropTheme::Route22Foothills;
    }
    if (stateScriptPath.find("route1") != std::string::npos) {
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
