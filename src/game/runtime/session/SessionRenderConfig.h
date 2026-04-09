#pragma once

#include <cstddef>

class IRenderBackend;

namespace game::runtime::startup_asset_prewarm {
enum class AuthoredVfxKind : int;
}

namespace game::runtime::session_render_config {

std::size_t backendModelTriangleLimit();
std::size_t backendModelTriangleFrameBudget();

bool backendModelBackfaceCullingEnabled();
bool backendWorldPortraitFallbackEnabled();
bool backendWorldPortraitOverlayForced();
bool backendPreloadModelCacheEnabled();
bool backendPrewarmAnimRolesEnabled();
bool backendPrewarmModelTexturesEnabled();
bool backendPrewarmModelGeometryEnabled();
bool snapshotPrewarmRestoreRenderEnabled();
bool backendModelFullMeshEnabled();
bool backendModelVerboseLoggingEnabled();
bool backendModelFastTexturedPathEnabled();
bool backendUiSpritePrewarmEnabled();
bool backendWorldLayerPrewarmEnabled();
bool backendPrewarmAuthoredVfxEnabled(startup_asset_prewarm::AuthoredVfxKind kind);
bool backendPrewarmGrowlVfxEnabled();
bool backendPrewarmTackleVfxEnabled();
bool backendPrewarmParticleVfxEnabled();
bool backendGpuClipSkinningEnabled(const IRenderBackend* renderer);
bool backendUseLegacyGrowlWaveVfxEnabled();
bool backendUseLegacyParticleVfxSnapshotBridgeEnabled();
bool backendUseExactTailFireCpuPathEnabled();
bool backendPrewarmLegacyTailFirePremulEnabled();

void resetForTests();

} // namespace game::runtime::session_render_config
