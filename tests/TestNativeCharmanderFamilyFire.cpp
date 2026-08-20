#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "game/runtime/phlosion/PhlosionModelObject.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

namespace {

namespace render_model = game::runtime::render_model;
namespace mesh_support =
    game::runtime::shared_projected_unit_backend_mesh_support;

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

bool containsAll(
    const std::string& text,
    std::initializer_list<std::string_view> needles) {
    return std::all_of(
        needles.begin(),
        needles.end(),
        [&](std::string_view needle) {
            return text.find(needle) != std::string::npos;
        });
}

bool hasAnimatedComponent(
    const render_model::ContinuousMaterialAnimationTrack& track) {
    return std::any_of(
        track.components.begin(),
        track.components.end(),
        [](const auto& component) {
            return component.keys.size() >= 2u;
        });
}

bool qualifyFireModel(
    std::string_view stem,
    std::string& outFail) {
    const std::string modelIdentity =
        "assets/models/" + std::string(stem) + ".phmodel";
    const std::string objectPath =
        game::runtime::phlosion::objectPathForModel(modelIdentity);
    render_model::MeshData mesh;
    std::string loadError;
    if (!expect(
            game::runtime::phlosion::loadModelObject(
                objectPath,
                mesh,
                &loadError),
            "Could not load native Charmander-family fire object " +
                objectPath + ": " + loadError,
            outFail)) {
        return false;
    }

    std::vector<std::size_t> fireSubmeshes;
    for (std::size_t submeshIndex = 0u;
         submeshIndex < mesh.submeshMaterialModes.size();
         ++submeshIndex) {
        if (mesh.submeshMaterialModes[submeshIndex] ==
            render_model::kNativeLayeredUnlitMaterialMode) {
            fireSubmeshes.push_back(submeshIndex);
        }
    }
    if (!expect(
            fireSubmeshes.size() == 1u,
            std::string(stem) +
                " must contain exactly one native layered fire submesh; found " +
                std::to_string(fireSubmeshes.size()),
            outFail)) {
        return false;
    }
    const std::size_t fireSubmesh = fireSubmeshes.front();

    const auto hasTexture =
        [&](const std::vector<render_model::CachedTextureRgba>& textures) {
            return fireSubmesh < textures.size() &&
                textures[fireSubmesh].hasPixels();
        };
    if (!expect(
            fireSubmesh < mesh.submeshMaterialFlags.size() &&
                mesh.submeshMaterialFlags[fireSubmesh] > 1.5f &&
                hasTexture(mesh.submeshBaseTextures) &&
                hasTexture(mesh.submeshNormalTextures) &&
                hasTexture(mesh.submeshMetallicRoughnessTextures) &&
                fireSubmesh < mesh.submeshMaterialParams0.size() &&
                fireSubmesh < mesh.submeshMaterialParams1.size() &&
                fireSubmesh < mesh.submeshMaterialParams2.size() &&
                fireSubmesh < mesh.submeshMaterialParams3.size(),
            std::string(stem) +
                " lost its authored fire base, layer mask, displacement map, or material parameters",
            outFail)) {
        return false;
    }

    bool hasBaseTrack = false;
    bool hasDisplacementTrack = false;
    for (const auto& track : mesh.continuousMaterialAnimations) {
        if (track.submeshIndex != fireSubmesh ||
            !track.loop ||
            track.durationSec <= 0.0f ||
            track.sourceFrameRate <= 0.0f ||
            !hasAnimatedComponent(track)) {
            continue;
        }
        hasBaseTrack = hasBaseTrack ||
            track.parameter ==
                render_model::MaterialAnimationParameter::UvScaleOffset;
        hasDisplacementTrack = hasDisplacementTrack ||
            track.parameter ==
                render_model::MaterialAnimationParameter::UvScaleOffset3;
    }
    if (!expect(
            hasBaseTrack && hasDisplacementTrack,
            std::string(stem) +
                " lost one or both always-running source fire UV tracks",
            outFail)) {
        return false;
    }

    bool fireNodeNamed = false;
    bool fireSkinBound = false;
    if (fireSubmesh < mesh.submeshMeshIndex.size()) {
        const int meshIndex = mesh.submeshMeshIndex[fireSubmesh];
        if (meshIndex >= 0 &&
            static_cast<std::size_t>(meshIndex) < mesh.meshIndexToNode.size()) {
            const int nodeIndex =
                mesh.meshIndexToNode[static_cast<std::size_t>(meshIndex)];
            if (nodeIndex >= 0 &&
                static_cast<std::size_t>(nodeIndex) < mesh.nodeNames.size()) {
                std::string nodeName =
                    mesh.nodeNames[static_cast<std::size_t>(nodeIndex)];
                std::transform(
                    nodeName.begin(),
                    nodeName.end(),
                    nodeName.begin(),
                    [](unsigned char value) {
                        return static_cast<char>(std::tolower(value));
                    });
                fireNodeNamed = nodeName.find("fire") != std::string::npos;
                fireSkinBound =
                    static_cast<std::size_t>(nodeIndex) < mesh.nodeSkin.size() &&
                    mesh.nodeSkin[static_cast<std::size_t>(nodeIndex)] >= 0;
            }
        }
    }
    if (!fireSkinBound) {
        const std::size_t triangleCount = std::min(
            mesh.triangleSubmesh.size(),
            mesh.triangleSkinIndex.size());
        for (std::size_t triangle = 0u; triangle < triangleCount; ++triangle) {
            if (mesh.triangleSubmesh[triangle] == fireSubmesh &&
                mesh.triangleSkinIndex[triangle] >= 0) {
                fireSkinBound = true;
                break;
            }
        }
    }
    if (!expect(
            fireNodeNamed && fireSkinBound,
            std::string(stem) +
                " fire surface is no longer attached to a named, skinned authored fire carrier",
            outFail)) {
        return false;
    }

    for (int quality = 0; quality <= 3; ++quality) {
        const auto* materials =
            mesh_support::ensureFastTexturedMaterialTemplateCache(
                &mesh,
                mesh.submeshMaterialModes.size(),
                true,
                quality);
        if (!expect(
                materials &&
                    fireSubmesh < materials->materials.size() &&
                    materials->materials[fireSubmesh].materialMode ==
                        render_model::kNativeLayeredUnlitMaterialMode &&
                    materials->materials[fireSubmesh].normalTextureRgba != nullptr &&
                    materials->materials[fireSubmesh].metallicRoughnessTextureRgba != nullptr,
                std::string(stem) +
                    " native fire maps or material mode were dropped at graphics quality " +
                    std::to_string(quality),
                outFail)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool test_native_charmander_family_fire_contract(std::string& outFail) {
    constexpr std::array<std::string_view, 6u> models{{
        "0004_Charmander_SV",
        "0004_Charmander_SV_Shiny",
        "0005_Charmeleon_SV",
        "0005_Charmeleon_SV_Shiny",
        "0006_Charizard_SV",
        "0006_Charizard_SV_Shiny",
    }};
    for (std::string_view model : models) {
        if (!qualifyFireModel(model, outFail)) return false;
    }

#ifndef PAC_PHLOSION_ENGINE_SOURCE_DIR
    outFail = "PAC_PHLOSION_ENGINE_SOURCE_DIR is required for renderer fire qualification";
    return false;
#else
    const std::filesystem::path engineRoot = PAC_PHLOSION_ENGINE_SOURCE_DIR;
    const std::string openGl = readText(
        engineRoot /
        "src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp");
    const std::string d3d12 = readText(
        engineRoot /
        "src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp");
    const std::string vulkanVertex = readText(
        engineRoot / "assets/shaders/vulkan/world.vert");
    const std::string vulkanFragment = readText(
        engineRoot / "assets/shaders/vulkan/world.frag");
    const std::string vulkanIndirectVertex = readText(
        engineRoot / "assets/shaders/vulkan/world_indirect.vert");
    const std::string vulkanIndirectFragment = readText(
        engineRoot / "assets/shaders/vulkan/world_indirect.frag");

    if (!expect(
            containsAll(openGl, {
                "evalNativeLayeredUnlitDisplaced",
                "uMaterialMode > 26.5",
                "uNormalTexture",
                "uMetallicRoughnessTexture"}) &&
                containsAll(d3d12, {
                    "evalNativeLayeredUnlitDisplaced",
                    "uMaterialMode > 26.5f",
                    "gVertexDisplacementMap",
                    "gMetallicRoughnessTex"}) &&
                containsAll(vulkanVertex, {
                    "materialMode > 26.5",
                    "normalTexture",
                    "displacementUv"}) &&
                containsAll(vulkanFragment, {
                    "materialMode > 26.5",
                    "evaluateNativeLayeredUnlitDisplaced",
                    "metallicRoughnessTexture"}) &&
                containsAll(vulkanIndirectVertex, {
                    "materialMode > 26.5",
                    "normalTextures",
                    "displacementUv"}) &&
                containsAll(vulkanIndirectFragment, {
                    "materialMode > 26.5",
                    "evaluateNativeLayeredUnlitDisplaced",
                    "metallicRoughnessTextures"}),
            "OpenGL, D3D12, and both Vulkan submission paths must preserve native layered fire sampling and displacement",
            outFail)) {
        return false;
    }
#endif

    return true;
}
