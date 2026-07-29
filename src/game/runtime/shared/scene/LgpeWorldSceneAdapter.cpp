#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"

#include "engine/render/LgpeFieldCliffMaterial.h"
#include "engine/render/LgpeFieldFlowerMaterial.h"
#include "engine/render/LgpeFieldOverlayMaterial.h"
#include "engine/render/LgpeFieldGrassMaterial.h"
#include "engine/render/LgpeFieldGroundMaterial.h"
#include "engine/render/LgpeFieldRockMaterial.h"
#include "engine/render/LgpeFieldSignMaterial.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "engine/render/LgpeFieldObjectTreeMikiMaterial.h"
#include "engine/render/LgpeFieldTree02Material.h"
#include "engine/render/LgpeFieldTree05Material.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace game::runtime::lgpe_world_scene {
namespace {

using Family = IRenderBackend::WorldSceneSourceMaterialFamily;
using Json = nlohmann::json;

bool fail(std::string* outError, std::string message) {
    if (outError) *outError = std::move(message);
    return false;
}

int wrapMode(std::string_view value) {
    if (value == "Clamp" || value == "ClampToEdge") return 33071;
    if (value == "Mirror" || value == "MirroredRepeat") return 33648;
    return 10497;
}

void buildMipStorage(const engine::assets::lgpe::Texture& texture,
                     TextureStorage& storage) {
    std::vector<const engine::assets::lgpe::TextureSubresource*> authored;
    authored.reserve(texture.subresources.size());
    for (const auto& subresource : texture.subresources) {
        if (subresource.arrayLevel == 0u && subresource.depthLevel == 0u) {
            authored.push_back(&subresource);
        }
    }
    std::sort(
        authored.begin(),
        authored.end(),
        [](const auto* lhs, const auto* rhs) {
            return lhs->mipLevel < rhs->mipLevel;
        });

    storage.mipRgba.clear();
    storage.mipLevels.clear();
    storage.mipRgba.reserve(authored.size());
    for (std::size_t level = 0u; level < authored.size(); ++level) {
        const auto& source = *authored[level];
        const std::size_t expectedBytes =
            static_cast<std::size_t>(source.width) *
            static_cast<std::size_t>(source.height) * 4u;
        if (source.mipLevel != level || source.width == 0u ||
            source.height == 0u || source.rgba8.size() != expectedBytes) {
            break;
        }
        storage.mipRgba.emplace_back(source.rgba8.begin(), source.rgba8.end());
    }

    storage.mipLevels.reserve(storage.mipRgba.size());
    for (std::size_t level = 0u; level < storage.mipRgba.size(); ++level) {
        const auto& source = *authored[level];
        const auto& pixels = storage.mipRgba[level];
        storage.mipLevels.push_back({
            pixels.empty() ? nullptr : pixels.data(),
            static_cast<int>(source.width),
            static_cast<int>(source.height)});
    }
}

std::string authoredMipCacheKey(
    const std::string& textureKey,
    const IRenderBackend::WorldSceneSourceTextureBinding& binding) {
    return textureKey + ":authored-mips:" +
           std::to_string(binding.mipLevelCount);
}

std::uint32_t semanticMask(const engine::assets::lgpe::Mesh& mesh) {
    std::uint32_t mask =
        engine::render::backend::WorldSceneSourceVertexSemanticNormalW |
        engine::render::backend::WorldSceneSourceVertexSemanticBitangent;
    for (const auto& attribute : mesh.attributes) {
        if (attribute.semanticHint == "TEXCOORD_1") {
            mask |=
                engine::render::backend::WorldSceneSourceVertexSemanticTexCoord1;
        } else if (attribute.semanticHint == "TEXCOORD_2") {
            mask |=
                engine::render::backend::WorldSceneSourceVertexSemanticTexCoord2;
        } else if (attribute.semanticHint == "TEXCOORD_3") {
            mask |=
                engine::render::backend::WorldSceneSourceVertexSemanticTexCoord3;
        } else if (attribute.semanticHint == "COLOR_1") {
            mask |= engine::render::backend::WorldSceneSourceVertexSemanticColor1;
        } else if (attribute.semanticHint == "COLOR_2") {
            mask |= engine::render::backend::WorldSceneSourceVertexSemanticColor2;
        } else if (attribute.semanticHint == "COLOR_3") {
            mask |= engine::render::backend::WorldSceneSourceVertexSemanticColor3;
        }
    }
    return mask;
}

std::uint32_t knownSwitchBit(std::string_view name) {
    using namespace engine::render::backend;
    if (name == "SkipMainRendering") {
        return WorldSceneSourceMaterialSwitchSkipMainRendering;
    }
    if (name == "DepthWrite") return WorldSceneSourceMaterialSwitchDepthWrite;
    if (name == "DepthTest") return WorldSceneSourceMaterialSwitchDepthTest;
    if (name == "DiscardEnable") {
        return WorldSceneSourceMaterialSwitchDiscardEnable;
    }
    if (name == "TextureAlphaTestEnable") {
        return WorldSceneSourceMaterialSwitchTextureAlphaTestEnable;
    }
    if (name == "CastShadow") return WorldSceneSourceMaterialSwitchCastShadow;
    if (name == "ReceiveShadow") {
        return WorldSceneSourceMaterialSwitchReceiveShadow;
    }
    return WorldSceneSourceMaterialSwitchNone;
}

void parseSourceSwitches(const engine::assets::lgpe::Material& source,
                         IRenderBackend::WorldSceneMaterial& out) {
    try {
        const Json metadata = Json::parse(source.sourceMetadataJson);
        const auto parseSwitchArray = [&out](const Json& container) {
            const auto switches = container.find("Switches");
            if (switches == container.end() || !switches->is_array()) return;
            for (const Json& entry : *switches) {
                const std::uint32_t bit =
                    knownSwitchBit(entry.value("Name", std::string{}));
                if (bit == 0u) continue;
                out.sourceSwitchMask |= bit;
                if (entry.value("Value", false)) {
                    out.sourceEnabledSwitchMask |= bit;
                }
            }
        };
        parseSwitchArray(metadata);
        const auto common = metadata.find("Common");
        if (common != metadata.end() && common->is_object()) {
            parseSwitchArray(*common);
        }
    } catch (const std::exception&) {
        // The canonical loader already validates JSON syntax. Retaining the
        // exact metadata string is sufficient if a future schema changes the
        // structure of Switches.
    }

    using namespace engine::render::backend;
    if (source.skipMainRendering) {
        out.sourceSwitchMask |=
            WorldSceneSourceMaterialSwitchSkipMainRendering;
        out.sourceEnabledSwitchMask |=
            WorldSceneSourceMaterialSwitchSkipMainRendering;
    }
}

bool sourceColor(const std::string& metadataJson,
                 std::string_view name,
                 std::array<float, 3>& out) {
    try {
        const Json metadata = Json::parse(metadataJson);
        const auto colors = metadata.find("Colors");
        if (colors == metadata.end() || !colors->is_array()) return false;
        for (const Json& entry : *colors) {
            if (entry.value("Name", std::string{}) != name) continue;
            const auto color = entry.find("Color");
            if (color == entry.end() || !color->is_object()) return false;
            out = {
                color->value("R", 0.0f),
                color->value("G", 0.0f),
                color->value("B", 0.0f)};
            return true;
        }
    } catch (const std::exception&) {
    }
    return false;
}

bool sourceValue(const std::string& metadataJson,
                 std::string_view containerName,
                 std::string_view name,
                 float& out) {
    try {
        const Json metadata = Json::parse(metadataJson);
        const Json* container = &metadata;
        if (!containerName.empty()) {
            const auto found = metadata.find(containerName);
            if (found == metadata.end() || !found->is_object()) return false;
            container = &*found;
        }
        const auto values = container->find("Values");
        if (values == container->end() || !values->is_array()) return false;
        for (const Json& entry : *values) {
            if (entry.value("Name", std::string{}) != name) continue;
            out = entry.value("Value", 0.0f);
            return true;
        }
    } catch (const std::exception&) {
    }
    return false;
}

bool sourceSwitch(const std::string& metadataJson,
                  std::string_view containerName,
                  std::string_view name,
                  bool& out) {
    try {
        const Json metadata = Json::parse(metadataJson);
        const Json* container = &metadata;
        if (!containerName.empty()) {
            const auto found = metadata.find(containerName);
            if (found == metadata.end() || !found->is_object()) return false;
            container = &*found;
        }
        const auto switches = container->find("Switches");
        if (switches == container->end() || !switches->is_array()) {
            return false;
        }
        for (const Json& entry : *switches) {
            if (entry.value("Name", std::string{}) != name) continue;
            out = entry.value("Value", false);
            return true;
        }
    } catch (const std::exception&) {
    }
    return false;
}

const IRenderBackend::WorldSceneSourceTextureBinding* sourceBinding(
    const IRenderBackend::WorldSceneMaterial& material,
    std::string_view samplerName) {
    const auto found = std::find_if(
        material.sourceTextureBindings.begin(),
        material.sourceTextureBindings.end(),
        [samplerName](const auto& binding) {
            return binding.samplerName == samplerName &&
                   binding.baseRgba &&
                   binding.baseWidth > 0 &&
                   binding.baseHeight > 0;
        });
    return found == material.sourceTextureBindings.end() ? nullptr : &*found;
}

bool uniformOpaqueWhite(
    const IRenderBackend::WorldSceneSourceTextureBinding& binding) {
    if (!binding.baseRgba || binding.baseWidth <= 0 ||
        binding.baseHeight <= 0) {
        return false;
    }
    const std::size_t pixelCount =
        static_cast<std::size_t>(binding.baseWidth) *
        static_cast<std::size_t>(binding.baseHeight);
    for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel) {
        const std::size_t offset = pixel * 4u;
        if (binding.baseRgba[offset + 0u] != 255u ||
            binding.baseRgba[offset + 1u] != 255u ||
            binding.baseRgba[offset + 2u] != 255u ||
            binding.baseRgba[offset + 3u] != 255u) {
            return false;
        }
    }
    return true;
}

bool route1GroundCliffSharedLightingContract(
    const IRenderBackend::WorldSceneMaterial& material,
    const IRenderBackend::WorldSceneSourceTextureBinding* shadowToon,
    const IRenderBackend::WorldSceneSourceTextureBinding* lightProjection) {
    std::array<float, 3> shadowColor{};
    float lightProjectionColorPower = 0.0f;
    bool cloudEnabled = false;
    if (!shadowToon || !lightProjection ||
        !uniformOpaqueWhite(*shadowToon) ||
        !sourceColor(
            material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapColorPow",
            lightProjectionColorPower) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !cloudEnabled ||
        std::abs(lightProjectionColorPower - 1.0f) > 0.0001f) {
        return false;
    }
    for (std::size_t channel = 0u; channel < shadowColor.size(); ++channel) {
        if (std::abs(
                shadowColor[channel] -
                engine::render::lgpe_field_shared::kShadowColor[channel]) >
            0.0001f) {
            return false;
        }
    }
    return true;
}

std::string sourceTextureKey(std::string_view profileId,
                             const IRenderBackend::WorldSceneSourceTextureBinding& binding) {
    return "lgpe:" + std::string(profileId) + ":" + binding.textureName +
           ":" + binding.samplerName;
}

