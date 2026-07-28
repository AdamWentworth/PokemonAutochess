#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "engine/render/LgpeFieldGroundMaterial.h"

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

engine::assets::lgpe::CanonicalScene makeGroundScene() {
    using namespace engine::assets::lgpe;

    CanonicalScene scene;
    scene.profileId = "ground_fixture";
    const auto addTexture = [&scene](const char* name, unsigned char value) {
        Texture texture;
        texture.name = name;
        texture.sourceContainerRelativePath =
            std::string("field/") + name + ".bntx";
        texture.sourceFormat = "BC1_UNORM_SRGB";
        texture.sourceIsSrgb = true;
        texture.arrayCount = 1u;
        texture.mipCount = 1u;
        TextureSubresource base;
        base.width = 1u;
        base.height = 1u;
        base.rgba8 = {value, value, value, 255u};
        texture.subresources.push_back(std::move(base));
        scene.textures.push_back(std::move(texture));
    };
    addTexture("ground01", 10u);
    addTexture("ground02", 20u);
    addTexture("grass02", 30u);
    addTexture("grass01", 40u);
    addTexture("blend", 50u);
    addTexture("grass_blend", 60u);

    Material material;
    material.sourceIndex = 19u;
    material.name = "grass01_com_soil01_com";
    material.shaderGroup = "FieldGroundShader01";
    material.sourceMetadataJson =
        R"({"Colors":[{"Name":"Alpha_light","Color":{"R":0.337170243,"G":1.00002408,"B":0.194618359}}]})";
    const auto addBinding =
        [&material](const char* textureName, const char* samplerName) {
            TextureBinding binding;
            binding.textureName = textureName;
            binding.samplerName = samplerName;
            binding.wrapS = "Repeat";
            binding.wrapT = "Repeat";
            material.textureBindings.push_back(std::move(binding));
        };
    addBinding("grass_blend", "GrassBlendTex");
    addBinding("ground02", "GroundTex02");
    addBinding("grass02", "GrassTex02");
    addBinding("grass01", "GrassTex01");
    addBinding("blend", "BlendTex");
    addBinding("ground01", "GroundTex01");
    scene.materials.push_back(std::move(material));

    Mesh mesh;
    mesh.sourceIndex = 2u;
    mesh.name = "ground_mesh";
    mesh.attributes.push_back({0u, "POSITION", 0u, 3u});
    mesh.attributes.push_back({5u, "TEXCOORD_2", 0u, 2u});
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        CanonicalVertex vertex;
        vertex.position = {static_cast<float>(index), 0.0f, 0.0f};
        vertex.texcoords[0] = {0.2f, 0.3f};
        vertex.texcoords[2] = {0.6f, 0.7f};
        mesh.vertices.push_back(vertex);
    }
    mesh.polygonGroups.push_back({0u, "Triangles", {0u, 1u, 2u}});
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

    auto groundSource = makeGroundScene();
    PreparedScene groundPrepared;
    if (!prepareCanonicalScene(groundSource, groundPrepared, &error)) {
        outFail = "LGPE ground material fixture failed: " + error;
        return false;
    }
    const auto& ground = groundPrepared.registry.materials[0];
    const auto& groundGeometry = groundPrepared.registry.geometries[0];
    if (groundPrepared.stats.fieldGroundSurfaceMaterialCount != 1u ||
        groundPrepared.stats.materialWithPreviewTextureCount != 0u ||
        ground.materialMode !=
            engine::render::lgpe_field_ground::kMaterialMode ||
        ground.textureRgba[0] != 10u ||
        ground.normalTextureRgba[0] != 20u ||
        ground.metallicRoughnessTextureRgba[0] != 30u ||
        ground.occlusionTextureRgba[0] != 40u ||
        ground.emissiveTextureRgba[0] != 50u ||
        ground.environmentTextureRgba[0] != 60u ||
        ground.textureSrgb == 0u ||
        ground.normalTextureSrgb == 0u ||
        ground.metallicRoughnessTextureSrgb == 0u ||
        ground.occlusionTextureSrgb == 0u ||
        ground.emissiveTextureSrgb == 0u ||
        ground.environmentTextureSrgb == 0u ||
        !near(ground.emissiveFactorR, 0.337170243f) ||
        !near(ground.emissiveFactorG, 1.00002408f) ||
        !near(ground.emissiveFactorB, 0.194618359f) ||
        !near(groundGeometry.vertices[0].sourceUv2U, 0.6f) ||
        !near(groundGeometry.vertices[0].sourceUv2V, 0.7f)) {
        outFail =
            "FieldGroundShader01 did not bind its six authored surface roles, Alpha_light, and UV2 channel.";
        return false;
    }

    engine::render::lgpe_field_ground::SurfaceInputs surface{};
    surface.groundTex01 = {0.1f, 0.1f, 0.1f, 1.0f};
    surface.groundTex02 = {0.3f, 0.3f, 0.3f, 1.0f};
    surface.grassTex02 = {0.5f, 0.5f, 0.5f, 1.0f};
    surface.grassTex01 = {0.7f, 0.7f, 0.7f, 1.0f};
    surface.blendTexRed = 0.25f;
    surface.grassBlendTex = {0.8f, 0.8f, 0.8f, 0.75f};
    surface.vertexColor = {0.5f, 0.5f, 0.5f, 0.25f};
    surface.alphaLight = {0.1f, 0.1f, 0.1f};
    const auto evaluated =
        engine::render::lgpe_field_ground::evaluateSurface(surface);
    if (!near(evaluated[0], 0.255f) ||
        !near(evaluated[1], 0.255f) ||
        !near(evaluated[2], 0.255f) ||
        !near(evaluated[3], 1.0f)) {
        outFail =
            "The deterministic FieldGroundShader01 surface oracle changed blend order.";
        return false;
    }
    return true;
}
