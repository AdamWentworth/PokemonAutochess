#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include "engine/render/ModelAnimationTypes.h"

namespace game::runtime::render_model {

// Runtime material modes below 27 are currently owned by Phlosion's core PBR,
// VFX, and LGPE environment paths.  This mode is source-agnostic: it represents
// a skinned, layered, unlit material whose authored displacement texture shapes
// its vertices while its native skeleton/material tracks own animation.
// Scarlet's Charmander flame is the first consumer; Ponyta and other native
// animated materials can use the same contract.
inline constexpr std::uint8_t kNativeLayeredUnlitMaterialMode = 27u;

// Game Freak's EyeClearCoat is not a generic metallic/roughness material.
// It combines authored layer masks, layer-local surface response, emission,
// and a dielectric clear-coat lobe.  Keep it distinct so backends can retain
// those semantics without making every PBR material eye-specific.
// params0 = coat roughness, highlight roughness, highlight metallic, enabled;
// params1 = clear-coat base RGB and coat metallic (-1 marks PLA plain Eye);
// params3.yzw = layer-5 emission RGB multiplied by authored intensity.
// params3.x remains reserved for qualified facial-shell depth ordering.
inline constexpr std::uint8_t kNativeEyeClearCoatMaterialMode = 28u;
// Clip-bound native eye UV animation. Mode 29 keeps the existing generic PBR
// response; mode 30 keeps the dedicated native clear-coat response. Both
// reserve two packed material slots for the source UV offset selected by the
// active body clip.
inline constexpr std::uint8_t kNativeAnimatedEyeMaterialMode = 29u;
inline constexpr std::uint8_t kNativeAnimatedEyeClearCoatMaterialMode = 30u;
// Source character shaders can layer an opaque facial shell ahead of a
// displaced outer volume without changing the shell's projected position.
// Mode 31 carries a small clip-space depth bias in materialParams0.x so all
// backends preserve that ordering without physically moving skinned vertices.
inline constexpr std::uint8_t kNativeFacialOverlayMaterialMode = 31u;

// Z-A's ordinary IkCharacter body shader is a stylized layered material, not
// generic metallic/roughness PBR. Its cooked auxiliary maps carry authored
// shadow color, specular strength/shape, metallic, AO, and rim controls;
// materialParams0.xy retain ReflectionsBlur and DiffusionLevels. Keeping a
// distinct mode prevents fur, stone, shell, plastic, and metal from collapsing
// into one invented roughness value.
inline constexpr std::uint8_t kNativeIkCharacterMaterialMode = 32u;

// Exact source-surface qualifiers carried by IkCharacter params0.z. Zero is
// deliberately neutral so a coat/feather response is never inferred from a
// generic body material.
inline constexpr float kNativeIkCharacterSurfaceDefault = 0.0f;
inline constexpr float kNativeIkCharacterSurfaceFibre = 1.0f;
inline constexpr float kNativeIkCharacterSurfaceFeather = 2.0f;

// Scarlet/Violet's SSS body family is deliberately softer than generic
// metallic/roughness PBR. Exact compiled-program differentials prove that its
// scalar roughness atlas is sampled alongside tangent-space normal, AO, base
// color, and SSS mask inputs. Keep that source contract distinct; an optional
// fibre/velvet response remains a narrowly qualified Phlosion reconstruction,
// never a property inferred for every SSS material.
inline constexpr std::uint8_t kNativeSssMaterialMode = 33u;
inline constexpr float kNativeSssSurfaceDefault = 0.0f;
inline constexpr float kNativeSssSurfaceFibre = 1.0f;

// Ordinary Z-A IkCharacter body materials carry per-pixel specular strength
// in the alpha channel of the cooked metallic/roughness texture. The source
// SpecularIntensity remains in materialParams0.x. Keep this as an explicit
// opt-in so standard glTF metallic/roughness alpha remains ignored.
inline constexpr float kNativeSpecularStrengthMaterialFlag = 5.0f;

// Bit 5 is a CPU submission qualifier rather than a shader surface input.
// Some source meshes use a front-facing shell even though Phlosion keeps its
// shared world pipeline globally two-sided for renderer parity. The qualifier
// lets those narrowly proven shells reject rear-facing triangles without
// changing culling for older GLB/native content. Shader PBR flags occupy only
// bits 0-4, so the value can coexist with their texture-presence mask.
inline constexpr std::uint32_t kNativeFrontFacingOnlyMaterialFlagBit = 1u << 5u;

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
    std::uint16_t j0 = 0u;
    std::uint16_t j1 = 0u;
    std::uint16_t j2 = 0u;
    std::uint16_t j3 = 0u;
    float w0 = 0.0f;
    float w1 = 0.0f;
    float w2 = 0.0f;
    float w3 = 0.0f;
};

struct CachedTextureRgba {
    int width = 0;
    int height = 0;
    int wrapS = 10497;
    int wrapT = 10497;
    int minF = 9729;
    int magF = 9729;
    std::vector<unsigned char> rgba;

    bool hasPixels() const {
        if (width <= 0 || height <= 0) return false;
        const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        const std::uint64_t requiredBytes = pixels * 4ull;
        return requiredBytes > 0ull && requiredBytes <= static_cast<std::uint64_t>(rgba.size());
    }
};