void assignBaseTexture(
    std::string_view profileId,
    const IRenderBackend::WorldSceneSourceTextureBinding& binding,
    IRenderBackend::WorldSceneMaterial& material) {
    material.textureKey = sourceTextureKey(profileId, binding);
    material.textureCacheKey = authoredMipCacheKey(material.textureKey, binding);
    material.textureRgba = binding.baseRgba;
    material.textureWidth = binding.baseWidth;
    material.textureHeight = binding.baseHeight;
    material.textureMipLevels = binding.mipLevels;
    material.textureMipLevelCount = binding.mipLevelCount;
    material.textureWrapS = binding.resolvedWrapS;
    material.textureWrapT = binding.resolvedWrapT;
    material.textureSrgb = binding.sourceIsSrgb ? 1u : 0u;
}

void assignNormalSlot(
    std::string_view profileId,
    const IRenderBackend::WorldSceneSourceTextureBinding& binding,
    IRenderBackend::WorldSceneMaterial& material) {
    material.normalTextureKey = sourceTextureKey(profileId, binding);
    material.normalTextureCacheKey =
        authoredMipCacheKey(material.normalTextureKey, binding);
    material.normalTextureRgba = binding.baseRgba;
    material.normalTextureWidth = binding.baseWidth;
    material.normalTextureHeight = binding.baseHeight;
    material.normalTextureMipLevels = binding.mipLevels;
    material.normalTextureMipLevelCount = binding.mipLevelCount;
    material.normalTextureWrapS = binding.resolvedWrapS;
    material.normalTextureWrapT = binding.resolvedWrapT;
    material.normalTextureSrgb = binding.sourceIsSrgb ? 1u : 0u;
}

void assignMetallicRoughnessSlot(
    std::string_view profileId,
    const IRenderBackend::WorldSceneSourceTextureBinding& binding,
    IRenderBackend::WorldSceneMaterial& material) {
    material.metallicRoughnessTextureKey = sourceTextureKey(profileId, binding);
    material.metallicRoughnessTextureCacheKey =
        authoredMipCacheKey(material.metallicRoughnessTextureKey, binding);
    material.metallicRoughnessTextureRgba = binding.baseRgba;
    material.metallicRoughnessTextureWidth = binding.baseWidth;
    material.metallicRoughnessTextureHeight = binding.baseHeight;
    material.metallicRoughnessTextureMipLevels = binding.mipLevels;
    material.metallicRoughnessTextureMipLevelCount = binding.mipLevelCount;
    material.metallicRoughnessTextureWrapS = binding.resolvedWrapS;
    material.metallicRoughnessTextureWrapT = binding.resolvedWrapT;
    material.metallicRoughnessTextureSrgb = binding.sourceIsSrgb ? 1u : 0u;
}

void assignOcclusionSlot(
    std::string_view profileId,
    const IRenderBackend::WorldSceneSourceTextureBinding& binding,
    IRenderBackend::WorldSceneMaterial& material) {
    material.occlusionTextureKey = sourceTextureKey(profileId, binding);
    material.occlusionTextureCacheKey =
        authoredMipCacheKey(material.occlusionTextureKey, binding);
    material.occlusionTextureRgba = binding.baseRgba;
    material.occlusionTextureWidth = binding.baseWidth;
    material.occlusionTextureHeight = binding.baseHeight;
    material.occlusionTextureMipLevels = binding.mipLevels;
    material.occlusionTextureMipLevelCount = binding.mipLevelCount;
    material.occlusionTextureWrapS = binding.resolvedWrapS;
    material.occlusionTextureWrapT = binding.resolvedWrapT;
    material.occlusionTextureSrgb = binding.sourceIsSrgb ? 1u : 0u;
}

void assignEmissiveSlot(
    std::string_view profileId,
    const IRenderBackend::WorldSceneSourceTextureBinding& binding,
    IRenderBackend::WorldSceneMaterial& material) {
    material.emissiveTextureKey = sourceTextureKey(profileId, binding);
    material.emissiveTextureCacheKey =
        authoredMipCacheKey(material.emissiveTextureKey, binding);
    material.emissiveTextureRgba = binding.baseRgba;
    material.emissiveTextureWidth = binding.baseWidth;
    material.emissiveTextureHeight = binding.baseHeight;
    material.emissiveTextureMipLevels = binding.mipLevels;
    material.emissiveTextureMipLevelCount = binding.mipLevelCount;
    material.emissiveTextureWrapS = binding.resolvedWrapS;
    material.emissiveTextureWrapT = binding.resolvedWrapT;
    material.emissiveTextureSrgb = binding.sourceIsSrgb ? 1u : 0u;
}

void assignEnvironmentSlot(
    std::string_view profileId,
    const IRenderBackend::WorldSceneSourceTextureBinding& binding,
    IRenderBackend::WorldSceneMaterial& material) {
    material.environmentTextureKey = sourceTextureKey(profileId, binding);
    material.environmentTextureCacheKey =
        authoredMipCacheKey(material.environmentTextureKey, binding);
    material.environmentTextureRgba = binding.baseRgba;
    material.environmentTextureWidth = binding.baseWidth;
    material.environmentTextureHeight = binding.baseHeight;
    material.environmentTextureMipLevels = binding.mipLevels;
    material.environmentTextureMipLevelCount = binding.mipLevelCount;
    material.environmentTextureWrapS = binding.resolvedWrapS;
    material.environmentTextureWrapT = binding.resolvedWrapT;
    material.environmentTextureSrgb = binding.sourceIsSrgb ? 1u : 0u;
}

void assignLightProjectionSlot(
    std::string_view profileId,
    const IRenderBackend::WorldSceneSourceTextureBinding& binding,
    IRenderBackend::WorldSceneMaterial& material) {
    material.lightProjectionTextureKey = sourceTextureKey(profileId, binding);
    material.lightProjectionTextureCacheKey =
        authoredMipCacheKey(material.lightProjectionTextureKey, binding);
    material.lightProjectionTextureRgba = binding.baseRgba;
    material.lightProjectionTextureWidth = binding.baseWidth;
    material.lightProjectionTextureHeight = binding.baseHeight;
    material.lightProjectionTextureMipLevels = binding.mipLevels;
    material.lightProjectionTextureMipLevelCount = binding.mipLevelCount;
    material.lightProjectionTextureWrapS = binding.resolvedWrapS;
    material.lightProjectionTextureWrapT = binding.resolvedWrapT;
    material.lightProjectionTextureSrgb = binding.sourceIsSrgb ? 1u : 0u;
}

bool configureFieldGroundSurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    if (material.sourceShaderGroup != "FieldGroundShader01") return false;

    const auto* ground01 = sourceBinding(material, "GroundTex01");
    const auto* ground02 = sourceBinding(material, "GroundTex02");
    const auto* grass02 = sourceBinding(material, "GrassTex02");
    const auto* grass01 = sourceBinding(material, "GrassTex01");
    const auto* blend = sourceBinding(material, "BlendTex");
    const auto* grassBlend = sourceBinding(material, "GrassBlendTex");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    std::array<float, 3> alphaLight{};
    if (!ground01 || !ground02 || !grass02 || !grass01 || !blend ||
        !grassBlend ||
        !sourceColor(material.sourceMetadataJson, "Alpha_light", alphaLight) ||
        !route1GroundCliffSharedLightingContract(
            material, shadowToon, lightProjection)) {
        return false;
    }

    // Six existing renderer descriptors become a typed private contract for
    // material mode 4. Their generic names are not material semantics.
    assignBaseTexture(profileId, *ground01, material);
    assignNormalSlot(profileId, *ground02, material);
    assignMetallicRoughnessSlot(profileId, *grass02, material);
    assignOcclusionSlot(profileId, *grass01, material);
    assignEmissiveSlot(profileId, *blend, material);
    assignEnvironmentSlot(profileId, *grassBlend, material);
    assignLightProjectionSlot(profileId, *lightProjection, material);
    material.emissiveFactorR = alphaLight[0];
    material.emissiveFactorG = alphaLight[1];
    material.emissiveFactorB = alphaLight[2];
    material.alphaMode = 0u;
    material.materialMode =
        engine::render::lgpe_field_ground::kMaterialMode;
    return true;
}

bool configureFieldCliffSurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    if (material.sourceShaderGroup != "FieldCliffShader01") return false;

    const auto* cliff = sourceBinding(material, "CliffTex01");
    const auto* ground02 = sourceBinding(material, "GroundTex02");
    const auto* ground01 = sourceBinding(material, "GroundTex01");
    const auto* border = sourceBinding(material, "BorderTex");
    const auto* blend = sourceBinding(material, "BlendTex");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    std::array<float, 3> rimColor{};
    float groundBlend = 0.0f;
    float tex01Uv = 0.0f;
    float tex00Uv = 0.0f;
    float mipMapBias = 0.0f;
    float rimMin = 0.0f;
    float rimMax = 0.0f;
    float rimStrength = 0.0f;
    if (!cliff || !ground02 || !ground01 || !border || !blend ||
        !route1GroundCliffSharedLightingContract(
            material, shadowToon, lightProjection) ||
        !sourceColor(material.sourceMetadataJson, "RimColor", rimColor) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "GroundBlend", groundBlend) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "Tex01_UV", tex01Uv) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "Tex00_UV", tex00Uv) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Min", rimMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Max", rimMax) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Strength", rimStrength) ||
        std::abs(groundBlend - 2.0f) > 0.0001f ||
        std::abs(tex01Uv - 1.0f) > 0.0001f ||
        std::abs(tex00Uv) > 0.0001f ||
        std::abs(mipMapBias + 2.0f) > 0.0001f) {
        return false;
    }

    // Five existing renderer descriptors become a typed private contract for
    // material mode 5. Their generic names are not material semantics.
    assignBaseTexture(profileId, *cliff, material);
    assignNormalSlot(profileId, *ground02, material);
    assignMetallicRoughnessSlot(profileId, *ground01, material);
    assignOcclusionSlot(profileId, *blend, material);
    assignEmissiveSlot(profileId, *border, material);
    assignLightProjectionSlot(profileId, *lightProjection, material);
    material.emissiveFactorR = rimColor[0];
    material.emissiveFactorG = rimColor[1];
    material.emissiveFactorB = rimColor[2];
    material.normalScale = rimMin;
    material.metallicFactor = rimMax;
    material.roughnessFactor = rimStrength;
    material.alphaMode = 0u;
    material.materialMode =
        engine::render::lgpe_field_cliff::kMaterialMode;
    return true;
}

