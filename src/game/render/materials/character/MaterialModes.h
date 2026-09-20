#pragma once

#include <cstdint>

namespace game::render::materials {

// Opt-in marker for ordinary PBR materials whose metallic/roughness alpha
// stores a source-authored dielectric specular mask. materialRect0U carries
// that source shader's scalar specular intensity.
inline constexpr float kDielectricMaskMaterialFlag = 5.0f;
inline constexpr std::uint8_t kLayeredCharacterMaterialMode = 32u;
inline constexpr std::uint8_t kSubsurfaceMaterialMode = 33u;
inline constexpr float kSubsurfaceDefault = 0.0f;
inline constexpr float kSubsurfaceFibre = 1.0f;
// Scarlet/Violet's FresnelEffect combines ordinary lit base color with an
// additive, view-angle-gated second color layer and a local reflection probe.
// Keep it out of generic PBR so the authored fifth-power Fresnel response and
// the second map's linear sampling contract survive every backend.
inline constexpr std::uint8_t kViewAngleLayerMaterialMode = 34u;
// Z-A's IkCharacter eye variant keeps live iris parallax/refraction,
// eyelid-shadow, highlight, and local-reflection inputs alongside the body
// program's color process.
inline constexpr std::uint8_t kRefractiveEyeMaterialMode = 35u;

} // namespace game::render::materials