struct MeshVisibilityTrack {
    int nodeIndex = -1;
    float sourceFrameRate = 0.0f;
    std::vector<float> inputs;
    std::vector<std::uint8_t> values;
};

struct MaterialAnimationKey {
    float timeSec = 0.0f;
    float value = 0.0f;
};

struct MaterialAnimationCurve {
    std::vector<MaterialAnimationKey> keys;
};

enum class MaterialAnimationParameter : std::uint8_t {
    UvScaleOffset,
    UvScaleOffset3,
};

enum class MaterialAnimationSampling : std::uint8_t {
    Linear,
    HoldSourceFrame,
};

// An always-running source material track, independent of the selected body
// clip. Game Freak uses these for effects such as fire: every component keeps
// its original key times and values instead of being reduced to a guessed
// scroll rate.
struct ContinuousMaterialAnimationTrack {
    std::size_t submeshIndex = 0u;
    MaterialAnimationParameter parameter =
        MaterialAnimationParameter::UvScaleOffset;
    float durationSec = 0.0f;
    float sourceFrameRate = 0.0f;
    bool loop = false;
    MaterialAnimationSampling sampling =
        MaterialAnimationSampling::Linear;
    glm::vec4 defaultValue{1.0f, 1.0f, 0.0f, 0.0f};
    std::array<MaterialAnimationCurve, 4u> components;
};

struct MeshData {
    std::string assetCacheIdentity;
    float modelScaleFactor = 1.0f;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> triangleSubmesh;
    std::vector<glm::vec3> triangleBaseColors;
    std::vector<float> triangleOpacity;
    std::vector<std::uint8_t> triangleDoubleSided;
    std::vector<glm::vec4> submeshBaseColors;
    std::vector<int> submeshMeshIndex;
    std::vector<std::uint32_t> submeshIndexOffset;
    std::vector<std::uint32_t> submeshIndexCount;
    std::vector<CachedTextureRgba> submeshBaseTextures;
    std::vector<CachedTextureRgba> submeshNormalTextures;
    std::vector<CachedTextureRgba> submeshMetallicRoughnessTextures;
    std::vector<CachedTextureRgba> submeshOcclusionTextures;
    std::vector<CachedTextureRgba> submeshEmissiveTextures;
    std::vector<std::uint8_t> submeshAlphaMode;
    std::vector<float> submeshAlphaCutoff;
    std::vector<float> submeshNormalScale;
    std::vector<float> submeshMetallicFactor;
    std::vector<float> submeshRoughnessFactor;
    std::vector<float> submeshOcclusionStrength;
    std::vector<glm::vec3> submeshEmissiveFactors;
    std::vector<std::uint8_t> submeshMaterialModes;
    std::vector<float> submeshMaterialFlags;
    std::vector<glm::vec4> submeshMaterialParams0;
    std::vector<glm::vec4> submeshMaterialParams1;
    std::vector<glm::vec4> submeshMaterialParams2;
    std::vector<glm::vec4> submeshMaterialParams3;
    std::vector<int> meshIndexToNode;
    std::vector<int> triangleNodeIndex;
    std::vector<int> triangleSkinIndex;
    std::vector<glm::vec3> vertexBaseColors;
    std::vector<engine::render::model_types::NodeTRS> nodesDefault;
    std::vector<std::string> nodeNames;
    std::vector<std::vector<int>> nodeChildren;
    std::vector<int> nodeParent;
    std::vector<int> nodeMesh;
    std::vector<int> nodeSkin;
    std::vector<int> sceneRoots;
    std::vector<glm::mat4> bindNodeGlobals;
    std::vector<engine::render::model_types::SkinData> skins;
    std::vector<engine::render::model_types::AnimationClip> animations;
    // Parallel to animations. These source-authored step tracks control
    // renderable mesh nodes without coercing visibility into skeletal TRS.
    std::vector<std::vector<MeshVisibilityTrack>> animationMeshVisibility;
    // Parallel to animations. Unlike continuousMaterialAnimations, these
    // tracks are sampled with the selected skeletal clip and its clip time.
    // Game Freak uses them for blinks, pupil motion, and expression shapes.
    std::vector<std::vector<ContinuousMaterialAnimationTrack>>
        animationMaterialParameters;
    std::vector<ContinuousMaterialAnimationTrack>
        continuousMaterialAnimations;
    bool hasVertexColor = false;
    bool hasVertexBaseColor = false;
};

std::string cachePathForModel(const std::string& modelPath);
bool loadLegacyMeshFromCache(
    const std::string& modelPath,
    MeshData& out,
    std::string* outError = nullptr);
bool loadMeshFromCache(const std::string& modelPath, MeshData& out, std::string* outError = nullptr);

// Returns the bind-model-space height of the authored support surface.
// Character tails, shells, wings, and effect meshes can extend below the
// feet. Game Freak rigs may also expose unweighted EffFoot anchors, so raw
// geometry bounds are only the final fallback.
float modelSupportContactY(const MeshData& mesh);

} // namespace game::runtime::render_model