bool configureFieldGrassSurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    using namespace engine::render::backend;
    const bool isShader01 =
        material.sourceShaderGroup == "FieldGrassShader01" &&
        material.sourceMaterialName == "grass01_com_002";
    const bool isShader02 =
        material.sourceShaderGroup == "FieldGrassShader02" &&
        (material.sourceMaterialName == "grass01_com" ||
         material.sourceMaterialName == "grass01_com_001");
    if (!isShader01 && !isShader02) return false;

    const auto* textureMap01 = sourceBinding(material, "TextureMap01");
    const auto* textureMap02 = sourceBinding(material, "TextureMap02");
    const auto* greenBlend = sourceBinding(material, "green_blend");
    const auto* highlight = sourceBinding(material, "Hilight");
    const auto* greenHikari = sourceBinding(material, "green_hikari");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> color{};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> onGameColor{1.0f, 1.0f, 1.0f};
    float discard = 0.0f;
    float rimMin = 0.0f;
    float rimMax = 1.0f;
    float rimStrength = 0.0f;
    float shadowSamplingScale = 0.0f;
    float shadowBias = 0.0f;
    float projectionTranslateU = 0.0f;
    float projectionTranslateV = 0.0f;
    float projectionScaleU = 0.0f;
    float projectionScaleV = 0.0f;
    float projectionColorPower = 0.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 1.0f;
    float uvSet01 = 0.0f;
    float uvSet0 = 0.0f;
    float mipMapBias = 0.0f;
    bool cloudEnabled = false;
    bool castShadow = true;
    bool receiveShadow = false;
    bool rimEnabled = !isShader01;
    if (!textureMap01 || !textureMap02 || !greenBlend || !highlight ||
        !greenHikari || !shadowToon || !lightProjection || !depthBuffer ||
        !sourceColor(material.sourceMetadataJson, "Color", color) ||
        !sourceColor(
            material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "DiscardValuie", discard) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "ShadowSampingScale",
            shadowSamplingScale) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "ShadowBias", shadowBias) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapTranslateU",
            projectionTranslateU) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapTranslateV",
            projectionTranslateV) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleU",
            projectionScaleU) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleV",
            projectionScaleV) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapColorPow",
            projectionColorPower) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UVSet01", uvSet01) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UVSet0", uvSet0) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CastShadow",
            castShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "ReceiveShadow",
            receiveShadow) ||
        (material.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchDiscardEnable) == 0u ||
        !cloudEnabled || castShadow || !receiveShadow ||
        std::abs(discard - 0.85f) > 0.0001f ||
        std::abs(projectionTranslateU) > 0.0001f ||
        std::abs(projectionTranslateV) > 0.0001f ||
        std::abs(projectionScaleU - 0.5f) > 0.0001f ||
        std::abs(projectionScaleV - 0.5f) > 0.0001f ||
        std::abs(projectionColorPower - 1.0f) > 0.0001f ||
        std::abs(uvSet01 - 1.0f) > 0.0001f ||
        std::abs(uvSet0) > 0.0001f ||
        (std::abs(mipMapBias) > 0.0001f &&
         std::abs(mipMapBias + 2.0f) > 0.0001f)) {
        return false;
    }

    if (isShader01) {
        if (!sourceColor(material.sourceMetadataJson, "RimColor", rimColor) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "RimLight_Min", rimMin) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "RimLight_Max", rimMax) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "RimLight_Strength",
                rimStrength) ||
            !sourceSwitch(
                material.sourceMetadataJson,
                "Common",
                "RimLight",
                rimEnabled) ||
            !rimEnabled ||
            std::abs(shadowSamplingScale - 2.0f) > 0.0001f ||
            std::abs(shadowBias - 0.003f) > 0.0001f) {
            return false;
        }
    } else {
        if (!sourceColor(
                material.sourceMetadataJson, "OnGameColor", onGameColor) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "OnGameColorVal",
                onGameColorValue) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "OnGameAlpha", onGameAlpha) ||
            std::abs(onGameColorValue - 1.0f) > 0.0001f ||
            std::abs(onGameAlpha - 1.0f) > 0.0001f ||
            (std::abs(shadowSamplingScale - 1.5f) > 0.0001f &&
             std::abs(shadowSamplingScale - 2.0f) > 0.0001f) ||
            (std::abs(shadowBias - 0.003f) > 0.0001f &&
             std::abs(shadowBias - 0.004f) > 0.0001f)) {
            return false;
        }
    }

    // Six renderer descriptors form the exact material-local sample set.
    // LightProjMap is a separate shared-light input; DepthBuffer remains
    // bounded until its captured runtime projection matrix is represented.
    assignBaseTexture(profileId, *textureMap01, material);
    assignNormalSlot(profileId, *textureMap02, material);
    assignMetallicRoughnessSlot(profileId, *greenHikari, material);
    assignOcclusionSlot(profileId, *greenBlend, material);
    assignEmissiveSlot(profileId, *highlight, material);
    assignEnvironmentSlot(profileId, *shadowToon, material);
    assignLightProjectionSlot(profileId, *lightProjection, material);
    material.normalScale = color[0];
    material.metallicFactor = color[1];
    material.roughnessFactor = color[2];
    material.emissiveFactorR = shadowColor[0];
    material.emissiveFactorG = shadowColor[1];
    material.emissiveFactorB = shadowColor[2];
    material.materialFlipbook0Fps = mipMapBias;
    material.alphaMode = 1u;
    material.alphaCutoff = discard;
    if (isShader01) {
        material.materialTimeSec = rimMin;
        material.materialFlags = rimMax;
        material.materialAtlasWidth = rimStrength;
        material.materialAtlasHeight = rimColor[0];
        material.materialRect0U = rimColor[1];
        material.materialRect0V = rimColor[2];
        material.materialMode =
            engine::render::lgpe_field_grass::kShader01MaterialMode;
    } else {
        material.materialTimeSec = onGameColor[0];
        material.materialFlags = onGameColor[1];
        material.materialAtlasWidth = onGameColor[2];
        material.materialAtlasHeight = onGameColorValue;
        material.materialRect0U = onGameAlpha;
        material.materialMode =
            engine::render::lgpe_field_grass::kShader02MaterialMode;
    }
    return true;
}

bool configureFieldOverlaySurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    using namespace engine::render::backend;
    const bool isRoadstone =
        material.sourceShaderGroup == "FieldObjectShader" &&
        material.sourceMaterialName == "roadstone01_com";
    const bool isRockMask =
        material.sourceShaderGroup == "FieldGrassShader02" &&
        material.sourceMaterialName == "rockmask01_com";
    if (!isRoadstone && !isRockMask) return false;

    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> shadowColor{};
    std::array<float, 3> onGameColor{};
    float shadowSamplingScale = 0.0f;
    float shadowBias = 0.0f;
    float projectionTranslateU = 0.0f;
    float projectionTranslateV = 0.0f;
    float projectionScaleU = 0.0f;
    float projectionScaleV = 0.0f;
    float projectionColorPower = 0.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 0.0f;
    float mipMapBias = 0.0f;
    float sourceBlendMode = 0.0f;
    bool discardEnabled = true;
    bool cloudEnabled = false;
    bool castShadow = true;
    bool receiveShadow = false;
    bool depthWrite = false;
    bool depthTest = false;
    if (!shadowToon || !lightProjection || !depthBuffer ||
        !sourceColor(
            material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(
            material.sourceMetadataJson, "OnGameColor", onGameColor) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "ShadowSampingScale",
            shadowSamplingScale) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "ShadowBias", shadowBias) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapTranslateU",
            projectionTranslateU) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapTranslateV",
            projectionTranslateV) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleU",
            projectionScaleU) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleV",
            projectionScaleV) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapColorPow",
            projectionColorPower) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "OnGameColorVal",
            onGameColorValue) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "OnGameAlpha", onGameAlpha) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceValue(
            material.sourceMetadataJson,
            "Common",
            "BlendMode",
            sourceBlendMode) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DiscardEnable",
            discardEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CastShadow",
            castShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "ReceiveShadow",
            receiveShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DepthWrite",
            depthWrite) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DepthTest",
            depthTest) ||
        discardEnabled || !cloudEnabled || castShadow || !receiveShadow ||
        !depthWrite || !depthTest ||
        std::abs(shadowSamplingScale - 2.0f) > 0.0001f ||
        std::abs(projectionTranslateU) > 0.0001f ||
        std::abs(projectionTranslateV) > 0.0001f ||
        std::abs(projectionScaleU - 0.5f) > 0.0001f ||
        std::abs(projectionScaleV - 0.5f) > 0.0001f ||
        std::abs(projectionColorPower - 1.0f) > 0.0001f ||
        std::abs(onGameColorValue - 1.0f) > 0.0001f ||
        std::abs(onGameAlpha - 1.0f) > 0.0001f ||
        std::abs(mipMapBias + 2.0f) > 0.0001f ||
        std::abs(sourceBlendMode - 5.0f) > 0.0001f) {
        return false;
    }

    material.emissiveFactorR = shadowColor[0];
    material.emissiveFactorG = shadowColor[1];
    material.emissiveFactorB = shadowColor[2];
    material.materialTimeSec = onGameColor[0];
    material.materialFlags = onGameColor[1];
    material.materialAtlasWidth = onGameColor[2];
    material.materialAtlasHeight = onGameColorValue;
    material.materialRect0U = onGameAlpha;
    material.materialFlipbook0Fps = mipMapBias;
    // Native BlendMode 5 consumes the shaders' premultiplied RGB output.
    material.alphaMode = 2u;
    material.blendMode = 2u;
    assignLightProjectionSlot(profileId, *lightProjection, material);

    if (isRoadstone) {
        const auto* texture01 = sourceBinding(material, "Texture01");
        std::array<float, 2> tex01Translate{};
        std::array<float, 2> tex01Scale{};
        float tex01Rotate = 0.0f;
        float transparent = 0.0f;
        float tex01Uv = 0.0f;
        if (!texture01 ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "Tex01_Translate_U",
                tex01Translate[0]) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "Tex01_Translate_V",
                tex01Translate[1]) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "Tex01_Scale_U",
                tex01Scale[0]) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "Tex01_Scale_V",
                tex01Scale[1]) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Tex01_Rotate", tex01Rotate) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Transparent", transparent) ||
            !sourceValue(
                material.sourceMetadataJson, "Common", "Tex01_UV", tex01Uv) ||
            std::abs(shadowBias - 0.003f) > 0.0001f ||
            std::abs(tex01Translate[0]) > 0.0001f ||
            std::abs(tex01Translate[1]) > 0.0001f ||
            std::abs(tex01Scale[0] - 1.0f) > 0.0001f ||
            std::abs(tex01Scale[1] - 1.0f) > 0.0001f ||
            std::abs(tex01Rotate) > 0.0001f ||
            std::abs(transparent - 1.0f) > 0.0001f ||
            std::abs(tex01Uv) > 0.0001f) {
            return false;
        }
        assignBaseTexture(profileId, *texture01, material);
        assignOcclusionSlot(profileId, *shadowToon, material);
        material.materialRect0V = transparent;
        material.materialMode =
            engine::render::lgpe_field_overlay::kRoadstoneMaterialMode;
        return true;
    }

    const auto* textureMap01 = sourceBinding(material, "TextureMap01");
    const auto* textureMap02 = sourceBinding(material, "TextureMap02");
    const auto* greenBlend = sourceBinding(material, "green_blend");
    const auto* highlight = sourceBinding(material, "Hilight");
    const auto* greenHikari = sourceBinding(material, "green_hikari");
    std::array<float, 3> color{};
    float uvSet01 = 0.0f;
    float uvSet0 = 0.0f;
    if (!textureMap01 || !textureMap02 || !greenBlend || !highlight ||
        !greenHikari ||
        !sourceColor(material.sourceMetadataJson, "Color", color) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UVSet01", uvSet01) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UVSet0", uvSet0) ||
        std::abs(shadowBias - 0.0031f) > 0.0001f ||
        std::abs(uvSet01 - 1.0f) > 0.0001f ||
        std::abs(uvSet0) > 0.0001f) {
        return false;
    }

    assignBaseTexture(profileId, *textureMap01, material);
    assignNormalSlot(profileId, *textureMap02, material);
    assignMetallicRoughnessSlot(profileId, *greenHikari, material);
    assignOcclusionSlot(profileId, *greenBlend, material);
    assignEmissiveSlot(profileId, *highlight, material);
    assignEnvironmentSlot(profileId, *shadowToon, material);
    material.normalScale = color[0];
    material.metallicFactor = color[1];
    material.roughnessFactor = color[2];
    material.materialMode =
        engine::render::lgpe_field_overlay::kRockMaskMaterialMode;
    return true;
}

