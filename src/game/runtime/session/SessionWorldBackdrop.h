#pragma once

#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/shared/projected/core/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>

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

struct Route1BackdropTuningState {
    bool enabled = false;
    float scaleMul = 5.0f;
    float offsetXCells = 0.0f;
    float offsetY = 0.75f;
    float offsetZCells = 0.0f;
    float yawDeg = 0.0f;
};

Route1BackdropTuningState defaultRoute1BackdropTuningState();
std::string formatRoute1BackdropTuningState(const Route1BackdropTuningState& state);

struct ProjectedBackdropArgs {
    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
    bool enableBackdropTiles = true;
    bool enableCanonicalRoute1Environment = false;
    int graphicsQuality = 3;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;
    int benchGapCells = 0;
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
    float simulationSeconds = 0.0f;
    std::string stateScriptPath;
    ArenaBackdropTheme theme = ArenaBackdropTheme::Default;
    std::span<
        const route1_environment::EncounterGrassInteractor>
        encounterGrassInteractors{};
    Route1BackdropTuningState route1BackdropTuning = defaultRoute1BackdropTuningState();
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>
        ensureBackendTextureLoaded;
};

ArenaBackdropTheme routeThemeFromScriptPath(const std::string& stateScriptPath);
// Temporary content policy: until a route receives its own authored world,
// retain the route theme identity but resolve its environment to the complete
// authored Route 1 scene. Default is intentionally excluded because it is
// also used by non-route screens.
bool routeThemeUsesAuthoredRoute1Fallback(ArenaBackdropTheme theme) noexcept;

float composeProjectedBackdrop(
    const ProjectedBackdropArgs& args,
    shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    session_render_scratch::RenderScratch& scratch);

} // namespace game::runtime::session_world_backdrop

