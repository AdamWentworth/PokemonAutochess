#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) <= 0.0001f;
}

engine::assets::lgpe::CanonicalScene makeScene() {
    using namespace engine::assets::lgpe;

    CanonicalScene scene;
    scene.profileId = "adapter_fixture";

    Texture texture;
    texture.name = "grass_base";
    texture.sourceContainerRelativePath = "field/fixture.bntx";
    texture.sourceFormat = "R8G8B8A8_UNORM";
    texture.sourceIsSrgb = true;
    texture.arrayCount = 1u;
    texture.mipCount = 1u;
    TextureSubresource base;
    base.width = 1u;
    base.height = 1u;
    base.rgba8 = {17u, 34u, 51u, 255u};
    texture.subresources.push_back(std::move(base));
    scene.textures.push_back(std::move(texture));

    Material grass;
    grass.sourceIndex = 7u;
    grass.name = "grass_material";
    grass.shaderGroup = "FieldGrassShader02";
    grass.sourceMetadataJson =
        R"({"Switches":[{"Name":"SkipMainRendering","Value":false}]})";
    TextureBinding grassBinding;
    grassBinding.textureName = "grass_base";
    grassBinding.samplerName = "TextureMap01";
    grassBinding.textureType = "Unknown";
    grassBinding.textureUnit = 5;
    grassBinding.wrapS = "Clamp";
    grassBinding.wrapT = "Repeat";
    grass.textureBindings.push_back(std::move(grassBinding));
    scene.materials.push_back(std::move(grass));

    Material shadow;
    shadow.sourceIndex = 9u;
    shadow.name = "shadow_material";
    shadow.shaderGroup = "FieldShadowOnlyShader";
    shadow.skipMainRendering = true;
    shadow.sourceMetadataJson =
        R"({"Switches":[{"Name":"SkipMainRendering","Value":true}]})";
    scene.materials.push_back(std::move(shadow));

    Mesh mesh;
    mesh.sourceIndex = 12u;
    mesh.name = "fixture_mesh";
    mesh.transform[12] = 3.5f;
    mesh.attributes.push_back({0u, "POSITION", 0u, 3u});
    mesh.attributes.push_back({4u, "TEXCOORD_1", 0u, 2u});
    mesh.attributes.push_back({6u, "COLOR_1", 0u, 4u});
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        CanonicalVertex vertex;
        vertex.position = {
            static_cast<float>(index),
            static_cast<float>(index + 1u),
            static_cast<float>(index + 2u)};
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.tangent = {1.0f, 0.0f, 0.0f, -1.0f};
        vertex.bitangent = {0.0f, 0.0f, 1.0f, 0.0f};
        vertex.texcoords[0] = {0.1f, 0.2f};
        vertex.texcoords[1] = {0.25f + index, 0.5f};
        vertex.colors[0] = {0.6f, 0.7f, 0.8f, 0.9f};
        vertex.colors[1] = {0.75f, 0.5f, 0.25f, 1.0f};
        vertex.normalW = 0.5f;
        mesh.vertices.push_back(vertex);
    }
    mesh.polygonGroups.push_back({0u, "Triangles", {0u, 1u, 2u}});
    mesh.polygonGroups.push_back({1u, "Triangles", {0u, 2u, 1u}});
    scene.meshes.push_back(std::move(mesh));
    return scene;
}

} // namespace

