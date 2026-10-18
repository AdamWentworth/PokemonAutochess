#pragma once

#include "game/render/materials/character/MaterialModes.h"
#include "engine/models/ModelData.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include "engine/render/ModelAnimationTypes.h"

namespace game::runtime::render_model {

using pokemon::models::kDielectricMaskMaterialFlag;
using pokemon::models::kLayeredCharacterMaterialMode;
using pokemon::models::kNativeAnimatedEyeClearCoatMaterialMode;
using pokemon::models::kNativeAnimatedEyeMaterialMode;
using pokemon::models::kNativeDepthOverlayClipSpaceBias;
using pokemon::models::kNativeDepthOverlayMaterialFlagBit;
using pokemon::models::kNativeEyeClearCoatMaterialMode;
using pokemon::models::kNativeFacialOverlayMaterialMode;
using pokemon::models::kNativeFrontFacingOnlyMaterialFlagBit;
using pokemon::models::kNativeIkCharacterSurfaceDefault;
using pokemon::models::kNativeLayeredUnlitMaterialMode;
using pokemon::models::kRefractiveEyeMaterialMode;
using pokemon::models::kSubsurfaceDefault;
using pokemon::models::kSubsurfaceFibre;
using pokemon::models::kSubsurfaceMaterialMode;
using pokemon::models::kViewAngleLayerMaterialMode;

using engine::models::AnimatedBillboard;
using engine::models::CachedTextureRgba;
using engine::models::ContinuousMaterialAnimationTrack;
using engine::models::MaterialAnimationCurve;
using engine::models::MaterialAnimationKey;
using engine::models::MaterialAnimationParameter;
using engine::models::MaterialAnimationSampling;
using engine::models::MeshData;
using engine::models::MeshVertex;
using engine::models::MeshVisibilityTrack;

std::string cachePathForModel(const std::string& modelPath);
bool loadLegacyMeshFromCache(
    const std::string& modelPath,
    MeshData& out,
    std::string* outError = nullptr);
// Resolve a source identity to its cooked PHLO object. Canonical .phmodel
// identities are strictly offline inputs and never fall back to source decode
// or legacy .pacmdl caches. Retained GLB compatibility identities can still
// use the legacy cache only when strict cooked mode is not requested.
bool loadMeshFromCache(const std::string& modelPath, MeshData& out, std::string* outError = nullptr);

// Returns the bind-model-space height of the authored support surface.
// Character tails, shells, wings, and effect meshes can extend below the
// feet. Game Freak rigs may also expose unweighted EffFoot anchors, so raw
// geometry bounds are only the final fallback.
float modelSupportContactY(const MeshData& mesh);

} // namespace game::runtime::render_model