bool configureFieldFlowerSurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    if (material.sourceShaderGroup != "FieldObjectShader" ||
        material.sourceMaterialName != "flower01_com") {
        return false;
    }

    const auto* texture01 = sourceBinding(material, "Texture01");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> shadowColor{};
    std::array<float, 3> onGameColor{};
    float transparent = 0.0f;
    float discardValue = 0.0f;
    float shadowSamplingScale = 0.0f;
    float shadowBias = 0.0f;
    float projectionScaleU = 0.0f;
    float projectionScaleV = 0.0f;
    float projectionColorPower = 0.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 0.0f;
    float tex01Uv = 0.0f;
    float mipMapBias = 0.0f;
    float sourceBlendMode = 0.0f;
    float sourcePremultiplyMode = 0.0f;
    bool discardEnabled = false;
    bool cloudEnabled = false;
    bool castShadow = true;
    bool receiveShadow = false;
    bool depthWrite = false;
    bool depthTest = false;
    if (!texture01 || !shadowToon || !lightProjection || !depthBuffer ||
        !sourceColor(
            material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(
            material.sourceMetadataJson, "OnGameColor", onGameColor) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Transparent", transparent) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "DiscardValuie", discardValue) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "ShadowSampingScale",
            shadowSamplingScale) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "ShadowBias", shadowBias) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleU",
            projectionScaleU) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleV",
            projectionScaleV) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapColorPow",
            projectionColorPower) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "OnGameColorVal",
            onGameColorValue) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "OnGameAlpha", onGameAlpha) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "Tex01_UV", tex01Uv) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceValue(
            material.sourceMetadataJson,
            "Common",
            "BlendMode",
            sourceBlendMode) ||
        !sourceValue(
            material.sourceMetadataJson,
            "Common",
            "PreMultiplieMode",
            sourcePremultiplyMode) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DiscardEnable",
            discardEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CastShadow",
            castShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "ReceiveShadow",
            receiveShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DepthWrite",
            depthWrite) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DepthTest",
            depthTest) ||
        !discardEnabled || !cloudEnabled || castShadow || !receiveShadow ||
        !depthWrite || !depthTest ||
        std::abs(transparent - 1.0f) > 0.0001f ||
        std::abs(
            discardValue -
            engine::render::lgpe_field_flower::kDiscardValue) > 0.0001f ||
        std::abs(shadowSamplingScale - 2.0f) > 0.0001f ||
        std::abs(shadowBias - 0.003f) > 0.0001f ||
        std::abs(projectionScaleU - 0.5f) > 0.0001f ||
        std::abs(projectionScaleV - 0.5f) > 0.0001f ||
        std::abs(projectionColorPower - 1.0f) > 0.0001f ||
        std::abs(onGameColorValue - 1.0f) > 0.0001f ||
        std::abs(onGameAlpha - 1.0f) > 0.0001f ||
        std::abs(tex01Uv) > 0.0001f ||
        std::abs(mipMapBias) > 0.0001f ||
        std::abs(sourceBlendMode) > 0.0001f ||
        std::abs(sourcePremultiplyMode) > 0.0001f) {
        return false;
    }

    assignBaseTexture(profileId, *texture01, material);
    assignOcclusionSlot(profileId, *shadowToon, material);
    assignLightProjectionSlot(profileId, *lightProjection, material);
    material.emissiveFactorR = shadowColor[0];
    material.emissiveFactorG = shadowColor[1];
    material.emissiveFactorB = shadowColor[2];
    material.materialTimeSec = onGameColor[0];
    material.materialFlags = onGameColor[1];
    material.materialAtlasWidth = onGameColor[2];
    material.materialAtlasHeight = onGameColorValue;
    material.materialRect0U = onGameAlpha;
    material.materialRect0V = transparent;
    material.materialFlipbook0Fps = mipMapBias;
    material.alphaMode = 1u;
    material.alphaCutoff = discardValue;
    material.materialMode =
        engine::render::lgpe_field_flower::kMaterialMode;
    return true;
}

bool configureFieldRockSurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    if (material.sourceShaderGroup != "FieldRockShader" ||
        material.sourceMaterialName != "rock01_com_grass01_com") {
        return false;
    }

    const auto* rock = sourceBinding(material, "Rock_tex");
    const auto* ground01 = sourceBinding(material, "GroundTex01");
    const auto* ground02 = sourceBinding(material, "GroundTex02");
    const auto* blend = sourceBinding(material, "BlendTex");
    const auto* border = sourceBinding(material, "BorderTex");
    const auto* lightToon = sourceBinding(material, "lightToonTable");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> lightColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> onGameColor{};
    float rimMin = 0.0f;
    float rimMax = 0.0f;
    float rimStrength = 0.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 0.0f;
    float groundBlend = 0.0f;
    float tex01Uv = 0.0f;
    float tex00Uv = 0.0f;
    float mipMapBias = 0.0f;
    float sourceBlendMode = 0.0f;
    bool discardEnabled = true;
    bool cloudEnabled = false;
    bool castShadow = true;
    bool receiveShadow = false;
    bool rimEnabled = false;
    bool depthWrite = false;
    bool depthTest = false;
    if (!rock || !ground01 || !ground02 || !blend || !border ||
        !lightToon || !shadowToon || !lightProjection || !depthBuffer ||
        !sourceColor(
            material.sourceMetadataJson, "lightColor", lightColor) ||
        !sourceColor(
            material.sourceMetadataJson, "RimColor", rimColor) ||
        !sourceColor(
            material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(
            material.sourceMetadataJson, "OnGameColor", onGameColor) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Min", rimMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Max", rimMax) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "RimLight_Strength",
            rimStrength) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "OnGameColorVal",
            onGameColorValue) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "OnGameAlpha", onGameAlpha) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "GroundBlend", groundBlend) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "Tex01_UV", tex01Uv) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "Tex00_UV", tex00Uv) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceValue(
            material.sourceMetadataJson,
            "Common",
            "BlendMode",
            sourceBlendMode) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DiscardEnable",
            discardEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CastShadow",
            castShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "ReceiveShadow",
            receiveShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "RimLight",
            rimEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DepthWrite",
            depthWrite) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DepthTest",
            depthTest) ||
        discardEnabled || !cloudEnabled || castShadow || !receiveShadow ||
        !rimEnabled || !depthWrite || !depthTest ||
        std::abs(onGameColorValue - 1.0f) > 0.0001f ||
        std::abs(onGameAlpha - 1.0f) > 0.0001f ||
        std::abs(groundBlend - 2.0f) > 0.0001f ||
        std::abs(tex01Uv - 1.0f) > 0.0001f ||
        std::abs(tex00Uv) > 0.0001f ||
        std::abs(mipMapBias) > 0.0001f ||
        std::abs(sourceBlendMode) > 0.0001f) {
        return false;
    }

    // The five surface maps plus the source shadow-toon table fit the
    // renderer's typed six-texture contract. The separate light-table red
    // curve is recovered byte-for-byte in LgpeFieldRockMaterial and evaluated
    // from the same source normal coordinate in each backend.
    assignBaseTexture(profileId, *rock, material);
    assignNormalSlot(profileId, *ground02, material);
    assignMetallicRoughnessSlot(profileId, *ground01, material);
    assignOcclusionSlot(profileId, *blend, material);
    assignEmissiveSlot(profileId, *border, material);
    assignEnvironmentSlot(profileId, *shadowToon, material);
    assignLightProjectionSlot(profileId, *lightProjection, material);

    material.emissiveFactorR = lightColor[0];
    material.emissiveFactorG = lightColor[1];
    material.emissiveFactorB = lightColor[2];
    material.normalScale = rimColor[0];
    material.metallicFactor = rimColor[1];
    material.roughnessFactor = rimColor[2];
    material.occlusionStrength = rimMin;
    material.materialTimeSec = rimMax;
    material.materialFlags = rimStrength;
    material.materialAtlasWidth = shadowColor[0];
    material.materialAtlasHeight = shadowColor[1];
    material.materialRect0U = shadowColor[2];
    material.materialRect0V = onGameColor[0];
    material.materialRect0W = onGameColor[1];
    material.materialRect0H = onGameColor[2];
    material.materialRect1U = onGameColorValue;
    material.materialRect1V = onGameAlpha;
    material.materialFlipbook0Fps = mipMapBias;
    material.alphaMode = 0u;
    material.materialMode =
        engine::render::lgpe_field_rock::kMaterialMode;
    return true;
}

