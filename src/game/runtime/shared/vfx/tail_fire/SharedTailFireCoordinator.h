#pragma once

#include "game/PokemonInstance.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"
#include "game/vfx/TailFireVFXConfig.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string_view>

#include <glm/mat4x4.hpp>

namespace game::runtime::render_model {
struct MeshData;
}

namespace game::runtime::shared_backend_pose {
struct PoseEval;
}

namespace game::runtime::shared_tail_fire_coordinator {

struct AnchorExportArgs {
    int unitId = 0;
    const render_model::MeshData* mesh = nullptr;
    const shared_backend_pose::PoseEval* scenePose = nullptr;
    float resolvedScaleCorrection = 1.0f;
    const TailFireVFXConfig* config = nullptr;
    std::function<glm::mat4(int)> worldMatrixForNode;
    std::function<int(std::string_view)> resolveNamedNodeIndex;
    bool logDebug = false;
};

bool speciesUsesTailFireMeshPlayback(std::string_view species);
bool unitUsesTailFireMeshPlayback(const PokemonInstance& unit);
const std::array<std::string_view, 2>& playbackSpeciesOrder();
std::string_view primaryPlaybackSpecies();
const TailFireVFXConfig& resolvePlaybackConfig(std::string_view species);
const TailFireVFXConfig& resolvePrimaryPlaybackConfig();
const std::array<shared_tail_fire_mesh_playback::FlipbookSpec, 2>& authoredFlipbookSpecs();
const shared_tail_fire_mesh_playback::FlipbookSpec* resolvePrimaryAuthoredFlipbookSpec();
bool backendUsesAuthoredMeshPlayback(const char* backendId);
bool backendUsesGpuClipSkinning(const char* backendId, std::string_view species);
const shared_tail_fire_mesh_playback::Profile* resolvePlaybackProfile(
    std::string_view species,
    const render_model::MeshData* mesh);
bool baseSubmeshUsesAuthoredFire(
    std::size_t baseSubmeshIndex,
    const shared_tail_fire_mesh_playback::Profile* profile);
bool exportPlaybackAnchor(
    const AnchorExportArgs& args,
    shared_tail_fire_fallback::Anchor& outAnchor);

} // namespace game::runtime::shared_tail_fire_coordinator
