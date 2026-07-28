#pragma once

#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace game::runtime::lgpe_world_scene {

struct BuildStats {
    std::uint32_t sourceMeshCount = 0u;
    std::uint32_t sourcePolygonGroupCount = 0u;
    std::uint32_t mainPassPolygonGroupCount = 0u;
    std::uint32_t skippedMainPassPolygonGroupCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t materialWithPreviewTextureCount = 0u;
    std::uint32_t fieldGroundSurfaceMaterialCount = 0u;
    std::uint32_t fieldCliffSurfaceMaterialCount = 0u;
    std::uint32_t fieldGrass01SurfaceMaterialCount = 0u;
    std::uint32_t fieldGrass02SurfaceMaterialCount = 0u;
    std::uint32_t fieldGrass04SurfaceMaterialCount = 0u;
    std::uint32_t fieldGrass05SurfaceMaterialCount = 0u;
    std::uint32_t fieldRoadstoneSurfaceMaterialCount = 0u;
    std::uint32_t fieldRockMaskSurfaceMaterialCount = 0u;
    std::uint32_t fieldTree02SurfaceMaterialCount = 0u;
    std::uint32_t fieldTree04SurfaceMaterialCount = 0u;
    std::uint32_t fieldTree05SurfaceMaterialCount = 0u;
    std::uint32_t fieldObjectTreeMikiSurfaceMaterialCount = 0u;
    std::uint32_t sourceTextureBindingCount = 0u;
    std::uint32_t texCoord1MeshCount = 0u;
    std::uint32_t texCoord2MeshCount = 0u;
    std::uint32_t texCoord3MeshCount = 0u;
    std::uint32_t color1MeshCount = 0u;
    std::uint32_t color2MeshCount = 0u;
    std::uint32_t color3MeshCount = 0u;
    std::uint64_t sourceVertexCount = 0u;
    std::uint64_t mainPassTriangleCount = 0u;
    std::uint64_t skippedMainPassTriangleCount = 0u;
    std::array<std::uint32_t, 8> materialFamilyCounts{};
};

struct MeshVertexStorage {
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<IRenderBackend::WorldSceneSourceVertex> sourceVertices;
};

struct PolygonGroupStorage {
    std::string geometryCacheKey;
    std::vector<std::uint32_t> indices;
};

struct MaterialStorage {
    std::uint32_t sourceMaterialIndex = 0u;
};

struct TextureStorage {
    std::vector<std::vector<unsigned char>> mipRgba;
    std::vector<IRenderBackend::WorldTextureMipLevel> mipLevels;
};

// Owns every buffer referenced by registry/frame. Moving PreparedScene is safe;
// copying it is intentionally disabled because registry pointers refer to the
// owned vector allocations.
struct PreparedScene {
    PreparedScene() = default;
    PreparedScene(const PreparedScene&) = delete;
    PreparedScene& operator=(const PreparedScene&) = delete;
    PreparedScene(PreparedScene&&) noexcept = default;
    PreparedScene& operator=(PreparedScene&&) noexcept = default;

    shared_world_scene::WorldSceneRegistry registry;
    IRenderBackend::WorldSceneFrame frame;
    std::vector<MeshVertexStorage> meshVertexStorage;
    std::vector<PolygonGroupStorage> polygonGroupStorage;
    std::vector<MaterialStorage> materialStorage;
    std::vector<TextureStorage> textureStorage;
    BuildStats stats;
};

IRenderBackend::WorldSceneSourceMaterialFamily classifyMaterialFamily(
    const std::string& shaderGroup);

// Builds a renderer-facing scene from the canonical direct-source
// representation. It preserves source-declared secondary vertex channels,
// every source texture binding and exact shader-group metadata. The generic
// preview texture is deliberately only a diagnostic stand-in; family shader
// interpretation belongs to the following material-parity pass.
bool prepareCanonicalScene(
    const engine::assets::lgpe::CanonicalScene& source,
    PreparedScene& out,
    std::string* outError = nullptr);

} // namespace game::runtime::lgpe_world_scene