bool configureFieldSignSurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    if (material.sourceShaderGroup != "FieldObjectShader" ||
        material.sourceMaterialName != "bm_signboard01_01") {
        return false;
    }

    const auto* texture01 = sourceBinding(material, "Texture01");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightToon = sourceBinding(material, "lightToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> lightColor{};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> autoShadowColor{};
    std::array<float, 3> onGameColor{};
    std::array<float, 3> rimColor{};
    float shadowBias = 0.0f;
    float transparent = 0.0f;
    float shadowSamplingScale = 0.0f;
    float shadowMin = 0.0f;
    float shadowMax = 0.0f;
    float shadowStrength = 0.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 0.0f;
    float projectionScaleU = 0.0f;
    float projectionScaleV = 0.0f;
    float projectionColorPower = 0.0f;
    float rimMin = 0.0f;
    float rimMax = 0.0f;
    float rimStrength = 0.0f;
    float tex01Uv = 0.0f;
    float mipMapBias = 0.0f;
    float sourceBlendMode = 0.0f;
    float sourcePremultiplyMode = 0.0f;
    bool discardEnabled = true;
    bool directionalHighlight = false;
    bool cloudEnabled = false;
    bool castShadow = false;
    bool receiveShadow = false;
    bool autoShadow = false;
    bool rimEnabled = false;
    bool depthWrite = false;
    bool depthTest = false;
    if (!texture01 || !shadowToon || !lightToon ||
        !lightProjection || !depthBuffer ||
        !sourceColor(
            material.sourceMetadataJson, "lightColor", lightColor) ||
        !sourceColor(
            material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(
            material.sourceMetadataJson, "ShadowColor", autoShadowColor) ||
        !sourceColor(
            material.sourceMetadataJson, "OnGameColor", onGameColor) ||
        !sourceColor(
            material.sourceMetadataJson, "RimColor", rimColor) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "ShadowBias", shadowBias) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Transparent", transparent) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "ShadowSampingScale",
            shadowSamplingScale) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Shadow_Min", shadowMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Shadow_Max", shadowMax) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "Shadow_Strangth",
            shadowStrength) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "OnGameColorVal",
            onGameColorValue) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "OnGameAlpha", onGameAlpha) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleU",
            projectionScaleU) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleV",
            projectionScaleV) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapColorPow",
            projectionColorPower) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Min", rimMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Max", rimMax) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "RimLight_Strength",
            rimStrength) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "Tex01_UV", tex01Uv) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceValue(
            material.sourceMetadataJson,
            "Common",
            "BlendMode",
            sourceBlendMode) ||
        !sourceValue(
            material.sourceMetadataJson,
            "Common",
            "PreMultiplieMode",
            sourcePremultiplyMode) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DiscardEnable",
            discardEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "LightDir_Hilight",
            directionalHighlight) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CastShadow",
            castShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "ReceiveShadow",
            receiveShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "AutoShadow",
            autoShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "RimLight",
            rimEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DepthWrite",
            depthWrite) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "DepthTest",
            depthTest) ||
        discardEnabled || !directionalHighlight || !cloudEnabled ||
        !castShadow || !receiveShadow || !autoShadow || !rimEnabled ||
        !depthWrite || !depthTest ||
        std::abs(shadowBias - 0.003f) > 0.0001f ||
        std::abs(transparent - 1.0f) > 0.0001f ||
        std::abs(shadowSamplingScale - 2.0f) > 0.0001f ||
        std::abs(shadowMin -
                 engine::render::lgpe_field_sign::kShadowMin) > 0.0001f ||
        std::abs(shadowMax -
                 engine::render::lgpe_field_sign::kShadowMax) > 0.0001f ||
        std::abs(shadowStrength -
                 engine::render::lgpe_field_sign::kShadowStrength) >
            0.0001f ||
        std::abs(onGameColorValue - 1.0f) > 0.0001f ||
        std::abs(onGameAlpha - 1.0f) > 0.0001f ||
        std::abs(projectionScaleU - 0.5f) > 0.0001f ||
        std::abs(projectionScaleV - 0.5f) > 0.0001f ||
        std::abs(projectionColorPower - 1.0f) > 0.0001f ||
        std::abs(rimMin -
                 engine::render::lgpe_field_sign::kRimMin) > 0.0001f ||
        std::abs(rimMax -
                 engine::render::lgpe_field_sign::kRimMax) > 0.0001f ||
        std::abs(rimStrength -
                 engine::render::lgpe_field_sign::kRimStrength) > 0.0001f ||
        std::abs(tex01Uv) > 0.0001f ||
        std::abs(mipMapBias) > 0.0001f ||
        std::abs(sourceBlendMode) > 0.0001f ||
        std::abs(sourcePremultiplyMode) > 0.0001f ||
        std::abs(autoShadowColor[0] - 1.0f) > 0.0001f ||
        std::abs(autoShadowColor[1] - 1.0f) > 0.0001f ||
        std::abs(autoShadowColor[2] - 1.0f) > 0.0001f ||
        std::abs(onGameColor[0] - 1.0f) > 0.0001f ||
        std::abs(onGameColor[1] - 1.0f) > 0.0001f ||
        std::abs(onGameColor[2] - 1.0f) > 0.0001f) {
        return false;
    }

    // The source light-table red curve is byte-identical to the one already
    // recovered for FieldRockShader and is evaluated exactly in every backend.
    assignBaseTexture(profileId, *texture01, material);
    assignOcclusionSlot(profileId, *shadowToon, material);
    assignLightProjectionSlot(profileId, *lightProjection, material);
    material.emissiveFactorR = lightColor[0];
    material.emissiveFactorG = lightColor[1];
    material.emissiveFactorB = lightColor[2];
    material.normalScale = shadowColor[0];
    material.metallicFactor = shadowColor[1];
    material.roughnessFactor = shadowColor[2];
    material.materialTimeSec = rimColor[0];
    material.materialFlags = rimColor[1];
    material.materialAtlasWidth = rimColor[2];
    material.materialAtlasHeight = rimMin;
    material.materialRect0U = rimMax;
    material.materialRect0V = rimStrength;
    material.materialFlipbook0Fps = mipMapBias;
    material.alphaMode = 0u;
    material.materialMode =
        engine::render::lgpe_field_sign::kMaterialMode;
    return true;
}

bool configureFieldSmallGrassSurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    using namespace engine::render::backend;
    const bool isShader04 =
        material.sourceShaderGroup == "FieldGrassShader04" &&
        material.sourceMaterialName == "grass_s03";
    const bool isShader05 =
        material.sourceShaderGroup == "FieldGrassShader05" &&
        material.sourceMaterialName == "grass_s04";
    if (!isShader04 && !isShader05) return false;

    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> shadowColor{};
    std::array<float, 3> onGameColor{};
    float shadowSamplingScale = 0.0f;
    float shadowBias = 0.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 0.0f;
    float projectionTranslateU = 0.0f;
    float projectionTranslateV = 0.0f;
    float projectionScaleU = 0.0f;
    float projectionScaleV = 0.0f;
    float projectionColorPower = 0.0f;
    float discard = 0.0f;
    float mipMapBias = 0.0f;
    bool cloudEnabled = false;
    bool castShadow = true;
    bool receiveShadow = false;
    if (!shadowToon || !lightProjection || !depthBuffer ||
        !sourceColor(
            material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(
            material.sourceMetadataJson, "OnGameColor", onGameColor) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "ShadowSampingScale",
            shadowSamplingScale) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "ShadowBias", shadowBias) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "OnGameColorVal",
            onGameColorValue) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "OnGameAlpha", onGameAlpha) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapTranslateU",
            projectionTranslateU) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapTranslateV",
            projectionTranslateV) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleU",
            projectionScaleU) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapScaleV",
            projectionScaleV) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "LightProjMapColorPow",
            projectionColorPower) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "DiscardValuie", discard) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CastShadow",
            castShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "ReceiveShadow",
            receiveShadow) ||
        (material.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchDiscardEnable) == 0u ||
        !cloudEnabled || castShadow || !receiveShadow ||
        std::abs(shadowSamplingScale - 2.0f) > 0.0001f ||
        std::abs(shadowBias - 0.003f) > 0.0001f ||
        std::abs(onGameColorValue - 1.0f) > 0.0001f ||
        std::abs(onGameAlpha - 1.0f) > 0.0001f ||
        std::abs(projectionTranslateU) > 0.0001f ||
        std::abs(projectionTranslateV) > 0.0001f ||
        std::abs(projectionScaleU - 0.5f) > 0.0001f ||
        std::abs(projectionScaleV - 0.5f) > 0.0001f ||
        std::abs(projectionColorPower - 1.0f) > 0.0001f ||
        std::abs(mipMapBias) > 0.0001f) {
        return false;
    }

    material.normalScale = shadowColor[0];
    material.metallicFactor = shadowColor[1];
    material.roughnessFactor = shadowColor[2];
    // D3D12 repacks Shadow_Color from the fixed scalar slots above. OpenGL
    // and both Vulkan paths consume the ordinary emissive-factor payload.
    material.emissiveFactorR = shadowColor[0];
    material.emissiveFactorG = shadowColor[1];
    material.emissiveFactorB = shadowColor[2];
    material.alphaMode = 1u;
    material.alphaCutoff = discard;

    assignLightProjectionSlot(profileId, *lightProjection, material);
    if (isShader04) {
        const auto* texture01 = sourceBinding(material, "Texture01");
        const auto* texture02 = sourceBinding(material, "Texture02");
        const auto* texture03 = sourceBinding(material, "Texture03");
        float transparent = 0.0f;
        float tex01TranslateU = 0.0f;
        float tex01TranslateV = 0.0f;
        float tex01Rotate = 0.0f;
        float tex01ScaleU = 0.0f;
        float tex01ScaleV = 0.0f;
        float tex02TranslateU = 0.0f;
        float tex02TranslateV = 0.0f;
        float tex02Rotate = 0.0f;
        float tex02ScaleU = 0.0f;
        float tex02ScaleV = 0.0f;
        float tex01Uv = 0.0f;
        float tex02Uv = 0.0f;
        if (!texture01 || !texture02 || !texture03 ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Transparent", transparent) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "Tex01_Translate_U",
                tex01TranslateU) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "Tex01_Translate_V",
                tex01TranslateV) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Tex01_Rotate", tex01Rotate) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Tex01_Scale_U", tex01ScaleU) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Tex01_Scale_V", tex01ScaleV) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "Tex02_Translate_U",
                tex02TranslateU) ||
            !sourceValue(
                material.sourceMetadataJson,
                {},
                "Tex02_Translate_V",
                tex02TranslateV) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Tex02_Rotate", tex02Rotate) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Tex02_Scale_U", tex02ScaleU) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "Tex02_Scale_V", tex02ScaleV) ||
            !sourceValue(
                material.sourceMetadataJson, "Common", "Tex01_UV", tex01Uv) ||
            !sourceValue(
                material.sourceMetadataJson, "Common", "Tex02_UV", tex02Uv) ||
            std::abs(discard - 0.470133f) > 0.0001f ||
            std::abs(transparent - 1.0f) > 0.0001f ||
            std::abs(tex01TranslateU) > 0.0001f ||
            std::abs(tex01TranslateV) > 0.0001f ||
            std::abs(tex01Rotate) > 0.0001f ||
            std::abs(tex01ScaleU - 1.0f) > 0.0001f ||
            std::abs(tex01ScaleV - 1.0f) > 0.0001f ||
            std::abs(tex02TranslateU) > 0.0001f ||
            std::abs(tex02TranslateV) > 0.0001f ||
            std::abs(tex02Rotate) > 0.0001f ||
            std::abs(tex02ScaleU - 1.0f) > 0.0001f ||
            std::abs(tex02ScaleV - 1.0f) > 0.0001f ||
            std::abs(tex01Uv) > 0.0001f ||
            std::abs(tex02Uv - 1.0f) > 0.0001f) {
            return false;
        }
        // RenderDoc binding 8/9/10 plus the material dictionary prove
        // mix(Texture01(UV0), Texture02(UV1), Texture03(UV1).r). Keeping
        // those semantic roles explicit avoids the pale-card failure caused
        // by reading the decoded sampler-slot order as material-name order.
        assignBaseTexture(profileId, *texture01, material);
        assignNormalSlot(profileId, *texture02, material);
        assignMetallicRoughnessSlot(profileId, *texture03, material);
        assignOcclusionSlot(profileId, *shadowToon, material);
        assignEmissiveSlot(profileId, *lightProjection, material);
        material.materialTimeSec = transparent;
        material.materialFlags = onGameColorValue;
        material.materialAtlasWidth = onGameAlpha;
        material.materialAtlasHeight = onGameColor[0];
        material.materialRect0U = onGameColor[1];
        material.materialRect0V = onGameColor[2];
        material.materialMode =
            engine::render::lgpe_field_small_grass::kShader04MaterialMode;
    } else {
        const auto* textureMap01 = sourceBinding(material, "TextureMap01");
        const auto* textureMap02 = sourceBinding(material, "TextureMap02");
        const auto* greenBlend = sourceBinding(material, "green_blend");
        const auto* lightLine = sourceBinding(material, "light_line");
        const auto* alpha01 = sourceBinding(material, "alpha01");
        float scrollU = 0.0f;
        float scrollV = 0.0f;
        float tex01Uv = 0.0f;
        float uvTexture0 = 0.0f;
        if (!textureMap01 || !textureMap02 || !greenBlend ||
            !lightLine || !alpha01 ||
            !sourceValue(
                material.sourceMetadataJson, {}, "scroll_U", scrollU) ||
            !sourceValue(
                material.sourceMetadataJson, {}, "scroll_V", scrollV) ||
            !sourceValue(
                material.sourceMetadataJson, "Common", "Tex01_UV", tex01Uv) ||
            !sourceValue(
                material.sourceMetadataJson, "Common", "UV_tex0", uvTexture0) ||
            std::abs(discard - 0.85f) > 0.0001f ||
            std::abs(scrollU - 1.0f) > 0.0001f ||
            std::abs(scrollV - 1.0f) > 0.0001f ||
            std::abs(tex01Uv - 1.0f) > 0.0001f ||
            std::abs(uvTexture0) > 0.0001f) {
            return false;
        }
        // Exact BNSH remap: light_line masks two alpha01 samples; the
        // green_blend layer then mixes TextureMap02 to TextureMap01.
        assignBaseTexture(profileId, *alpha01, material);
        assignNormalSlot(profileId, *lightLine, material);
        assignMetallicRoughnessSlot(profileId, *textureMap01, material);
        assignOcclusionSlot(profileId, *textureMap02, material);
        assignEmissiveSlot(profileId, *greenBlend, material);
        // Route 1's decoded shadowtable02_t base level is entirely white.
        // Preserve its exact neutral response in the shader and use the final
        // descriptor for the non-neutral source cloud projection.
        assignEnvironmentSlot(profileId, *lightProjection, material);
        material.materialTimeSec = onGameColorValue;
        material.materialFlags = onGameAlpha;
        material.materialAtlasWidth = onGameColor[0];
        material.materialAtlasHeight = onGameColor[1];
        material.materialRect0U = onGameColor[2];
        material.materialRect0V = scrollU;
        material.materialRect0W = scrollV;
        material.materialMode =
            engine::render::lgpe_field_small_grass::kShader05MaterialMode;
    }
    return true;
}

