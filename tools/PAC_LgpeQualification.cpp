#define SDL_MAIN_HANDLED

#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Paths.h"
#include "engine/platform/Window.h"
#include "engine/render/LgpeFieldCliffMaterial.h"
#include "engine/render/LgpeFieldGrassMaterial.h"
#include "engine/render/LgpeFieldGroundMaterial.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "engine/render/LgpeFieldObjectTreeMikiMaterial.h"
#include "engine/render/LgpeFieldTree02Material.h"
#include "engine/render/LgpeFieldTree05Material.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <SDL2/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct CameraPreset {
    const char* name = "middle";
    glm::vec3 eye{};
    glm::vec3 target{};
};

CameraPreset cameraPreset(const std::string& name) {
    if (name == "trunk") {
        return {
            "trunk",
            {2300.0f, 430.0f, -2100.0f},
            {3300.0f, 320.0f, -3000.0f}};
    }
    if (name == "tree") {
        return {"tree", {3300.0f, 850.0f, -900.0f}, {3300.0f, 180.0f, -2400.0f}};
    }
    if (name == "canopy") {
        return {
            "canopy",
            {2500.0f, 900.0f, -2050.0f},
            {2050.0f, 360.0f, -3000.0f}};
    }
    if (name == "vegetation") {
        return {
            "vegetation",
            {1950.0f, 760.0f, -850.0f},
            {1950.0f, 130.0f, -1800.0f}};
    }
    if (name == "south") {
        return {"south", {1800.0f, 700.0f, -150.0f}, {1800.0f, 90.0f, -900.0f}};
    }
    if (name == "north") {
        return {"north", {2500.0f, 850.0f, -2350.0f}, {2500.0f, 210.0f, -3200.0f}};
    }
    return {"middle", {2200.0f, 800.0f, -1200.0f}, {2200.0f, 150.0f, -2100.0f}};
}

const IRenderBackend::WorldSceneRenderObject* renderObject(
    const game::runtime::shared_world_scene::WorldSceneRegistry& registry,
    IRenderBackend::WorldSceneRenderObjectHandle handle) {
    if (handle.id == 0u || handle.id > registry.renderObjects.size()) return nullptr;
    return &registry.renderObjects[handle.id - 1u];
}

const IRenderBackend::WorldSceneGeometry* geometry(
    const game::runtime::shared_world_scene::WorldSceneRegistry& registry,
    IRenderBackend::WorldSceneGeometryHandle handle) {
    if (handle.id == 0u || handle.id > registry.geometries.size()) return nullptr;
    return &registry.geometries[handle.id - 1u];
}

const IRenderBackend::WorldSceneMaterial* material(
    const game::runtime::shared_world_scene::WorldSceneRegistry& registry,
    IRenderBackend::WorldSceneMaterialHandle handle) {
    if (handle.id == 0u || handle.id > registry.materials.size()) return nullptr;
    return &registry.materials[handle.id - 1u];
}

} // namespace

