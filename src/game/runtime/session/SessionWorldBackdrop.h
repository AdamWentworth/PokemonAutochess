#pragma once

#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"

#include <cstddef>
#include <functional>
#include <string>

namespace game::runtime::render_model {
struct MeshData;
}

namespace game::runtime {
struct SharedBackendTextureCacheEntry;
}

namespace game::runtime::session_world_backdrop {

enum class ArenaBackdropTheme {
    Default = 0,
    Route1OpenRoad,
    Route22Foothills,
    Route2ForestEdge,
    ViridianForestShrine,
    Route3MountainPass,
};

struct ProjectedBackdropArgs {
    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
    int graphicsQuality = 3;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;
    float worldCellSize = 1.0f;
    float boardMinX = 0.0f;
    float boardMinZ = 0.0f;
    float boardMaxX = 0.0f;
    float boardMaxZ = 0.0f;
    int drawableW = 0;
    int drawableH = 0;
    float boardX = 0.0f;
    float boardY = 0.0f;
    float boardW = 0.0f;
    float boardH = 0.0f;
    float cellW = 0.0f;
    float cellH = 0.0f;
    float line = 1.0f;
    ArenaBackdropTheme theme = ArenaBackdropTheme::Default;
    std::function<render_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>
        ensureBackendTextureLoaded;
};

ArenaBackdropTheme routeThemeFromScriptPath(const std::string& stateScriptPath);
std::size_t authoredTreeTriangleBudgetForGraphicsQuality(int graphicsQuality);

float composeProjectedBackdrop(
    const ProjectedBackdropArgs& args,
    shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    session_render_scratch::RenderScratch& scratch);

} // namespace game::runtime::session_world_backdrop