bool configureFieldTree02Surface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    using namespace engine::render::backend;
    if (material.sourceShaderGroup != "FieldTreeShader02") return false;

    const auto* texture01 = sourceBinding(material, "Texture01");
    const auto* texture02 = sourceBinding(material, "Texture02");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightToon = sourceBinding(material, "lightToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> greenColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> shadowColor{};
    std::array<float, 3> directionalLightColor{};
    std::array<float, 3> rimColor02{};
    float discard = 0.0f;
    float rimMin = 0.0f;
    float rimMax = 0.0f;
    float rimStrength = 0.0f;
    float shadowSamplingScale = 0.0f;
    float shadowBias = 0.0f;
    float uvTexture01 = 0.0f;
    float mipMapBias = 0.0f;
    bool rimEnabled = false;
    bool directionalHighlightEnabled = false;
    bool cloudEnabled = false;
    bool castShadow = true;
    bool receiveShadow = false;
    if (!texture01 || !texture02 || !shadowToon || !lightToon ||
        !lightProjection || !depthBuffer ||
        !sourceColor(material.sourceMetadataJson, "GreenColor", greenColor) ||
        !sourceColor(material.sourceMetadataJson, "RimColor", rimColor) ||
        !sourceColor(
            material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(
            material.sourceMetadataJson,
            "DirlightColor",
            directionalLightColor) ||
        !sourceColor(
            material.sourceMetadataJson, "rimColor02", rimColor02) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "DiscardValuie", discard) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Min", rimMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Max", rimMax) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Strength", rimStrength) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "ShadowSampingScale",
            shadowSamplingScale) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "ShadowBias", shadowBias) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UV_tex01", uvTexture01) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceSwitch(
            material.sourceMetadataJson, "Common", "RimLight", rimEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "LightDir_Hilight",
            directionalHighlightEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CastShadow",
            castShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "ReceiveShadow",
            receiveShadow) ||
        (material.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchDiscardEnable) == 0u ||
        !rimEnabled || !directionalHighlightEnabled || !cloudEnabled ||
        castShadow || !receiveShadow ||
        std::abs(discard - 0.6f) > 0.0001f ||
        std::abs(shadowSamplingScale - 2.0f) > 0.0001f ||
        std::abs(shadowBias - 0.05f) > 0.0001f ||
        std::abs(uvTexture01) > 0.0001f ||
        std::abs(mipMapBias) > 0.0001f) {
        return false;
    }

    // The exact named fragment program samples five roles. LightProjMap is
    // retained in sourceTextureBindings as authored metadata but is not
    // sampled by this variant. DepthBuffer remains bound for the future
    // source ten-tap PCF integration.
    assignBaseTexture(profileId, *texture01, material);
    assignNormalSlot(profileId, *texture02, material);
    assignOcclusionSlot(profileId, *shadowToon, material);
    assignEmissiveSlot(profileId, *lightToon, material);
    assignEnvironmentSlot(profileId, *depthBuffer, material);

    // Private mode-8 payload:
    // pbr.xyz = GreenColor, emissive.xyz = Shadow_Color,
    // timing.xyz = rim range/strength, timing.w+rect0.xy = RimColor,
    // rect0.zw+rect1.x = DirlightColor, rect1.yzw = rimColor02.
    material.normalScale = greenColor[0];
    material.metallicFactor = greenColor[1];
    material.roughnessFactor = greenColor[2];
    material.emissiveFactorR = shadowColor[0];
    material.emissiveFactorG = shadowColor[1];
    material.emissiveFactorB = shadowColor[2];
    material.materialTimeSec = rimMin;
    material.materialFlags = rimMax;
    material.materialAtlasWidth = rimStrength;
    material.materialAtlasHeight = rimColor[0];
    material.materialRect0U = rimColor[1];
    material.materialRect0V = rimColor[2];
    material.materialRect0W = directionalLightColor[0];
    material.materialRect0H = directionalLightColor[1];
    material.materialRect1U = directionalLightColor[2];
    material.materialRect1V = rimColor02[0];
    material.materialRect1W = rimColor02[1];
    material.materialRect1H = rimColor02[2];
    material.alphaMode = 1u;
    material.alphaCutoff = discard;
    material.materialMode =
        engine::render::lgpe_field_tree02::kMaterialMode;
    return true;
}

bool configureFieldTree04Surface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    using namespace engine::render::backend;
    if (material.sourceShaderGroup != "FieldTreeShader04") return false;

    const auto* texture01 = sourceBinding(material, "Texture01");
    const auto* texture02 = sourceBinding(material, "Texture02");
    const auto* texture03 = sourceBinding(material, "Texture03");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> shadowColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> rimColor02{};
    std::array<float, 3> lightColor{};
    float discard = 0.0f;
    float rimMin = 0.0f;
    float rimMax = 0.0f;
    float rimStrength = 0.0f;
    float secondaryMin = 0.0f;
    float secondaryMax = 0.0f;
    float secondaryStrength = 0.0f;
    float shadowSamplingScale = 0.0f;
    float shadowBias = 0.0f;
    float uvTexture01 = 0.0f;
    float uvSet01 = 0.0f;
    float mipMapBias = 0.0f;
    bool cloudEnabled = false;
    bool castShadow = true;
    bool receiveShadow = false;
    if (!texture01 || !texture02 || !texture03 || !shadowToon ||
        !lightProjection || !depthBuffer ||
        !sourceColor(material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(material.sourceMetadataJson, "RimColor", rimColor) ||
        !sourceColor(material.sourceMetadataJson, "rimColor02", rimColor02) ||
        !sourceColor(material.sourceMetadataJson, "lightColor", lightColor) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "DiscardValuie", discard) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Min", rimMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Max", rimMax) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Strength", rimStrength) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "LightDir_Min", secondaryMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "LightDir_Max", secondaryMax) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Camera_Light", secondaryStrength) ||
        !sourceValue(
            material.sourceMetadataJson,
            {},
            "ShadowSampingScale",
            shadowSamplingScale) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "ShadowBias", shadowBias) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UV_tex01", uvTexture01) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UVSet01", uvSet01) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CloudEnable",
            cloudEnabled) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "CastShadow",
            castShadow) ||
        !sourceSwitch(
            material.sourceMetadataJson,
            "Common",
            "ReceiveShadow",
            receiveShadow) ||
        (material.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchDiscardEnable) == 0u ||
        !cloudEnabled || castShadow || !receiveShadow ||
        std::abs(discard - 0.777439f) > 0.0001f ||
        std::abs(shadowSamplingScale - 2.0f) > 0.0001f ||
        std::abs(shadowBias - 0.02f) > 0.0001f ||
        std::abs(uvTexture01) > 0.0001f ||
        std::abs(uvSet01 - 1.0f) > 0.0001f ||
        std::abs(mipMapBias) > 0.0001f ||
        std::abs(
            lightColor[0] -
            engine::render::lgpe_field_tree05::kInheritedSourceLightColor[0]) >
            0.0001f ||
        std::abs(
            lightColor[1] -
            engine::render::lgpe_field_tree05::kInheritedSourceLightColor[1]) >
            0.0001f ||
        std::abs(
            lightColor[2] -
            engine::render::lgpe_field_tree05::kInheritedSourceLightColor[2]) >
            0.0001f) {
        return false;
    }

    // FieldTreeShader04's exact decoded fragment SPIR-V is byte-identical to
    // the already implemented FieldTreeShader05 program. Its source material
    // explicitly supplies the otherwise inherited lightColor constant.
    assignBaseTexture(profileId, *texture01, material);
    assignNormalSlot(profileId, *texture02, material);
    assignMetallicRoughnessSlot(profileId, *texture03, material);
    assignOcclusionSlot(profileId, *shadowToon, material);
    assignEmissiveSlot(profileId, *lightProjection, material);
    assignEnvironmentSlot(profileId, *depthBuffer, material);
    assignLightProjectionSlot(profileId, *lightProjection, material);
    material.normalScale = shadowColor[0];
    material.metallicFactor = shadowColor[1];
    material.roughnessFactor = shadowColor[2];
    material.materialTimeSec = rimMin;
    material.materialFlags = rimMax;
    material.materialAtlasWidth = rimStrength;
    material.materialAtlasHeight = rimColor[0];
    material.materialRect0U = rimColor[1];
    material.materialRect0V = rimColor[2];
    material.materialRect0W = rimColor02[0];
    material.materialRect0H = rimColor02[1];
    material.materialRect1U = rimColor02[2];
    material.materialFlipbook1Frames = secondaryMin;
    material.materialFlipbook1Fps = secondaryMax;
    material.materialFlipbook0Fps = secondaryStrength;
    material.alphaMode = 1u;
    material.alphaCutoff = discard;
    material.materialMode =
        engine::render::lgpe_field_tree05::kMaterialMode;
    return true;
}

bool configureFieldTree05Surface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    using namespace engine::render::backend;
    if (material.sourceShaderGroup != "FieldTreeShader05") return false;

    const auto* texture01 = sourceBinding(material, "Texture01");
    const auto* texture02 = sourceBinding(material, "Texture02");
    const auto* texture03 = sourceBinding(material, "Texture03");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* lightProjection = sourceBinding(material, "LightProjMap");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> shadowColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> rimColor02{};
    float discard = 0.0f;
    float rimMin = 0.0f;
    float rimMax = 0.0f;
    float rimStrength = 0.0f;
    float secondaryMin = 0.0f;
    float secondaryMax = 0.0f;
    float secondaryStrength = 0.0f;
    float uvTexture01 = 0.0f;
    float uvSet01 = 0.0f;
    float mipMapBias = 0.0f;
    if (!texture01 || !texture02 || !texture03 || !shadowToon ||
        !lightProjection || !depthBuffer ||
        !sourceColor(material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(material.sourceMetadataJson, "RimColor", rimColor) ||
        !sourceColor(material.sourceMetadataJson, "rimColor02", rimColor02) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "DiscardValuie", discard) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Min", rimMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Max", rimMax) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Strength", rimStrength) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Min", secondaryMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Max", secondaryMax) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Strangth", secondaryStrength) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UV_tex01", uvTexture01) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "UVSet01", uvSet01) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        (material.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchDiscardEnable) == 0u ||
        std::abs(discard - 0.85f) > 0.0001f ||
        std::abs(uvTexture01) > 0.0001f ||
        std::abs(uvSet01 - 1.0f) > 0.0001f ||
        std::abs(mipMapBias) > 0.0001f) {
        return false;
    }

    // Six existing renderer descriptors become a typed private contract for
    // material mode 6. Their generic names are not material semantics.
    assignBaseTexture(profileId, *texture01, material);
    assignNormalSlot(profileId, *texture02, material);
    assignMetallicRoughnessSlot(profileId, *texture03, material);
    assignOcclusionSlot(profileId, *shadowToon, material);
    assignEmissiveSlot(profileId, *lightProjection, material);
    assignEnvironmentSlot(profileId, *depthBuffer, material);
    assignLightProjectionSlot(profileId, *lightProjection, material);

    // Private mode-6 scalar packing. The backend shaders unpack these as
    // named FieldTreeShader05 constants rather than generic PBR values.
    material.normalScale = shadowColor[0];
    material.metallicFactor = shadowColor[1];
    material.roughnessFactor = shadowColor[2];
    material.materialTimeSec = rimMin;
    material.materialFlags = rimMax;
    material.materialAtlasWidth = rimStrength;
    material.materialAtlasHeight = rimColor[0];
    material.materialRect0U = rimColor[1];
    material.materialRect0V = rimColor[2];
    material.materialRect0W = rimColor02[0];
    material.materialRect0H = rimColor02[1];
    material.materialRect1U = rimColor02[2];
    material.materialFlipbook1Frames = secondaryMin;
    material.materialFlipbook1Fps = secondaryMax;
    material.materialFlipbook0Fps = secondaryStrength;
    material.alphaMode = 1u;
    material.alphaCutoff = discard;
    material.materialMode =
        engine::render::lgpe_field_tree05::kMaterialMode;
    return true;
}