bool test_lgpe_world_scene_adapter_contract(std::string& outFail) {
    using namespace engine::render::backend;
    using game::runtime::lgpe_world_scene::PreparedScene;
    using game::runtime::lgpe_world_scene::classifyMaterialFamily;
    using game::runtime::lgpe_world_scene::prepareCanonicalScene;

    if (classifyMaterialFamily("FieldGroundShader01") !=
            WorldSceneSourceMaterialFamily::Ground ||
        classifyMaterialFamily("FieldGrassShader05") !=
            WorldSceneSourceMaterialFamily::Grass ||
        classifyMaterialFamily("FieldCliffShader01") !=
            WorldSceneSourceMaterialFamily::Cliff ||
        classifyMaterialFamily("FieldObjectShader") !=
            WorldSceneSourceMaterialFamily::Object ||
        classifyMaterialFamily("FieldRockShader") !=
            WorldSceneSourceMaterialFamily::Rock ||
        classifyMaterialFamily("FieldTreeShader04") !=
            WorldSceneSourceMaterialFamily::Tree ||
        classifyMaterialFamily("FieldShadowOnlyShader") !=
            WorldSceneSourceMaterialFamily::ShadowOnly) {
        outFail = "LGPE shader groups were not classified into stable source families.";
        return false;
    }

    auto source = makeScene();
    PreparedScene prepared;
    std::string error;
    if (!prepareCanonicalScene(source, prepared, &error)) {
        outFail = "LGPE WorldScene adapter failed: " + error;
        return false;
    }
    if (prepared.stats.sourceMeshCount != 1u ||
        prepared.stats.sourcePolygonGroupCount != 2u ||
        prepared.stats.mainPassPolygonGroupCount != 1u ||
        prepared.stats.skippedMainPassPolygonGroupCount != 1u ||
        prepared.stats.sourceTextureBindingCount != 1u ||
        prepared.stats.texCoord1MeshCount != 1u ||
        prepared.stats.color1MeshCount != 1u ||
        prepared.stats.mainPassTriangleCount != 1u ||
        prepared.stats.skippedMainPassTriangleCount != 1u ||
        prepared.registry.geometries.size() != 2u ||
        prepared.registry.materials.size() != 2u ||
        prepared.registry.renderObjects.size() != 1u ||
        prepared.frame.drawClasses.size() != 1u) {
        outFail =
            "LGPE WorldScene adapter did not preserve groups while excluding the authored shadow-only group from the main pass.";
        return false;
    }

    const auto& geometry = prepared.registry.geometries[0];
    const std::uint32_t expectedMask =
        WorldSceneSourceVertexSemanticTexCoord1 |
        WorldSceneSourceVertexSemanticColor1 |
        WorldSceneSourceVertexSemanticNormalW |
        WorldSceneSourceVertexSemanticBitangent;
    if (!worldSceneGeometrySourceSemanticsValid(geometry) ||
        geometry.sourceMeshIndex != 12u ||
        geometry.sourcePolygonGroupIndex != 0u ||
        geometry.sourceVertexSemanticMask != expectedMask ||
        !near(geometry.vertices[0].u, 0.1f) ||
        !near(geometry.vertices[0].r, 0.6f) ||
        !near(geometry.sourceVertices[0].texcoords[0][0], 0.25f) ||
        !near(geometry.sourceVertices[0].colors[0][0], 0.75f) ||
        !near(geometry.sourceVertices[0].normalW, 0.5f) ||
        !near(geometry.sourceVertices[0].bitangent[2], 1.0f)) {
        outFail =
            "LGPE WorldScene adapter dropped or mislabeled a canonical vertex channel.";
        return false;
    }

    const auto& grass = prepared.registry.materials[0];
    const auto& shadow = prepared.registry.materials[1];
    if (grass.sourceMaterialIndex != 7u ||
        grass.sourceMaterialFamily != WorldSceneSourceMaterialFamily::Grass ||
        grass.sourceShaderGroup != "FieldGrassShader02" ||
        grass.sourceTextureBindings.size() != 1u ||
        grass.sourceTextureBindings[0].sourceTextureIndex != 0u ||
        grass.sourceTextureBindings[0].sourceFormat != "R8G8B8A8_UNORM" ||
        !grass.sourceTextureBindings[0].sourceIsSrgb ||
        grass.sourcePreviewBindingIndex != 0 ||
        !grass.textureRgba ||
        grass.textureRgba[0] != 17u ||
        grass.textureWrapS != 33071 ||
        grass.textureWrapT != 10497 ||
        (grass.sourceSwitchMask &
         WorldSceneSourceMaterialSwitchSkipMainRendering) == 0u ||
        (grass.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchSkipMainRendering) != 0u ||
        (shadow.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchSkipMainRendering) == 0u) {
        outFail =
            "LGPE WorldScene adapter did not retain source material, sampler, or visibility semantics.";
        return false;
    }

    const auto& instance = prepared.frame.drawClasses[0].instances[0];
    if (!near(instance.modelMatrix[12], 3.5f)) {
        outFail = "LGPE WorldScene adapter altered the canonical mesh transform.";
        return false;
    }

    // The prepared scene must not depend on CanonicalScene buffer lifetime.
    source = {};
    if (geometry.vertices[2].x != 2.0f ||
        grass.textureRgba[2] != 51u) {
        outFail = "LGPE WorldScene adapter retained dangling canonical-scene storage.";
        return false;
    }

    WorldSceneGeometry malformed = geometry;
    malformed.sourceVertexCount = geometry.vertexCount - 1u;
    if (worldSceneGeometrySourceSemanticsValid(malformed)) {
        outFail = "WorldScene accepted a mismatched extended vertex stream.";
        return false;
    }
    return true;
}