int main(int argc, char** argv) {
    const std::string virtualRoot =
        argc >= 2 && argv[1] && argv[1][0] != '\0'
        ? argv[1]
        : "cache/lgpe/route1";
    const CameraPreset camera =
        cameraPreset(argc >= 3 && argv[2] ? argv[2] : "middle");
    const std::string materialFilter =
        argc >= 4 && argv[3] ? argv[3] : "";

    game::assets::DevAssetStore store(engine::paths::dataRoot());
    engine::assets::lgpe::CanonicalScene source;
    std::string error;
    if (!engine::assets::lgpe::loadCanonicalScene(
            store, virtualRoot, source, &error)) {
        std::cerr << "[PAC_LgpeQualification] FAIL root=" << virtualRoot
                  << " error=" << error << '\n';
        return 1;
    }

    game::runtime::lgpe_world_scene::PreparedScene scene;
    if (!game::runtime::lgpe_world_scene::prepareCanonicalScene(
            source, scene, &error)) {
        std::cerr << "[PAC_LgpeQualification] FAIL root=" << virtualRoot
                  << " world_scene_error=" << error << '\n';
        return 1;
    }

    try {
        Window window(
            "LGPE Route 1 direct-source qualification",
            1280,
            720,
            Window::GraphicsApi::OpenGL,
            false);
        if (!gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            throw std::runtime_error("Failed to initialize GLAD.");
        }

        int width = 0;
        int height = 0;
        window.getDrawableSize(width, height);
        width = width > 0 ? width : 1280;
        height = height > 0 ? height : 720;
        glViewport(0, 0, width, height);

        OpenGLRenderBackend renderer;
        renderer.onResize(width, height);
        renderer.prewarmWorldRenderAssets();

        const glm::mat4 projection = glm::perspective(
            glm::radians(35.0f),
            static_cast<float>(width) / static_cast<float>(height),
            10.0f,
            10000.0f);
        const glm::mat4 view = glm::lookAt(
            camera.eye, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 viewProjection = projection * view;
        const glm::vec3 forward = glm::normalize(camera.target - camera.eye);
        const std::array<float, 3> cameraWorld{
            camera.eye.x, camera.eye.y, camera.eye.z};
        const std::array<float, 3> cameraForward{
            forward.x, forward.y, forward.z};
        const std::array<float, 3> cameraTarget{
            camera.target.x, camera.target.y, camera.target.z};

        std::uint32_t groundGroups = 0u;
        std::uint64_t groundTriangles = 0u;
        std::uint32_t cliffGroups = 0u;
        std::uint64_t cliffTriangles = 0u;
        std::uint32_t grass01Groups = 0u;
        std::uint64_t grass01Triangles = 0u;
        std::uint32_t grass02Groups = 0u;
        std::uint64_t grass02Triangles = 0u;
        std::uint32_t grass04Groups = 0u;
        std::uint64_t grass04Triangles = 0u;
        std::uint32_t grass05Groups = 0u;
        std::uint64_t grass05Triangles = 0u;
        std::uint32_t tree02Groups = 0u;
        std::uint64_t tree02Triangles = 0u;
        std::uint32_t tree04Groups = 0u;
        std::uint64_t tree04Triangles = 0u;
        std::uint32_t tree05Groups = 0u;
        std::uint64_t tree05Triangles = 0u;
        std::uint32_t treeMikiGroups = 0u;
        std::uint64_t treeMikiTriangles = 0u;
        std::array<std::uint32_t, 6> groundMipCounts{};
        std::array<std::uint32_t, 5> cliffMipCounts{};
        std::array<std::uint32_t, 6> grass01MipCounts{};
        std::array<std::uint32_t, 6> grass02MipCounts{};
        std::array<std::uint32_t, 4> grass04MipCounts{};
        std::array<std::uint32_t, 6> grass05MipCounts{};
        std::array<std::uint32_t, 5> tree02MipCounts{};
        std::array<std::uint32_t, 6> tree04MipCounts{};
        std::array<std::uint32_t, 6> tree05MipCounts{};
        std::array<std::uint32_t, 4> treeMikiMipCounts{};
        renderer.beginFrame(0.075f, 0.09f, 0.065f, 1.0f);
        renderer.beginWorldIndexedBatchSubmission();
        for (const auto& drawClass : scene.frame.drawClasses) {
            const auto* object = renderObject(scene.registry, drawClass.objectHandle);
            if (!object) continue;
            const auto* mesh = geometry(scene.registry, object->geometryHandle);
            const auto* surface = material(scene.registry, object->materialHandle);
            if (!mesh || !surface) {
                continue;
            }
            if (!materialFilter.empty() &&
                surface->sourceMaterialName != materialFilter) {
                continue;
            }
            const bool isGround =
                surface->materialMode ==
                engine::render::lgpe_field_ground::kMaterialMode;
            const bool isCliff =
                surface->materialMode ==
                engine::render::lgpe_field_cliff::kMaterialMode;
            const bool isGrass01 =
                surface->materialMode ==
                engine::render::lgpe_field_grass::kShader01MaterialMode;
            const bool isGrass02 =
                surface->materialMode ==
                engine::render::lgpe_field_grass::kShader02MaterialMode;
            const bool isGrass04 =
                surface->materialMode ==
                engine::render::lgpe_field_small_grass::
                    kShader04MaterialMode;
            const bool isGrass05 =
                surface->materialMode ==
                engine::render::lgpe_field_small_grass::
                    kShader05MaterialMode;
            const bool isTree02 =
                surface->materialMode ==
                engine::render::lgpe_field_tree02::kMaterialMode;
            const bool isTree04 =
                surface->sourceShaderGroup == "FieldTreeShader04" &&
                surface->materialMode ==
                    engine::render::lgpe_field_tree05::kMaterialMode;
            const bool isTree05 =
                surface->sourceShaderGroup == "FieldTreeShader05" &&
                surface->materialMode ==
                engine::render::lgpe_field_tree05::kMaterialMode;
            const bool isTreeMiki =
                surface->materialMode ==
                engine::render::lgpe_field_object_tree_miki::kMaterialMode;
            if (!isGround && !isCliff && !isGrass01 && !isGrass02 &&
                !isGrass04 && !isGrass05 && !isTree02 && !isTree04 &&
                !isTree05 && !isTreeMiki) {
                continue;
            }

            IRenderBackend::WorldTextureData texture =
                game::runtime::shared_world_scene::makeWorldSceneTextureData(
                    *surface,
                    cameraWorld.data(),
                    cameraForward.data(),
                    cameraTarget.data());
            if (isGround) {
                groundMipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.metallicRoughnessMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.emissiveMipLevelCount,
                    texture.environmentMipLevelCount};
            } else if (isCliff) {
                cliffMipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.metallicRoughnessMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.emissiveMipLevelCount};
            } else if (isGrass01) {
                grass01MipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.metallicRoughnessMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.emissiveMipLevelCount,
                    texture.environmentMipLevelCount};
            } else if (isGrass02) {
                grass02MipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.metallicRoughnessMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.emissiveMipLevelCount,
                    texture.environmentMipLevelCount};
            } else if (isGrass04) {
                grass04MipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.metallicRoughnessMipLevelCount,
                    texture.occlusionMipLevelCount};
            } else if (isGrass05) {
                grass05MipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.metallicRoughnessMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.emissiveMipLevelCount,
                    texture.environmentMipLevelCount};
            } else if (isTree02) {
                tree02MipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.emissiveMipLevelCount,
                    texture.environmentMipLevelCount};
            } else if (isTree04) {
                tree04MipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.metallicRoughnessMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.emissiveMipLevelCount,
                    texture.environmentMipLevelCount};
            } else if (isTree05) {
                tree05MipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.metallicRoughnessMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.emissiveMipLevelCount,
                    texture.environmentMipLevelCount};
            } else {
                treeMikiMipCounts = {
                    texture.mipLevelCount,
                    texture.normalMipLevelCount,
                    texture.occlusionMipLevelCount,
                    texture.environmentMipLevelCount};
            }
            renderer.prewarmWorldTextureData(&texture);
            for (const auto& instance : drawClass.instances) {
                texture.modelMatrix = instance.modelMatrix;
                texture.vertexColorMulR = instance.vertexColorMulR;
                texture.vertexColorMulG = instance.vertexColorMulG;
                texture.vertexColorMulB = instance.vertexColorMulB;
                texture.vertexColorMulA = instance.vertexColorMulA;
                renderer.drawWorldIndexedMeshTexturedCached(
                    mesh->geometryCacheKey.c_str(),
                    mesh->vertices,
                    mesh->vertexCount,
                    mesh->indices,
                    mesh->indexCount,
                    &texture,
                    glm::value_ptr(viewProjection),
                    width,
                    height);
                if (isGround) {
                    ++groundGroups;
                    groundTriangles += mesh->indexCount / 3u;
                } else if (isCliff) {
                    ++cliffGroups;
                    cliffTriangles += mesh->indexCount / 3u;
                } else if (isGrass01) {
                    ++grass01Groups;
                    grass01Triangles += mesh->indexCount / 3u;
                } else if (isGrass02) {
                    ++grass02Groups;
                    grass02Triangles += mesh->indexCount / 3u;
                } else if (isGrass04) {
                    ++grass04Groups;
                    grass04Triangles += mesh->indexCount / 3u;
                } else if (isGrass05) {
                    ++grass05Groups;
                    grass05Triangles += mesh->indexCount / 3u;
                } else if (isTree02) {
                    ++tree02Groups;
                    tree02Triangles += mesh->indexCount / 3u;
                } else if (isTree04) {
                    ++tree04Groups;
                    tree04Triangles += mesh->indexCount / 3u;
                } else if (isTree05) {
                    ++tree05Groups;
                    tree05Triangles += mesh->indexCount / 3u;
                } else {
                    ++treeMikiGroups;
                    treeMikiTriangles += mesh->indexCount / 3u;
                }
            }
        }
        renderer.endWorldIndexedBatchSubmission();
        renderer.endFrame();

        std::size_t textureSubresourceCount = 0u;
        for (const auto& texture : source.textures) {
            textureSubresourceCount += texture.subresources.size();
        }
        std::cout
            << "[PAC_LgpeQualification] PASS"
            << " profile=" << source.profileId
            << " preset=" << camera.name
            << " material_filter="
            << (materialFilter.empty() ? "all" : materialFilter)
            << " material_modes="
            << static_cast<unsigned>(
                   engine::render::lgpe_field_ground::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_cliff::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_tree05::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_object_tree_miki::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_tree02::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_grass::kShader02MaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_grass::kShader01MaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_small_grass::
                       kShader04MaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_small_grass::
                       kShader05MaterialMode)
            << " ground_groups=" << groundGroups
            << " ground_triangles=" << groundTriangles
            << " cliff_groups=" << cliffGroups
            << " cliff_triangles=" << cliffTriangles
            << " grass01_groups=" << grass01Groups
            << " grass01_triangles=" << grass01Triangles
            << " grass02_groups=" << grass02Groups
            << " grass02_triangles=" << grass02Triangles
            << " grass04_groups=" << grass04Groups
            << " grass04_triangles=" << grass04Triangles
            << " grass05_groups=" << grass05Groups
            << " grass05_triangles=" << grass05Triangles
            << " tree02_groups=" << tree02Groups
            << " tree02_triangles=" << tree02Triangles
            << " tree04_groups=" << tree04Groups
            << " tree04_triangles=" << tree04Triangles
            << " tree05_groups=" << tree05Groups
            << " tree05_triangles=" << tree05Triangles
            << " tree_miki_groups=" << treeMikiGroups
            << " tree_miki_triangles=" << treeMikiTriangles
            << " authored_texture_subresources="
            << textureSubresourceCount
            << " ground_role_mips="
            << groundMipCounts[0] << ','
            << groundMipCounts[1] << ','
            << groundMipCounts[2] << ','
            << groundMipCounts[3] << ','
            << groundMipCounts[4] << ','
            << groundMipCounts[5]
            << " cliff_role_mips="
            << cliffMipCounts[0] << ','
            << cliffMipCounts[1] << ','
            << cliffMipCounts[2] << ','
            << cliffMipCounts[3] << ','
            << cliffMipCounts[4]
            << " grass01_role_mips="
            << grass01MipCounts[0] << ','
            << grass01MipCounts[1] << ','
            << grass01MipCounts[2] << ','
            << grass01MipCounts[3] << ','
            << grass01MipCounts[4] << ','
            << grass01MipCounts[5]
            << " grass02_role_mips="
            << grass02MipCounts[0] << ','
            << grass02MipCounts[1] << ','
            << grass02MipCounts[2] << ','
            << grass02MipCounts[3] << ','
            << grass02MipCounts[4] << ','
            << grass02MipCounts[5]
            << " grass04_role_mips="
            << grass04MipCounts[0] << ','
            << grass04MipCounts[1] << ','
            << grass04MipCounts[2] << ','
            << grass04MipCounts[3]
            << " grass05_role_mips="
            << grass05MipCounts[0] << ','
            << grass05MipCounts[1] << ','
            << grass05MipCounts[2] << ','
            << grass05MipCounts[3] << ','
            << grass05MipCounts[4] << ','
            << grass05MipCounts[5]
            << " tree02_role_mips="
            << tree02MipCounts[0] << ','
            << tree02MipCounts[1] << ','
            << tree02MipCounts[2] << ','
            << tree02MipCounts[3] << ','
            << tree02MipCounts[4]
            << " tree04_role_mips="
            << tree04MipCounts[0] << ','
            << tree04MipCounts[1] << ','
            << tree04MipCounts[2] << ','
            << tree04MipCounts[3] << ','
            << tree04MipCounts[4] << ','
            << tree04MipCounts[5]
            << " tree05_role_mips="
            << tree05MipCounts[0] << ','
            << tree05MipCounts[1] << ','
            << tree05MipCounts[2] << ','
            << tree05MipCounts[3] << ','
            << tree05MipCounts[4] << ','
            << tree05MipCounts[5]
            << " tree_miki_role_mips="
            << treeMikiMipCounts[0] << ','
            << treeMikiMipCounts[1] << ','
            << treeMikiMipCounts[2] << ','
            << treeMikiMipCounts[3]
            << '\n';
        renderer.shutdown();
        return !materialFilter.empty() ||
                   (groundGroups > 0u && cliffGroups > 0u &&
                    grass01Groups > 0u && grass02Groups > 0u &&
                    grass04Groups > 0u && grass05Groups > 0u &&
                    tree02Groups > 0u && tree04Groups > 0u &&
                    tree05Groups > 0u && treeMikiGroups > 0u)
            ? 0
            : 2;
    } catch (const std::exception& ex) {
        std::cerr << "[PAC_LgpeQualification] FAIL exception="
                  << ex.what() << '\n';
        return 1;
    }
}