bool configureFieldObjectTreeMikiSurface(
    std::string_view profileId,
    IRenderBackend::WorldSceneMaterial& material) {
    using namespace engine::render::backend;
    if (material.sourceShaderGroup != "FieldObjectShader" ||
        material.sourceMaterialName != "area02_tree2_tree_miki") {
        return false;
    }

    const auto* texture01 = sourceBinding(material, "Texture01");
    const auto* highlightMap = sourceBinding(material, "HighlightMap");
    const auto* shadowToon = sourceBinding(material, "ShadowToonTable");
    const auto* depthBuffer = sourceBinding(material, "DepthBuffer");
    std::array<float, 3> shadowColor{};
    std::array<float, 3> rimColor{};
    std::array<float, 3> onGameColor{};
    float rimMin = 0.0f;
    float rimMax = 0.0f;
    float rimStrength = 0.0f;
    float translateU = 0.0f;
    float translateV = 0.0f;
    float rotation = 0.0f;
    float scaleU = 0.0f;
    float scaleV = 0.0f;
    float transparent = 0.0f;
    float onGameColorValue = 0.0f;
    float onGameAlpha = 0.0f;
    float tex01Uv = 0.0f;
    float mipMapBias = 0.0f;
    bool rimLight = false;
    bool highlight = false;
    bool discard = true;
    bool cloud = true;
    if (!texture01 || !highlightMap || !shadowToon || !depthBuffer ||
        texture01->textureName != highlightMap->textureName ||
        !sourceColor(material.sourceMetadataJson, "Shadow_Color", shadowColor) ||
        !sourceColor(material.sourceMetadataJson, "RimColor", rimColor) ||
        !sourceColor(material.sourceMetadataJson, "OnGameColor", onGameColor) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Min", rimMin) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Max", rimMax) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "RimLight_Strength", rimStrength) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Tex01_Translate_U", translateU) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Tex01_Translate_V", translateV) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Tex01_Rotate", rotation) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Tex01_Scale_U", scaleU) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Tex01_Scale_V", scaleV) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "Transparent", transparent) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "OnGameColorVal",
            onGameColorValue) ||
        !sourceValue(
            material.sourceMetadataJson, {}, "OnGameAlpha", onGameAlpha) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "Tex01_UV", tex01Uv) ||
        !sourceValue(
            material.sourceMetadataJson, "Common", "MipMapBias", mipMapBias) ||
        !sourceSwitch(
            material.sourceMetadataJson, "Common", "RimLight", rimLight) ||
        !sourceSwitch(
            material.sourceMetadataJson, "Common", "Highlight", highlight) ||
        !sourceSwitch(
            material.sourceMetadataJson, "Common", "DiscardEnable", discard) ||
        !sourceSwitch(
            material.sourceMetadataJson, "Common", "CloudEnable", cloud) ||
        !rimLight || !highlight || discard || cloud ||
        std::abs(translateU) > 0.0001f ||
        std::abs(translateV) > 0.0001f ||
        std::abs(rotation) > 0.0001f ||
        std::abs(scaleU - 1.0f) > 0.0001f ||
        std::abs(scaleV - 1.0f) > 0.0001f ||
        std::abs(transparent - 1.0f) > 0.0001f ||
        std::abs(onGameColorValue - 1.0f) > 0.0001f ||
        std::abs(onGameAlpha - 1.0f) > 0.0001f ||
        std::abs(onGameColor[0] - 1.0f) > 0.0001f ||
        std::abs(onGameColor[1] - 1.0f) > 0.0001f ||
        std::abs(onGameColor[2] - 1.0f) > 0.0001f ||
        std::abs(tex01Uv) > 0.0001f ||
        std::abs(mipMapBias) > 0.0001f) {
        return false;
    }

    // Four existing descriptors become the typed mode-7 source contract.
    // Texture01 and HighlightMap intentionally reference stem01_com through
    // different samplers and UV sets in the recovered program.
    assignBaseTexture(profileId, *texture01, material);
    assignNormalSlot(profileId, *highlightMap, material);
    assignOcclusionSlot(profileId, *shadowToon, material);
    assignEnvironmentSlot(profileId, *depthBuffer, material);

    material.normalScale = shadowColor[0];
    material.metallicFactor = shadowColor[1];
    material.roughnessFactor = shadowColor[2];
    material.materialTimeSec = rimMin;
    material.materialFlags = rimMax;
    material.materialAtlasWidth = rimStrength;
    material.materialAtlasHeight = rimColor[0];
    material.materialRect0U = rimColor[1];
    material.materialRect0V = rimColor[2];
    material.alphaMode = 0u;
    material.materialMode =
        engine::render::lgpe_field_object_tree_miki::kMaterialMode;
    return true;
}

std::vector<std::string_view> previewSamplerPriority(Family family) {
    switch (family) {
        case Family::Ground:
            return {"GrassTex01"};
        case Family::Grass:
            return {"Texture01", "TextureMap01"};
        case Family::Cliff:
            return {"CliffTex01"};
        case Family::Rock:
            return {"Rock_tex"};
        case Family::Tree:
        case Family::Object:
            return {"Texture01"};
        default:
            return {};
    }
}

std::int32_t previewBindingIndex(
    Family family,
    const std::vector<IRenderBackend::WorldSceneSourceTextureBinding>& bindings) {
    for (const std::string_view sampler : previewSamplerPriority(family)) {
        const auto found = std::find_if(
            bindings.begin(),
            bindings.end(),
            [sampler](const auto& binding) {
                return binding.samplerName == sampler && binding.baseRgba;
            });
        if (found != bindings.end()) {
            return static_cast<std::int32_t>(
                std::distance(bindings.begin(), found));
        }
    }
    return -1;
}

IRenderBackend::WorldMeshVertex baseVertex(
    const engine::assets::lgpe::CanonicalVertex& source) {
    IRenderBackend::WorldMeshVertex out{};
    out.x = source.position[0];
    out.y = source.position[1];
    out.z = source.position[2];
    out.u = source.texcoords[0][0];
    out.v = source.texcoords[0][1];
    out.r = source.colors[0][0];
    out.g = source.colors[0][1];
    out.b = source.colors[0][2];
    out.a = source.colors[0][3];
    out.nx = source.normal[0];
    out.ny = source.normal[1];
    out.nz = source.normal[2];
    out.joint0 = static_cast<float>(source.joints[0]);
    out.joint1 = static_cast<float>(source.joints[1]);
    out.joint2 = static_cast<float>(source.joints[2]);
    out.joint3 = static_cast<float>(source.joints[3]);
    out.weight0 = source.weights[0];
    out.weight1 = source.weights[1];
    out.weight2 = source.weights[2];
    out.weight3 = source.weights[3];
    out.tx = source.tangent[0];
    out.ty = source.tangent[1];
    out.tz = source.tangent[2];
    out.tw = source.tangent[3];
    out.sourceUv1U = source.texcoords[1][0];
    out.sourceUv1V = source.texcoords[1][1];
    out.sourceUv2U = source.texcoords[2][0];
    out.sourceUv2V = source.texcoords[2][1];
    return out;
}

IRenderBackend::WorldSceneSourceVertex sourceVertex(
    const engine::assets::lgpe::CanonicalVertex& source) {
    IRenderBackend::WorldSceneSourceVertex out{};
    for (std::size_t index = 0u; index < 3u; ++index) {
        out.texcoords[index] = source.texcoords[index + 1u];
        out.colors[index] = source.colors[index + 1u];
    }
    out.normalW = source.normalW;
    out.bitangent = source.bitangent;
    return out;
}

} // namespace

IRenderBackend::WorldSceneSourceMaterialFamily classifyMaterialFamily(
    const std::string& shaderGroup) {
    if (shaderGroup.starts_with("FieldGroundShader")) return Family::Ground;
    if (shaderGroup.starts_with("FieldGrassShader")) return Family::Grass;
    if (shaderGroup.starts_with("FieldCliffShader")) return Family::Cliff;
    if (shaderGroup.starts_with("FieldObjectShader")) return Family::Object;
    if (shaderGroup.starts_with("FieldRockShader")) return Family::Rock;
    if (shaderGroup.starts_with("FieldTreeShader")) return Family::Tree;
    if (shaderGroup.starts_with("FieldShadowOnlyShader")) {
        return Family::ShadowOnly;
    }
    return Family::Unknown;
}

bool prepareCanonicalScene(
    const engine::assets::lgpe::CanonicalScene& source,
    PreparedScene& out,
    std::string* outError) {
    PreparedScene prepared;
    prepared.stats.sourceMeshCount =
        static_cast<std::uint32_t>(source.meshes.size());
    prepared.stats.materialCount =
        static_cast<std::uint32_t>(source.materials.size());
    prepared.meshVertexStorage.resize(source.meshes.size());
    prepared.materialStorage.resize(source.materials.size());

    std::size_t polygonGroupCount = 0u;
    for (const auto& mesh : source.meshes) {
        polygonGroupCount += mesh.polygonGroups.size();
    }
    if (polygonGroupCount >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return fail(outError, "LGPE polygon-group count exceeds WorldScene limits");
    }
    prepared.stats.sourcePolygonGroupCount =
        static_cast<std::uint32_t>(polygonGroupCount);
    prepared.polygonGroupStorage.reserve(polygonGroupCount);

    struct TextureLookup {
        const engine::assets::lgpe::Texture* texture = nullptr;
        std::size_t storageIndex = 0u;
    };
    prepared.textureStorage.resize(source.textures.size());
    std::unordered_map<std::string, TextureLookup> textureByName;
    textureByName.reserve(source.textures.size());
    for (std::size_t textureIndex = 0u;
         textureIndex < source.textures.size();
         ++textureIndex) {
        const auto& texture = source.textures[textureIndex];
        buildMipStorage(texture, prepared.textureStorage[textureIndex]);
        textureByName.emplace(
            texture.name,
            TextureLookup{&texture, textureIndex});
    }

    std::vector<IRenderBackend::WorldSceneMaterialHandle> materialHandles;
    materialHandles.reserve(source.materials.size());
    for (std::size_t materialIndex = 0u;
         materialIndex < source.materials.size();
         ++materialIndex) {
        const auto& sourceMaterial = source.materials[materialIndex];
        auto& storage = prepared.materialStorage[materialIndex];
        storage.sourceMaterialIndex = sourceMaterial.sourceIndex;

        IRenderBackend::WorldSceneMaterial material{};
        material.sourceMaterialIndex = sourceMaterial.sourceIndex;
        material.sourceMaterialName = sourceMaterial.name;
        material.sourceShaderGroup = sourceMaterial.shaderGroup;
        material.sourceMetadataJson = sourceMaterial.sourceMetadataJson;
        material.sourceMaterialFamily =
            classifyMaterialFamily(sourceMaterial.shaderGroup);
        parseSourceSwitches(sourceMaterial, material);
        material.sourceTextureBindings.reserve(
            sourceMaterial.textureBindings.size());
        prepared.stats.sourceTextureBindingCount +=
            static_cast<std::uint32_t>(sourceMaterial.textureBindings.size());

        for (std::size_t bindingIndex = 0u;
             bindingIndex < sourceMaterial.textureBindings.size();
             ++bindingIndex) {
            const auto& sourceBinding =
                sourceMaterial.textureBindings[bindingIndex];
            IRenderBackend::WorldSceneSourceTextureBinding binding{};
            binding.textureName = sourceBinding.textureName;
            binding.samplerName = sourceBinding.samplerName;
            binding.textureType = sourceBinding.textureType;
            binding.textureUnit = sourceBinding.textureUnit;
            binding.wrapS = sourceBinding.wrapS;
            binding.wrapT = sourceBinding.wrapT;
            binding.wrapW = sourceBinding.wrapW;
            binding.minFilter = sourceBinding.minFilter;
            binding.magFilter = sourceBinding.magFilter;
            binding.scale = sourceBinding.scale;
            binding.translate = sourceBinding.translate;
            binding.resolvedWrapS = wrapMode(sourceBinding.wrapS);
            binding.resolvedWrapT = wrapMode(sourceBinding.wrapT);

            const auto textureFound =
                textureByName.find(sourceBinding.textureName);
            if (textureFound != textureByName.end()) {
                binding.sourceTextureIndex = static_cast<std::uint32_t>(
                    textureFound->second.storageIndex);
                binding.sourceContainerRelativePath =
                    textureFound->second.texture->sourceContainerRelativePath;
                binding.sourceFormat =
                    textureFound->second.texture->sourceFormat;
                binding.sourceIsSrgb =
                    textureFound->second.texture->sourceIsSrgb;
                binding.sourceArrayCount =
                    textureFound->second.texture->arrayCount;
                binding.sourceMipCount =
                    textureFound->second.texture->mipCount;
                const auto& mipStorage = prepared
                    .textureStorage[textureFound->second.storageIndex]
                    .mipLevels;
                if (!mipStorage.empty()) {
                    binding.baseRgba = mipStorage.front().rgba;
                    binding.baseWidth = mipStorage.front().width;
                    binding.baseHeight = mipStorage.front().height;
                    binding.mipLevels = mipStorage.data();
                    binding.mipLevelCount =
                        static_cast<std::uint32_t>(mipStorage.size());
                }
            }
            material.sourceTextureBindings.push_back(std::move(binding));
        }

        material.sourcePreviewBindingIndex = previewBindingIndex(
            material.sourceMaterialFamily,
            material.sourceTextureBindings);
        if (material.sourcePreviewBindingIndex >= 0) {
            const auto& preview = material.sourceTextureBindings[
                static_cast<std::size_t>(material.sourcePreviewBindingIndex)];
            material.textureKey =
                "lgpe:" + source.profileId + ":" + preview.textureName;
            material.textureCacheKey =
                authoredMipCacheKey(material.textureKey, preview);
            material.textureRgba = preview.baseRgba;
            material.textureWidth = preview.baseWidth;
            material.textureHeight = preview.baseHeight;
            material.textureMipLevels = preview.mipLevels;
            material.textureMipLevelCount = preview.mipLevelCount;
            material.textureWrapS = preview.resolvedWrapS;
            material.textureWrapT = preview.resolvedWrapT;
            ++prepared.stats.materialWithPreviewTextureCount;
        }
        if (configureFieldGroundSurface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldGroundSurfaceMaterialCount;
        } else if (configureFieldCliffSurface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldCliffSurfaceMaterialCount;
        } else if (configureFieldOverlaySurface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            if (material.materialMode ==
                engine::render::lgpe_field_overlay::
                    kRoadstoneMaterialMode) {
                ++prepared.stats.fieldRoadstoneSurfaceMaterialCount;
            } else {
                ++prepared.stats.fieldRockMaskSurfaceMaterialCount;
            }
        } else if (configureFieldFlowerSurface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldFlowerSurfaceMaterialCount;
        } else if (configureFieldRockSurface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldRockSurfaceMaterialCount;
        } else if (configureFieldSignSurface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldSignSurfaceMaterialCount;
        } else if (configureFieldGrassSurface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            if (material.materialMode ==
                engine::render::lgpe_field_grass::kShader01MaterialMode) {
                ++prepared.stats.fieldGrass01SurfaceMaterialCount;
            } else {
                ++prepared.stats.fieldGrass02SurfaceMaterialCount;
            }
        } else if (configureFieldSmallGrassSurface(
                       source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            if (material.materialMode ==
                engine::render::lgpe_field_small_grass::
                    kShader04MaterialMode) {
                ++prepared.stats.fieldGrass04SurfaceMaterialCount;
            } else {
                ++prepared.stats.fieldGrass05SurfaceMaterialCount;
            }
        } else if (configureFieldTree02Surface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldTree02SurfaceMaterialCount;
        } else if (configureFieldTree04Surface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldTree04SurfaceMaterialCount;
        } else if (configureFieldTree05Surface(source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldTree05SurfaceMaterialCount;
        } else if (configureFieldObjectTreeMikiSurface(
                       source.profileId, material)) {
            if (material.sourcePreviewBindingIndex >= 0 &&
                prepared.stats.materialWithPreviewTextureCount > 0u) {
                --prepared.stats.materialWithPreviewTextureCount;
            }
            ++prepared.stats.fieldObjectTreeMikiSurfaceMaterialCount;
        }

        const std::size_t familyIndex =
            static_cast<std::size_t>(material.sourceMaterialFamily);
        if (familyIndex < prepared.stats.materialFamilyCounts.size()) {
            ++prepared.stats.materialFamilyCounts[familyIndex];
        }
        materialHandles.push_back(
            shared_world_scene::ensureMaterial(
                prepared.registry,
                &prepared.materialStorage[materialIndex],
                material));
    }

    std::uint32_t instanceId = 1u;
    std::uint32_t groupOrdinal = 0u;
    for (std::size_t meshIndex = 0u;
         meshIndex < source.meshes.size();
         ++meshIndex) {
        const auto& sourceMesh = source.meshes[meshIndex];
        auto& meshStorage = prepared.meshVertexStorage[meshIndex];
        meshStorage.vertices.reserve(sourceMesh.vertices.size());
        meshStorage.sourceVertices.reserve(sourceMesh.vertices.size());
        for (const auto& vertex : sourceMesh.vertices) {
            meshStorage.vertices.push_back(baseVertex(vertex));
            meshStorage.sourceVertices.push_back(sourceVertex(vertex));
        }
        prepared.stats.sourceVertexCount += sourceMesh.vertices.size();
        const std::uint32_t meshSemanticMask = semanticMask(sourceMesh);
        using namespace engine::render::backend;
        prepared.stats.texCoord1MeshCount +=
            (meshSemanticMask & WorldSceneSourceVertexSemanticTexCoord1) != 0u;
        prepared.stats.texCoord2MeshCount +=
            (meshSemanticMask & WorldSceneSourceVertexSemanticTexCoord2) != 0u;
        prepared.stats.texCoord3MeshCount +=
            (meshSemanticMask & WorldSceneSourceVertexSemanticTexCoord3) != 0u;
        prepared.stats.color1MeshCount +=
            (meshSemanticMask & WorldSceneSourceVertexSemanticColor1) != 0u;
        prepared.stats.color2MeshCount +=
            (meshSemanticMask & WorldSceneSourceVertexSemanticColor2) != 0u;
        prepared.stats.color3MeshCount +=
            (meshSemanticMask & WorldSceneSourceVertexSemanticColor3) != 0u;

        for (std::size_t polygonGroupIndex = 0u;
             polygonGroupIndex < sourceMesh.polygonGroups.size();
             ++polygonGroupIndex, ++groupOrdinal) {
            const auto& sourceGroup =
                sourceMesh.polygonGroups[polygonGroupIndex];
            if (sourceGroup.primitiveType != "Triangles") {
                return fail(
                    outError,
                    "LGPE mesh '" + sourceMesh.name +
                        "' contains unsupported primitive type '" +
                        sourceGroup.primitiveType + "'");
            }
            if (sourceGroup.materialIndex >= source.materials.size()) {
                return fail(
                    outError,
                    "LGPE mesh '" + sourceMesh.name +
                        "' references an invalid material index");
            }
            prepared.polygonGroupStorage.push_back(PolygonGroupStorage{});
            auto& groupStorage = prepared.polygonGroupStorage.back();
            groupStorage.geometryCacheKey =
                "lgpe:" + source.profileId + ":mesh:" +
                std::to_string(sourceMesh.sourceIndex) + ":group:" +
                std::to_string(polygonGroupIndex);
            groupStorage.indices = sourceGroup.indices;

            const auto geometryHandle =
                shared_world_scene::ensureRigidGeometry(
                    prepared.registry,
                    &groupStorage,
                    groupStorage.geometryCacheKey.c_str(),
                    meshStorage.vertices.data(),
                    meshStorage.vertices.size(),
                    groupStorage.indices.data(),
                    groupStorage.indices.size(),
                    meshStorage.sourceVertices.data(),
                    meshStorage.sourceVertices.size(),
                    meshSemanticMask,
                    sourceMesh.sourceIndex,
                    static_cast<std::uint32_t>(polygonGroupIndex));

            const std::uint64_t triangleCount =
                sourceGroup.indices.size() / 3u;
            const auto& sourceMaterial =
                source.materials[sourceGroup.materialIndex];
            if (sourceMaterial.skipMainRendering) {
                ++prepared.stats.skippedMainPassPolygonGroupCount;
                prepared.stats.skippedMainPassTriangleCount += triangleCount;
                continue;
            }

            const auto objectHandle =
                shared_world_scene::ensureRenderObject(
                    prepared.registry,
                    geometryHandle,
                    materialHandles[sourceGroup.materialIndex],
                    shared_world_scene::PipelineVariant::OpaqueLit,
                    groupOrdinal);
            IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle{};
            instanceHandle.id = instanceId++;
            shared_world_scene::appendRigidInstance(
                prepared.frame,
                objectHandle,
                instanceHandle,
                sourceMesh.transform,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.0f);
            ++prepared.stats.mainPassPolygonGroupCount;
            prepared.stats.mainPassTriangleCount += triangleCount;
        }
    }

    out = std::move(prepared);
    if (outError) outError->clear();
    return true;
}

} // namespace game::runtime::lgpe_world_scene
