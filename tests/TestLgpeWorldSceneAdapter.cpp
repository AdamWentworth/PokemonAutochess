#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "engine/render/LgpeFieldCliffMaterial.h"
#include "engine/render/LgpeFieldGrassMaterial.h"
#include "engine/render/LgpeFieldGroundMaterial.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "engine/render/LgpeFieldObjectTreeMikiMaterial.h"
#include "engine/render/LgpeFieldTree02Material.h"
#include "engine/render/LgpeFieldTree05Material.h"

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
    texture.mipCount = 2u;
    TextureSubresource base;
    base.width = 2u;
    base.height = 2u;
    base.rgba8 = {
        17u, 34u, 51u, 255u,
        17u, 34u, 51u, 255u,
        17u, 34u, 51u, 255u,
        17u, 34u, 51u, 255u};
    texture.subresources.push_back(std::move(base));
    TextureSubresource mip1;
    mip1.mipLevel = 1u;
    mip1.width = 1u;
    mip1.height = 1u;
    mip1.rgba8 = {91u, 92u, 93u, 255u};
    texture.subresources.push_back(std::move(mip1));
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

engine::assets::lgpe::CanonicalScene makeCliffScene() {
    using namespace engine::assets::lgpe;

    CanonicalScene scene;
    scene.profileId = "cliff_fixture";
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
    addTexture("cliff01", 10u);
    addTexture("ground02", 20u);
    addTexture("ground01", 30u);
    addTexture("border", 40u);
    addTexture("blend", 50u);

    Material material;
    material.sourceIndex = 18u;
    material.name = "cliff01_com_grass01_com";
    material.shaderGroup = "FieldCliffShader01";
    material.sourceMetadataJson = R"({
        "Values":[
            {"Name":"RimLight_Min","Value":0.5},
            {"Name":"RimLight_Max","Value":1.0},
            {"Name":"RimLight_Strength","Value":0.5}
        ],
        "Colors":[
            {"Name":"RimColor","Color":{"R":0.278898,"G":0.205076,"B":0.031895}}
        ],
        "Common":{"Values":[
            {"Name":"GroundBlend","Value":2},
            {"Name":"Tex01_UV","Value":1},
            {"Name":"Tex00_UV","Value":0},
            {"Name":"MipMapBias","Value":-2}
        ]}
    })";
    const auto addBinding =
        [&material](const char* textureName, const char* samplerName) {
            TextureBinding binding;
            binding.textureName = textureName;
            binding.samplerName = samplerName;
            binding.wrapS = "Repeat";
            binding.wrapT = "Repeat";
            material.textureBindings.push_back(std::move(binding));
        };
    addBinding("blend", "BlendTex");
    addBinding("ground02", "GroundTex02");
    addBinding("cliff01", "CliffTex01");
    addBinding("border", "BorderTex");
    addBinding("ground01", "GroundTex01");
    scene.materials.push_back(std::move(material));

    Mesh mesh;
    mesh.sourceIndex = 3u;
    mesh.name = "cliff_mesh";
    mesh.attributes.push_back({0u, "POSITION", 0u, 3u});
    mesh.attributes.push_back({4u, "TEXCOORD_1", 0u, 2u});
    mesh.attributes.push_back({5u, "TEXCOORD_2", 0u, 2u});
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        CanonicalVertex vertex;
        vertex.position = {static_cast<float>(index), 0.0f, 0.0f};
        vertex.texcoords[0] = {0.2f, 0.3f};
        vertex.texcoords[1] = {0.4f, 0.5f};
        vertex.texcoords[2] = {0.6f, 0.7f};
        mesh.vertices.push_back(vertex);
    }
    mesh.polygonGroups.push_back({0u, "Triangles", {0u, 1u, 2u}});
    scene.meshes.push_back(std::move(mesh));
    return scene;
}

engine::assets::lgpe::CanonicalScene makeGrassScene(bool rimVariant) {
    using namespace engine::assets::lgpe;

    CanonicalScene scene;
    scene.profileId = rimVariant ? "grass01_fixture" : "grass02_fixture";
    const auto addTexture =
        [&scene](const char* name, unsigned char value, bool srgb) {
            Texture texture;
            texture.name = name;
            texture.sourceContainerRelativePath =
                std::string("field/") + name + ".bntx";
            texture.sourceFormat =
                srgb ? "BC3_UNORM_SRGB" : "R8G8B8A8_UNORM";
            texture.sourceIsSrgb = srgb;
            texture.arrayCount = 1u;
            texture.mipCount = 1u;
            TextureSubresource base;
            base.width = 1u;
            base.height = 1u;
            base.rgba8 = {value, value, value, 255u};
            texture.subresources.push_back(std::move(base));
            scene.textures.push_back(std::move(texture));
        };
    addTexture("texture_map01", 10u, true);
    addTexture("texture_map02", 20u, true);
    addTexture("green_hikari", 30u, true);
    addTexture("green_blend", 40u, true);
    addTexture("highlight", 50u, true);
    addTexture("shadow_toon", 60u, false);
    addTexture("light_projection", 70u, true);
    addTexture("depth_buffer", 80u, false);

    Material material;
    material.sourceIndex = rimVariant ? 14u : 13u;
    material.name = rimVariant ? "grass01_com_002" : "grass01_com";
    material.shaderGroup =
        rimVariant ? "FieldGrassShader01" : "FieldGrassShader02";
    material.sourceMetadataJson = rimVariant
        ? R"({
            "Values":[
                {"Name":"RimLight_Min","Value":0.433333337},
                {"Name":"RimLight_Max","Value":1.0},
                {"Name":"RimLight_Strength","Value":1.0},
                {"Name":"DiscardValuie","Value":0.85},
                {"Name":"ShadowSampingScale","Value":2.0},
                {"Name":"ShadowBias","Value":0.003},
                {"Name":"LightProjMapTranslateU","Value":0.0},
                {"Name":"LightProjMapTranslateV","Value":0.0},
                {"Name":"LightProjMapScaleU","Value":0.5},
                {"Name":"LightProjMapScaleV","Value":0.5},
                {"Name":"LightProjMapColorPow","Value":1.0}
            ],
            "Colors":[
                {"Name":"Color","Color":{"R":0.210406289,"G":0.295774639,"B":0.0872536451}},
                {"Name":"RimColor","Color":{"R":0.0,"G":0.25,"B":0.204700053}},
                {"Name":"Shadow_Color","Color":{"R":0.235,"G":0.361,"B":0.391}}
            ],
            "Common":{
                "Switches":[
                    {"Name":"DiscardEnable","Value":true},
                    {"Name":"RimLight","Value":true},
                    {"Name":"CloudEnable","Value":true},
                    {"Name":"CastShadow","Value":false},
                    {"Name":"ReceiveShadow","Value":true}
                ],
                "Values":[
                    {"Name":"UVSet01","Value":1},
                    {"Name":"UVSet0","Value":0},
                    {"Name":"MipMapBias","Value":0}
                ]
            }
        })"
        : R"({
            "Values":[
                {"Name":"DiscardValuie","Value":0.85},
                {"Name":"ShadowSampingScale","Value":2.0},
                {"Name":"ShadowBias","Value":0.003},
                {"Name":"LightProjMapTranslateU","Value":0.0},
                {"Name":"LightProjMapTranslateV","Value":0.0},
                {"Name":"LightProjMapScaleU","Value":0.5},
                {"Name":"LightProjMapScaleV","Value":0.5},
                {"Name":"LightProjMapColorPow","Value":1.0},
                {"Name":"OnGameColorVal","Value":1.0},
                {"Name":"OnGameAlpha","Value":1.0}
            ],
            "Colors":[
                {"Name":"Color","Color":{"R":0.133802816,"G":0.133802816,"B":0.133802816}},
                {"Name":"Shadow_Color","Color":{"R":0.235,"G":0.361,"B":0.391}},
                {"Name":"OnGameColor","Color":{"R":1.0,"G":1.0,"B":1.0}}
            ],
            "Common":{
                "Switches":[
                    {"Name":"DiscardEnable","Value":true},
                    {"Name":"CloudEnable","Value":true},
                    {"Name":"CastShadow","Value":false},
                    {"Name":"ReceiveShadow","Value":true}
                ],
                "Values":[
                    {"Name":"UVSet01","Value":1},
                    {"Name":"UVSet0","Value":0},
                    {"Name":"MipMapBias","Value":-2}
                ]
            }
        })";
    const auto addBinding =
        [&material](const char* textureName, const char* samplerName) {
            TextureBinding binding;
            binding.textureName = textureName;
            binding.samplerName = samplerName;
            binding.wrapS =
                std::string(samplerName) == "ShadowToonTable"
                ? "Clamp"
                : "Repeat";
            binding.wrapT = binding.wrapS;
            material.textureBindings.push_back(std::move(binding));
        };
    addBinding("green_hikari", "green_hikari");
    addBinding("texture_map02", "TextureMap02");
    addBinding("green_blend", "green_blend");
    addBinding("highlight", "Hilight");
    addBinding("texture_map01", "TextureMap01");
    addBinding("shadow_toon", "ShadowToonTable");
    addBinding("light_projection", "LightProjMap");
    addBinding("depth_buffer", "DepthBuffer");
    scene.materials.push_back(std::move(material));

    Mesh mesh;
    mesh.sourceIndex = rimVariant ? 14u : 13u;
    mesh.name = rimVariant ? "grass01_mesh" : "grass02_mesh";
    mesh.attributes.push_back({0u, "POSITION", 0u, 3u});
    mesh.attributes.push_back({4u, "TEXCOORD_1", 0u, 2u});
    mesh.attributes.push_back({7u, "COLOR_0", 0u, 4u});
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        CanonicalVertex vertex;
        vertex.position = {static_cast<float>(index), 0.0f, 0.0f};
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.texcoords[0] = {0.2f, 0.3f};
        vertex.texcoords[1] = {0.4f, 0.5f};
        vertex.colors[0] = {0.6f, 0.7f, 0.8f, 0.9f};
        mesh.vertices.push_back(vertex);
    }
    mesh.polygonGroups.push_back({0u, "Triangles", {0u, 1u, 2u}});
    scene.meshes.push_back(std::move(mesh));
    return scene;
}

engine::assets::lgpe::CanonicalScene makeSmallGrassScene(bool shader05) {
    using namespace engine::assets::lgpe;

    CanonicalScene scene;
    scene.profileId = shader05 ? "grass05_fixture" : "grass04_fixture";
    const auto addTexture =
        [&scene](const char* name, unsigned char value, bool srgb) {
            Texture texture;
            texture.name = name;
            texture.sourceContainerRelativePath =
                std::string("field/") + name + ".bntx";
            texture.sourceFormat =
                srgb ? "BC3_UNORM_SRGB" : "R8G8B8A8_UNORM";
            texture.sourceIsSrgb = srgb;
            texture.arrayCount = 1u;
            texture.mipCount = 1u;
            TextureSubresource base;
            base.width = 1u;
            base.height = 1u;
            base.rgba8 = {value, value, value, 255u};
            texture.subresources.push_back(std::move(base));
            scene.textures.push_back(std::move(texture));
        };
    addTexture("texture01", 10u, true);
    addTexture("texture02", 20u, true);
    addTexture("texture03", 30u, true);
    addTexture("alpha01", 40u, true);
    addTexture("green_blend", 50u, true);
    addTexture("shadow_toon", 60u, false);
    addTexture("light_projection", 70u, true);
    addTexture("depth_buffer", 80u, false);

    Material material;
    material.sourceIndex = shader05 ? 15u : 10u;
    material.name = shader05 ? "grass_s04" : "grass_s03";
    material.shaderGroup =
        shader05 ? "FieldGrassShader05" : "FieldGrassShader04";
    material.sourceMetadataJson = shader05
        ? R"({
            "Values":[
                {"Name":"ShadowSampingScale","Value":2.0},
                {"Name":"ShadowBias","Value":0.003},
                {"Name":"OnGameColorVal","Value":1.0},
                {"Name":"OnGameAlpha","Value":1.0},
                {"Name":"LightProjMapTranslateU","Value":0.0},
                {"Name":"LightProjMapTranslateV","Value":0.0},
                {"Name":"LightProjMapScaleU","Value":0.5},
                {"Name":"LightProjMapScaleV","Value":0.5},
                {"Name":"LightProjMapColorPow","Value":1.0},
                {"Name":"DiscardValuie","Value":0.85},
                {"Name":"scroll_U","Value":1.0},
                {"Name":"scroll_V","Value":1.0}
            ],
            "Colors":[
                {"Name":"Shadow_Color","Color":{"R":0.235,"G":0.361,"B":0.391}},
                {"Name":"OnGameColor","Color":{"R":1.0,"G":1.0,"B":1.0}}
            ],
            "Common":{
                "Switches":[
                    {"Name":"DiscardEnable","Value":true},
                    {"Name":"CloudEnable","Value":true},
                    {"Name":"CastShadow","Value":false},
                    {"Name":"ReceiveShadow","Value":true}
                ],
                "Values":[
                    {"Name":"MipMapBias","Value":0},
                    {"Name":"Tex01_UV","Value":1},
                    {"Name":"UV_tex0","Value":0}
                ]
            }
        })"
        : R"({
            "Values":[
                {"Name":"ShadowSampingScale","Value":2.0},
                {"Name":"ShadowBias","Value":0.003},
                {"Name":"Transparent","Value":1.0},
                {"Name":"OnGameColorVal","Value":1.0},
                {"Name":"OnGameAlpha","Value":1.0},
                {"Name":"LightProjMapTranslateU","Value":0.0},
                {"Name":"LightProjMapTranslateV","Value":0.0},
                {"Name":"LightProjMapScaleU","Value":0.5},
                {"Name":"LightProjMapScaleV","Value":0.5},
                {"Name":"LightProjMapColorPow","Value":1.0},
                {"Name":"DiscardValuie","Value":0.470133},
                {"Name":"Tex01_Translate_U","Value":0.0},
                {"Name":"Tex01_Translate_V","Value":0.0},
                {"Name":"Tex01_Rotate","Value":0.0},
                {"Name":"Tex01_Scale_U","Value":1.0},
                {"Name":"Tex01_Scale_V","Value":1.0},
                {"Name":"Tex02_Translate_U","Value":0.0},
                {"Name":"Tex02_Translate_V","Value":0.0},
                {"Name":"Tex02_Rotate","Value":0.0},
                {"Name":"Tex02_Scale_U","Value":1.0},
                {"Name":"Tex02_Scale_V","Value":1.0}
            ],
            "Colors":[
                {"Name":"Shadow_Color","Color":{"R":0.235,"G":0.361,"B":0.391}},
                {"Name":"OnGameColor","Color":{"R":1.0,"G":1.0,"B":1.0}}
            ],
            "Common":{
                "Switches":[
                    {"Name":"DiscardEnable","Value":true},
                    {"Name":"CloudEnable","Value":true},
                    {"Name":"CastShadow","Value":false},
                    {"Name":"ReceiveShadow","Value":true}
                ],
                "Values":[
                    {"Name":"MipMapBias","Value":0},
                    {"Name":"Tex01_UV","Value":0},
                    {"Name":"Tex02_UV","Value":1}
                ]
            }
        })";
    const auto addBinding =
        [&material](const char* textureName, const char* samplerName) {
            TextureBinding binding;
            binding.textureName = textureName;
            binding.samplerName = samplerName;
            binding.wrapS =
                std::string(samplerName) == "ShadowToonTable"
                ? "Clamp"
                : "Repeat";
            binding.wrapT = binding.wrapS;
            material.textureBindings.push_back(std::move(binding));
        };
    addBinding("shadow_toon", "ShadowToonTable");
    addBinding("light_projection", "LightProjMap");
    if (shader05) {
        addBinding("texture02", "TextureMap02");
        addBinding("texture01", "TextureMap01");
        addBinding("green_blend", "green_blend");
        addBinding("texture03", "light_line");
        addBinding("alpha01", "alpha01");
    } else {
        addBinding("texture01", "Texture01");
        addBinding("texture02", "Texture02");
        addBinding("texture03", "Texture03");
    }
    addBinding("depth_buffer", "DepthBuffer");
    scene.materials.push_back(std::move(material));

    Mesh mesh;
    mesh.sourceIndex = shader05 ? 15u : 10u;
    mesh.name = shader05 ? "grass05_mesh" : "grass04_mesh";
    mesh.attributes.push_back({0u, "POSITION", 0u, 3u});
    mesh.attributes.push_back({4u, "TEXCOORD_1", 0u, 2u});
    mesh.attributes.push_back({7u, "COLOR_0", 0u, 4u});
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        CanonicalVertex vertex;
        vertex.position = {static_cast<float>(index), 0.0f, 0.0f};
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.texcoords[0] = {0.2f, 0.3f};
        vertex.texcoords[1] = {0.4f, 0.5f};
        vertex.colors[0] = {0.6f, 0.7f, 0.8f, 0.9f};
        mesh.vertices.push_back(vertex);
    }
    mesh.polygonGroups.push_back({0u, "Triangles", {0u, 1u, 2u}});
    scene.meshes.push_back(std::move(mesh));
    return scene;
}

engine::assets::lgpe::CanonicalScene makeTree05Scene() {
    using namespace engine::assets::lgpe;

    CanonicalScene scene;
    scene.profileId = "tree05_fixture";
    const auto addTexture =
        [&scene](const char* name, unsigned char value, bool srgb) {
            Texture texture;
            texture.name = name;
            texture.sourceContainerRelativePath =
                std::string("field/") + name + ".bntx";
            texture.sourceFormat =
                srgb ? "BC3_UNORM_SRGB" : "R8G8B8A8_UNORM";
            texture.sourceIsSrgb = srgb;
            texture.arrayCount = 1u;
            texture.mipCount = 1u;
            TextureSubresource base;
            base.width = 1u;
            base.height = 1u;
            base.rgba8 = {value, value, value, 255u};
            texture.subresources.push_back(std::move(base));
            scene.textures.push_back(std::move(texture));
        };
    addTexture("texture01", 10u, true);
    addTexture("texture02", 20u, true);
    addTexture("texture03", 30u, true);
    addTexture("shadow_toon", 40u, false);
    addTexture("light_projection", 50u, true);
    addTexture("depth_buffer", 60u, false);

    Material material;
    material.sourceIndex = 4u;
    material.name = "tree001_newsha1";
    material.shaderGroup = "FieldTreeShader05";
    material.sourceMetadataJson = R"({
        "Values":[
            {"Name":"DiscardValuie","Value":0.85},
            {"Name":"RimLight_Min","Value":0.5},
            {"Name":"RimLight_Max","Value":1.0},
            {"Name":"RimLight_Strength","Value":1.0},
            {"Name":"Min","Value":0.0},
            {"Name":"Max","Value":0.15},
            {"Name":"Strangth","Value":0.9}
        ],
        "Colors":[
            {"Name":"Shadow_Color","Color":{"R":0.198068246,"G":0.3005508,"B":0.287446022}},
            {"Name":"rimColor02","Color":{"R":0.118644066,"G":0.115226671,"B":0.0408391841}},
            {"Name":"RimColor","Color":{"R":0.06551,"G":0.530335,"B":0.381026}}
        ],
        "Common":{
            "Switches":[
                {"Name":"DiscardEnable","Value":true},
                {"Name":"DepthWrite","Value":true}
            ],
            "Values":[
                {"Name":"UV_tex01","Value":0},
                {"Name":"UVSet01","Value":1},
                {"Name":"MipMapBias","Value":0}
            ]
        }
    })";
    const auto addBinding =
        [&material](const char* textureName, const char* samplerName) {
            TextureBinding binding;
            binding.textureName = textureName;
            binding.samplerName = samplerName;
            binding.wrapS = "Repeat";
            binding.wrapT = "Repeat";
            material.textureBindings.push_back(std::move(binding));
        };
    addBinding("shadow_toon", "ShadowToonTable");
    addBinding("light_projection", "LightProjMap");
    addBinding("texture01", "Texture01");
    addBinding("texture02", "Texture02");
    addBinding("texture03", "Texture03");
    addBinding("depth_buffer", "DepthBuffer");
    scene.materials.push_back(std::move(material));

    Mesh mesh;
    mesh.sourceIndex = 4u;
    mesh.name = "tree05_mesh";
    mesh.attributes.push_back({0u, "POSITION", 0u, 3u});
    mesh.attributes.push_back({4u, "TEXCOORD_1", 0u, 2u});
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        CanonicalVertex vertex;
        vertex.position = {static_cast<float>(index), 0.0f, 0.0f};
        vertex.texcoords[0] = {0.2f, 0.3f};
        vertex.texcoords[1] = {0.4f, 0.5f};
        mesh.vertices.push_back(vertex);
    }
    mesh.polygonGroups.push_back({0u, "Triangles", {0u, 1u, 2u}});
    scene.meshes.push_back(std::move(mesh));
    return scene;
}

engine::assets::lgpe::CanonicalScene makeTree02Scene() {
    using namespace engine::assets::lgpe;

    CanonicalScene scene;
    scene.profileId = "tree02_fixture";
    const auto addTexture =
        [&scene](const char* name, unsigned char value, bool srgb) {
            Texture texture;
            texture.name = name;
            texture.sourceContainerRelativePath =
                std::string("field/") + name + ".bntx";
            texture.sourceFormat =
                srgb ? "BC3_UNORM_SRGB" : "R8G8B8A8_UNORM";
            texture.sourceIsSrgb = srgb;
            texture.arrayCount = 1u;
            texture.mipCount = 1u;
            TextureSubresource base;
            base.width = 1u;
            base.height = 1u;
            base.rgba8 = {value, value, value, 255u};
            texture.subresources.push_back(std::move(base));
            scene.textures.push_back(std::move(texture));
        };
    addTexture("texture01", 10u, true);
    addTexture("texture02", 20u, true);
    addTexture("shadow_toon", 30u, false);
    addTexture("light_toon", 40u, false);
    addTexture("light_projection", 50u, true);
    addTexture("depth_buffer", 60u, false);

    Material material;
    material.sourceIndex = 6u;
    material.name = "tree004_sha";
    material.shaderGroup = "FieldTreeShader02";
    material.sourceMetadataJson = R"({
        "Values":[
            {"Name":"DiscardValuie","Value":0.6},
            {"Name":"RimLight_Min","Value":0.344764},
            {"Name":"RimLight_Max","Value":0.907407},
            {"Name":"RimLight_Strength","Value":0.246913582},
            {"Name":"ShadowSampingScale","Value":2.0},
            {"Name":"ShadowBias","Value":0.05}
        ],
        "Colors":[
            {"Name":"GreenColor","Color":{"R":0.0217413157,"G":0.112676054,"B":0.0529864542}},
            {"Name":"RimColor","Color":{"R":0.560922,"G":6.898251,"B":1.986181}},
            {"Name":"Shadow_Color","Color":{"R":0.145925313,"G":0.221428573,"B":0.211773708}},
            {"Name":"DirlightColor","Color":{"R":0.08767792,"G":0.366197169,"B":0.08999448}},
            {"Name":"rimColor02","Color":{"R":0.0,"G":0.0,"B":0.0}}
        ],
        "Common":{
            "Switches":[
                {"Name":"DiscardEnable","Value":true},
                {"Name":"RimLight","Value":true},
                {"Name":"LightDir_Hilight","Value":true},
                {"Name":"CloudEnable","Value":true},
                {"Name":"CastShadow","Value":false},
                {"Name":"ReceiveShadow","Value":true}
            ],
            "Values":[
                {"Name":"UV_tex01","Value":0},
                {"Name":"MipMapBias","Value":0}
            ]
        }
    })";
    const auto addBinding =
        [&material](const char* textureName, const char* samplerName) {
            TextureBinding binding;
            binding.textureName = textureName;
            binding.samplerName = samplerName;
            binding.wrapS = "Repeat";
            binding.wrapT = "Repeat";
            material.textureBindings.push_back(std::move(binding));
        };
    addBinding("texture01", "Texture01");
    addBinding("shadow_toon", "ShadowToonTable");
    addBinding("light_toon", "lightToonTable");
    addBinding("texture02", "Texture02");
    addBinding("light_projection", "LightProjMap");
    addBinding("depth_buffer", "DepthBuffer");
    scene.materials.push_back(std::move(material));

    Mesh mesh;
    mesh.sourceIndex = 6u;
    mesh.name = "tree02_mesh";
    mesh.attributes.push_back({0u, "POSITION", 0u, 3u});
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        CanonicalVertex vertex;
        vertex.position = {static_cast<float>(index), 0.0f, 0.0f};
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.texcoords[0] = {0.2f, 0.3f};
        vertex.colors[0] = {0.6f, 0.7f, 0.8f, 0.9f};
        mesh.vertices.push_back(vertex);
    }
    mesh.polygonGroups.push_back({0u, "Triangles", {0u, 1u, 2u}});
    scene.meshes.push_back(std::move(mesh));
    return scene;
}

engine::assets::lgpe::CanonicalScene makeTree04Scene() {
    using namespace engine::assets::lgpe;

    auto scene = makeTree05Scene();
    scene.profileId = "tree04_fixture";
    auto& material = scene.materials[0];
    material.sourceIndex = 8u;
    material.name = "tree006_sha";
    material.shaderGroup = "FieldTreeShader04";
    material.sourceMetadataJson = R"({
        "Values":[
            {"Name":"ShadowSampingScale","Value":2.0},
            {"Name":"ShadowBias","Value":0.02},
            {"Name":"Camera_Light","Value":0.9},
            {"Name":"LightDir_Min","Value":0.0},
            {"Name":"LightDir_Max","Value":0.5},
            {"Name":"DiscardValuie","Value":0.777439},
            {"Name":"RimLight_Max","Value":1.0},
            {"Name":"RimLight_Strength","Value":1.0},
            {"Name":"RimLight_Min","Value":0.475537032}
        ],
        "Colors":[
            {"Name":"Shadow_Color","Color":{"R":0.2541925,"G":0.3857143,"B":0.3688962}},
            {"Name":"lightColor","Color":{"R":0.110647157,"G":0.3070065,"B":0.0411512256}},
            {"Name":"rimColor02","Color":{"R":0.3641765,"G":0.4077916,"B":0.06737146}},
            {"Name":"RimColor","Color":{"R":0.0841252059,"G":0.699449658,"B":0.2343589}}
        ],
        "Common":{
            "Switches":[
                {"Name":"DiscardEnable","Value":true},
                {"Name":"CloudEnable","Value":true},
                {"Name":"CastShadow","Value":false},
                {"Name":"ReceiveShadow","Value":true}
            ],
            "Values":[
                {"Name":"UV_tex01","Value":0},
                {"Name":"UVSet01","Value":1},
                {"Name":"MipMapBias","Value":0}
            ]
        }
    })";
    return scene;
}

engine::assets::lgpe::CanonicalScene makeTreeMikiScene() {
    using namespace engine::assets::lgpe;

    CanonicalScene scene;
    scene.profileId = "tree_miki_fixture";
    const auto addTexture =
        [&scene](const char* name, unsigned char value, bool srgb) {
            Texture texture;
            texture.name = name;
            texture.sourceContainerRelativePath =
                std::string("field/") + name + ".bntx";
            texture.sourceFormat =
                srgb ? "BC3_UNORM_SRGB" : "R8G8B8A8_UNORM";
            texture.sourceIsSrgb = srgb;
            texture.arrayCount = 1u;
            texture.mipCount = 1u;
            TextureSubresource base;
            base.width = 1u;
            base.height = 1u;
            base.rgba8 = {value, value, value, 255u};
            texture.subresources.push_back(std::move(base));
            scene.textures.push_back(std::move(texture));
        };
    addTexture("stem", 10u, true);
    addTexture("shadow_toon", 20u, false);
    addTexture("depth_buffer", 30u, false);

    Material material;
    material.sourceIndex = 2u;
    material.name = "area02_tree2_tree_miki";
    material.shaderGroup = "FieldObjectShader";
    material.sourceMetadataJson = R"({
        "Values":[
            {"Name":"RimLight_Min","Value":0.0},
            {"Name":"RimLight_Max","Value":1.0},
            {"Name":"RimLight_Strength","Value":1.0},
            {"Name":"Tex01_Translate_U","Value":0.0},
            {"Name":"Tex01_Translate_V","Value":0.0},
            {"Name":"Tex01_Rotate","Value":0.0},
            {"Name":"Tex01_Scale_U","Value":1.0},
            {"Name":"Tex01_Scale_V","Value":1.0},
            {"Name":"Transparent","Value":1.0},
            {"Name":"OnGameColorVal","Value":1.0},
            {"Name":"OnGameAlpha","Value":1.0}
        ],
        "Colors":[
            {"Name":"Shadow_Color","Color":{"R":0.234547868,"G":0.3613101,"B":0.391571164}},
            {"Name":"RimColor","Color":{"R":0.455134153,"G":1.00002408,"B":0.833229}},
            {"Name":"OnGameColor","Color":{"R":1.0,"G":1.0,"B":1.0}}
        ],
        "Common":{
            "Switches":[
                {"Name":"DiscardEnable","Value":false},
                {"Name":"CloudEnable","Value":false},
                {"Name":"RimLight","Value":true},
                {"Name":"Highlight","Value":true},
                {"Name":"DepthWrite","Value":true}
            ],
            "Values":[
                {"Name":"Tex01_UV","Value":0},
                {"Name":"MipMapBias","Value":0}
            ]
        }
    })";
    const auto addBinding =
        [&material](const char* textureName, const char* samplerName) {
            TextureBinding binding;
            binding.textureName = textureName;
            binding.samplerName = samplerName;
            binding.wrapS = "Repeat";
            binding.wrapT = "Repeat";
            material.textureBindings.push_back(std::move(binding));
        };
    addBinding("shadow_toon", "ShadowToonTable");
    addBinding("stem", "Texture01");
    addBinding("stem", "HighlightMap");
    addBinding("depth_buffer", "DepthBuffer");
    scene.materials.push_back(std::move(material));

    Mesh mesh;
    mesh.sourceIndex = 10u;
    mesh.name = "tree_miki_mesh";
    mesh.attributes.push_back({0u, "POSITION", 0u, 3u});
    mesh.attributes.push_back({4u, "TEXCOORD_1", 0u, 2u});
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        CanonicalVertex vertex;
        vertex.position = {static_cast<float>(index), 0.0f, 0.0f};
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.texcoords[0] = {0.2f, 0.3f};
        vertex.texcoords[1] = {0.4f, 0.5f};
        vertex.colors[0] = {0.6f, 0.7f, 0.8f, 0.9f};
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
        !near(geometry.vertices[0].sourceUv1U, 0.25f) ||
        !near(geometry.vertices[0].sourceUv1V, 0.5f) ||
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
        grass.textureMipLevelCount != 2u ||
        !grass.textureMipLevels ||
        grass.textureMipLevels[0].width != 2 ||
        grass.textureMipLevels[0].height != 2 ||
        grass.textureMipLevels[1].width != 1 ||
        grass.textureMipLevels[1].height != 1 ||
        grass.textureMipLevels[1].rgba[0] != 91u ||
        grass.sourceTextureBindings[0].mipLevelCount != 2u ||
        grass.sourceTextureBindings[0].mipLevels != grass.textureMipLevels ||
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
        ground.textureMipLevelCount != 1u ||
        ground.normalTextureMipLevelCount != 1u ||
        ground.metallicRoughnessTextureMipLevelCount != 1u ||
        ground.occlusionTextureMipLevelCount != 1u ||
        ground.emissiveTextureMipLevelCount != 1u ||
        ground.environmentTextureMipLevelCount != 1u ||
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

    auto cliffSource = makeCliffScene();
    PreparedScene cliffPrepared;
    if (!prepareCanonicalScene(cliffSource, cliffPrepared, &error)) {
        outFail = "LGPE cliff material fixture failed: " + error;
        return false;
    }
    const auto& cliff = cliffPrepared.registry.materials[0];
    const auto& cliffGeometry = cliffPrepared.registry.geometries[0];
    if (cliffPrepared.stats.fieldCliffSurfaceMaterialCount != 1u ||
        cliffPrepared.stats.materialWithPreviewTextureCount != 0u ||
        cliff.materialMode !=
            engine::render::lgpe_field_cliff::kMaterialMode ||
        cliff.textureRgba[0] != 10u ||
        cliff.normalTextureRgba[0] != 20u ||
        cliff.metallicRoughnessTextureRgba[0] != 30u ||
        cliff.occlusionTextureRgba[0] != 50u ||
        cliff.emissiveTextureRgba[0] != 40u ||
        cliff.textureMipLevelCount != 1u ||
        cliff.normalTextureMipLevelCount != 1u ||
        cliff.metallicRoughnessTextureMipLevelCount != 1u ||
        cliff.occlusionTextureMipLevelCount != 1u ||
        cliff.emissiveTextureMipLevelCount != 1u ||
        cliff.textureSrgb == 0u ||
        cliff.normalTextureSrgb == 0u ||
        cliff.metallicRoughnessTextureSrgb == 0u ||
        cliff.occlusionTextureSrgb == 0u ||
        cliff.emissiveTextureSrgb == 0u ||
        !near(cliff.emissiveFactorR, 0.278898f) ||
        !near(cliff.emissiveFactorG, 0.205076f) ||
        !near(cliff.emissiveFactorB, 0.031895f) ||
        !near(cliff.normalScale, 0.5f) ||
        !near(cliff.metallicFactor, 1.0f) ||
        !near(cliff.roughnessFactor, 0.5f) ||
        !near(cliffGeometry.vertices[0].sourceUv1U, 0.4f) ||
        !near(cliffGeometry.vertices[0].sourceUv1V, 0.5f) ||
        !near(cliffGeometry.vertices[0].sourceUv2U, 0.6f) ||
        !near(cliffGeometry.vertices[0].sourceUv2V, 0.7f)) {
        outFail =
            "FieldCliffShader01 did not bind its five authored surface roles, exact rim constants, UV1, and UV2.";
        return false;
    }

    engine::render::lgpe_field_cliff::SurfaceInputs cliffSurface{};
    cliffSurface.cliffTex01 = {0.1f, 0.1f, 0.1f, 0.5f};
    cliffSurface.groundTex02 = {0.4f, 0.4f, 0.4f, 1.0f};
    cliffSurface.groundTex01 = {0.8f, 0.8f, 0.8f, 1.0f};
    cliffSurface.blendTexRed = 0.25f;
    cliffSurface.borderTex = {0.8f, 0.8f, 0.8f, 0.75f};
    cliffSurface.vertexColor = {0.5f, 0.5f, 0.5f, 1.0f};
    cliffSurface.rimColor = {0.2f, 0.2f, 0.2f};
    cliffSurface.rimLightMin = 0.5f;
    cliffSurface.rimLightMax = 1.0f;
    cliffSurface.rimLightStrength = 0.5f;
    cliffSurface.normalDotView = 0.0f;
    const auto evaluatedCliff =
        engine::render::lgpe_field_cliff::evaluateSurface(cliffSurface);
    if (!near(evaluatedCliff[0], 0.165f) ||
        !near(evaluatedCliff[1], 0.165f) ||
        !near(evaluatedCliff[2], 0.165f) ||
        !near(evaluatedCliff[3], 1.0f)) {
        outFail =
            "The deterministic FieldCliffShader01 surface oracle changed blend or rim order.";
        return false;
    }

    auto grass02Source = makeGrassScene(false);
    PreparedScene grass02Prepared;
    if (!prepareCanonicalScene(grass02Source, grass02Prepared, &error)) {
        outFail = "LGPE FieldGrassShader02 fixture failed: " + error;
        return false;
    }
    const auto& grass02 = grass02Prepared.registry.materials[0];
    const auto& grass02Geometry = grass02Prepared.registry.geometries[0];
    if (grass02Prepared.stats.fieldGrass02SurfaceMaterialCount != 1u ||
        grass02Prepared.stats.fieldGrass01SurfaceMaterialCount != 0u ||
        grass02Prepared.stats.materialWithPreviewTextureCount != 0u ||
        grass02.materialMode !=
            engine::render::lgpe_field_grass::kShader02MaterialMode ||
        grass02.alphaMode != 1u ||
        !near(grass02.alphaCutoff, 0.85f) ||
        grass02.textureRgba[0] != 10u ||
        grass02.normalTextureRgba[0] != 20u ||
        grass02.metallicRoughnessTextureRgba[0] != 30u ||
        grass02.occlusionTextureRgba[0] != 40u ||
        grass02.emissiveTextureRgba[0] != 50u ||
        grass02.environmentTextureRgba[0] != 60u ||
        !near(grass02.normalScale, 0.133802816f) ||
        !near(grass02.metallicFactor, 0.133802816f) ||
        !near(grass02.roughnessFactor, 0.133802816f) ||
        !near(grass02.emissiveFactorR, 0.235f) ||
        !near(grass02.emissiveFactorG, 0.361f) ||
        !near(grass02.emissiveFactorB, 0.391f) ||
        !near(grass02.materialTimeSec, 1.0f) ||
        !near(grass02.materialFlags, 1.0f) ||
        !near(grass02.materialAtlasWidth, 1.0f) ||
        !near(grass02.materialAtlasHeight, 1.0f) ||
        !near(grass02.materialRect0U, 1.0f) ||
        !near(grass02.materialFlipbook0Fps, -2.0f) ||
        !near(grass02Geometry.vertices[0].sourceUv1U, 0.4f) ||
        !near(grass02Geometry.vertices[0].sourceUv1V, 0.5f)) {
        outFail =
            "FieldGrassShader02 did not bind its six local source roles, mip bias, cutout, UV1, and source colors.";
        return false;
    }

    engine::render::lgpe_field_grass::SurfaceInputs grassSurface{};
    grassSurface.textureMap01 = {0.2f, 0.3f, 0.4f, 0.9f};
    grassSurface.textureMap02 = {0.6f, 0.7f, 0.8f, 1.0f};
    grassSurface.greenHikari = {0.1f, 0.2f, 0.3f, 1.0f};
    grassSurface.greenBlend = 0.25f;
    grassSurface.highlight = 0.5f;
    grassSurface.toon = 0.5f;
    grassSurface.projectedShadow = 1.0f;
    grassSurface.projectedCloud = 1.0f;
    grassSurface.color = {0.2f, 0.1f, 0.05f};
    grassSurface.shadowColor = {0.2f, 0.4f, 0.6f};
    grassSurface.onGameColor = {1.2f, 0.8f, 0.5f};
    grassSurface.vertexColor = {0.5f, 0.75f, 1.0f, 0.8f};
    grassSurface.discardThreshold = 0.85f;
    grassSurface.onGameColorValue = 0.5f;
    grassSurface.onGameAlpha = 0.75f;
    const auto evaluatedGrass02 =
        engine::render::lgpe_field_grass::evaluateShader02Surface(
            grassSurface);
    if (evaluatedGrass02.discarded ||
        !near(evaluatedGrass02.color[0], 0.02145f) ||
        !near(evaluatedGrass02.color[1], 0.05315625f) ||
        !near(evaluatedGrass02.color[2], 0.108f) ||
        !near(evaluatedGrass02.color[3], 0.675f)) {
        outFail =
            "The deterministic FieldGrassShader02 oracle changed source texture, decoration, vertex color, toon, or OnGame order.";
        return false;
    }

    auto grass01Source = makeGrassScene(true);
    PreparedScene grass01Prepared;
    if (!prepareCanonicalScene(grass01Source, grass01Prepared, &error)) {
        outFail = "LGPE FieldGrassShader01 fixture failed: " + error;
        return false;
    }
    const auto& grass01 = grass01Prepared.registry.materials[0];
    if (grass01Prepared.stats.fieldGrass01SurfaceMaterialCount != 1u ||
        grass01.materialMode !=
            engine::render::lgpe_field_grass::kShader01MaterialMode ||
        !near(grass01.normalScale, 0.210406289f) ||
        !near(grass01.metallicFactor, 0.295774639f) ||
        !near(grass01.roughnessFactor, 0.0872536451f) ||
        !near(grass01.materialTimeSec, 0.433333337f) ||
        !near(grass01.materialFlags, 1.0f) ||
        !near(grass01.materialAtlasWidth, 1.0f) ||
        !near(grass01.materialAtlasHeight, 0.0f) ||
        !near(grass01.materialRect0U, 0.25f) ||
        !near(grass01.materialRect0V, 0.204700053f) ||
        !near(grass01.materialFlipbook0Fps, 0.0f)) {
        outFail =
            "FieldGrassShader01 did not preserve its exact rim variant constants.";
        return false;
    }
    grassSurface.rimColor = {0.1f, 0.2f, 0.3f};
    grassSurface.rimLightMin = 0.5f;
    grassSurface.rimLightMax = 1.0f;
    grassSurface.rimLightStrength = 0.5f;
    grassSurface.normalDotView = 0.0f;
    const auto evaluatedGrass01 =
        engine::render::lgpe_field_grass::evaluateShader01Surface(
            grassSurface);
    if (evaluatedGrass01.discarded ||
        !near(evaluatedGrass01.color[0], 0.0495f) ||
        !near(evaluatedGrass01.color[1], 0.1290625f) ||
        !near(evaluatedGrass01.color[2], 0.264f) ||
        !near(evaluatedGrass01.color[3], 0.9f)) {
        outFail =
            "The deterministic FieldGrassShader01 oracle changed source rim or lighting order.";
        return false;
    }
    grassSurface.textureMap01[3] = 0.85f;
    if (!engine::render::lgpe_field_grass::evaluateShader01Surface(
             grassSurface)
             .discarded ||
        !engine::render::lgpe_field_grass::evaluateShader02Surface(
             grassSurface)
             .discarded) {
        outFail =
            "FieldGrassShader01/02 no longer discard TextureMap01 alpha at the exact threshold.";
        return false;
    }

    auto grass04Source = makeSmallGrassScene(false);
    PreparedScene grass04Prepared;
    if (!prepareCanonicalScene(grass04Source, grass04Prepared, &error)) {
        outFail = "LGPE FieldGrassShader04 fixture failed: " + error;
        return false;
    }
    const auto& grass04 = grass04Prepared.registry.materials[0];
    if (grass04Prepared.stats.fieldGrass04SurfaceMaterialCount != 1u ||
        grass04Prepared.stats.fieldGrass05SurfaceMaterialCount != 0u ||
        grass04Prepared.stats.materialWithPreviewTextureCount != 0u ||
        grass04.materialMode !=
            engine::render::lgpe_field_small_grass::
                kShader04MaterialMode ||
        grass04.alphaMode != 1u ||
        !near(grass04.alphaCutoff, 0.470133f) ||
        grass04.textureRgba[0] != 30u ||
        grass04.normalTextureRgba[0] != 20u ||
        grass04.metallicRoughnessTextureRgba[0] != 10u ||
        grass04.occlusionTextureRgba[0] != 60u ||
        !near(grass04.normalScale, 0.235f) ||
        !near(grass04.metallicFactor, 0.361f) ||
        !near(grass04.roughnessFactor, 0.391f) ||
        !near(grass04.materialTimeSec, 1.0f) ||
        !near(grass04.materialFlags, 1.0f) ||
        !near(grass04.materialAtlasWidth, 1.0f) ||
        !near(grass04.materialAtlasHeight, 1.0f) ||
        !near(grass04.materialRect0U, 1.0f) ||
        !near(grass04.materialRect0V, 1.0f)) {
        outFail =
            "FieldGrassShader04 did not bind Texture01/02/03, toon, cutout, and exact source colors.";
        return false;
    }

    engine::render::lgpe_field_small_grass::Shader04Inputs grass04Surface{};
    grass04Surface.texture03 = 0.25f;
    grass04Surface.texture02 = {0.2f, 0.4f, 0.6f, 0.8f};
    grass04Surface.texture01 = {0.6f, 0.8f, 1.0f, 0.4f};
    grass04Surface.toon = 0.5f;
    grass04Surface.projectedShadow = 1.0f;
    grass04Surface.projectedCloud = 1.0f;
    grass04Surface.shadowColor = {0.2f, 0.4f, 0.6f};
    grass04Surface.onGameColor = {1.2f, 0.8f, 0.5f};
    grass04Surface.vertexColor = {0.5f, 0.6f, 0.7f, 0.8f};
    grass04Surface.discardThreshold = 0.3f;
    grass04Surface.onGameColorValue = 0.5f;
    grass04Surface.onGameAlpha = 0.75f;
    grass04Surface.transparent = 0.9f;
    const auto evaluatedGrass04 =
        engine::render::lgpe_field_small_grass::evaluateShader04Surface(
            grass04Surface);
    if (evaluatedGrass04.discarded ||
        !near(evaluatedGrass04.color[0], 0.099f) ||
        !near(evaluatedGrass04.color[1], 0.189f) ||
        !near(evaluatedGrass04.color[2], 0.294f) ||
        !near(evaluatedGrass04.color[3], 0.378f)) {
        outFail =
            "The deterministic FieldGrassShader04 oracle changed its source blend, vertex color, lighting, or OnGame order.";
        return false;
    }

    auto grass05Source = makeSmallGrassScene(true);
    PreparedScene grass05Prepared;
    if (!prepareCanonicalScene(grass05Source, grass05Prepared, &error)) {
        outFail = "LGPE FieldGrassShader05 fixture failed: " + error;
        return false;
    }
    const auto& grass05 = grass05Prepared.registry.materials[0];
    if (grass05Prepared.stats.fieldGrass05SurfaceMaterialCount != 1u ||
        grass05Prepared.stats.fieldGrass04SurfaceMaterialCount != 0u ||
        grass05Prepared.stats.materialWithPreviewTextureCount != 0u ||
        grass05.materialMode !=
            engine::render::lgpe_field_small_grass::
                kShader05MaterialMode ||
        grass05.alphaMode != 1u ||
        !near(grass05.alphaCutoff, 0.85f) ||
        grass05.textureRgba[0] != 40u ||
        grass05.normalTextureRgba[0] != 30u ||
        grass05.metallicRoughnessTextureRgba[0] != 10u ||
        grass05.occlusionTextureRgba[0] != 20u ||
        grass05.emissiveTextureRgba[0] != 50u ||
        grass05.environmentTextureRgba[0] != 60u ||
        !near(grass05.normalScale, 0.235f) ||
        !near(grass05.metallicFactor, 0.361f) ||
        !near(grass05.roughnessFactor, 0.391f) ||
        !near(grass05.materialTimeSec, 1.0f) ||
        !near(grass05.materialFlags, 1.0f) ||
        !near(grass05.materialAtlasWidth, 1.0f) ||
        !near(grass05.materialAtlasHeight, 1.0f) ||
        !near(grass05.materialRect0U, 1.0f) ||
        !near(grass05.materialRect0V, 1.0f) ||
        !near(grass05.materialRect0W, 1.0f)) {
        outFail =
            "FieldGrassShader05 did not bind its six local layered-light roles, cutout, scroll, and source colors.";
        return false;
    }

    engine::render::lgpe_field_small_grass::Shader05Inputs grass05Surface{};
    grass05Surface.lightLine = 0.25f;
    grass05Surface.alpha01Primary = {0.2f, 0.3f, 0.4f, 0.8f};
    grass05Surface.alpha01Secondary = {0.6f, 0.7f, 0.8f, 0.4f};
    grass05Surface.greenBlend = 0.25f;
    grass05Surface.textureMap01 = {0.8f, 0.6f, 0.4f};
    grass05Surface.textureMap02 = {0.4f, 0.2f, 0.0f};
    grass05Surface.toon = 0.5f;
    grass05Surface.projectedShadow = 1.0f;
    grass05Surface.projectedCloud = 1.0f;
    grass05Surface.shadowColor = {0.2f, 0.4f, 0.6f};
    grass05Surface.onGameColor = {1.2f, 0.8f, 0.5f};
    grass05Surface.vertexColor = {0.5f, 0.6f, 0.7f, 0.8f};
    grass05Surface.discardThreshold = 0.3f;
    grass05Surface.onGameColorValue = 0.5f;
    grass05Surface.onGameAlpha = 0.75f;
    const auto evaluatedGrass05 =
        engine::render::lgpe_field_small_grass::evaluateShader05Surface(
            grass05Surface);
    if (evaluatedGrass05.discarded ||
        !near(evaluatedGrass05.color[0], 0.264f) ||
        !near(evaluatedGrass05.color[1], 0.2646f) ||
        !near(evaluatedGrass05.color[2], 0.252f) ||
        !near(evaluatedGrass05.color[3], 0.42f)) {
        outFail =
            "The deterministic FieldGrassShader05 oracle changed its two-layer base, light-line, vertex color, or OnGame order.";
        return false;
    }
    grass04Surface.discardThreshold = evaluatedGrass04.color[3];
    grass05Surface.discardThreshold = evaluatedGrass05.color[3];
    if (!engine::render::lgpe_field_small_grass::
             evaluateShader04Surface(grass04Surface)
             .discarded ||
        !engine::render::lgpe_field_small_grass::
             evaluateShader05Surface(grass05Surface)
             .discarded) {
        outFail =
            "FieldGrassShader04/05 no longer discard composite alpha at the exact threshold.";
        return false;
    }

    auto tree02Source = makeTree02Scene();
    PreparedScene tree02Prepared;
    if (!prepareCanonicalScene(tree02Source, tree02Prepared, &error)) {
        outFail = "LGPE FieldTreeShader02 fixture failed: " + error;
        return false;
    }
    const auto& tree02 = tree02Prepared.registry.materials[0];
    if (tree02Prepared.stats.fieldTree02SurfaceMaterialCount != 1u ||
        tree02Prepared.stats.materialWithPreviewTextureCount != 0u ||
        tree02.materialMode !=
            engine::render::lgpe_field_tree02::kMaterialMode ||
        tree02.alphaMode != 1u ||
        !near(tree02.alphaCutoff, 0.6f) ||
        tree02.textureRgba[0] != 10u ||
        tree02.normalTextureRgba[0] != 20u ||
        tree02.occlusionTextureRgba[0] != 30u ||
        tree02.emissiveTextureRgba[0] != 40u ||
        tree02.environmentTextureRgba[0] != 60u ||
        !near(tree02.normalScale, 0.0217413157f) ||
        !near(tree02.metallicFactor, 0.112676054f) ||
        !near(tree02.roughnessFactor, 0.0529864542f) ||
        !near(tree02.emissiveFactorR, 0.145925313f) ||
        !near(tree02.emissiveFactorG, 0.221428573f) ||
        !near(tree02.emissiveFactorB, 0.211773708f) ||
        !near(tree02.materialTimeSec, 0.344764f) ||
        !near(tree02.materialFlags, 0.907407f) ||
        !near(tree02.materialAtlasWidth, 0.246913582f) ||
        !near(tree02.materialAtlasHeight, 0.560922f) ||
        !near(tree02.materialRect0U, 6.898251f) ||
        !near(tree02.materialRect0V, 1.986181f) ||
        !near(tree02.materialRect0W, 0.08767792f) ||
        !near(tree02.materialRect0H, 0.366197169f) ||
        !near(tree02.materialRect1U, 0.08999448f) ||
        !near(tree02.materialRect1V, 0.0f) ||
        !near(tree02.materialRect1W, 0.0f) ||
        !near(tree02.materialRect1H, 0.0f)) {
        outFail =
            "FieldTreeShader02 did not bind its five sampled source roles, cutout, and five exact source colors.";
        return false;
    }

    engine::render::lgpe_field_tree02::SurfaceInputs tree02Surface{};
    tree02Surface.texture01 = {0.1f, 0.2f, 0.3f, 0.7f};
    tree02Surface.texture02 = {0.4f, 0.5f, 0.6f, 1.0f};
    tree02Surface.toon = 0.5f;
    tree02Surface.lightToon = 0.25f;
    tree02Surface.projectedShadow = 0.8f;
    tree02Surface.greenColor = {0.05f, 0.1f, 0.15f};
    tree02Surface.rimColor = {0.1f, 0.2f, 0.3f};
    tree02Surface.shadowColor = {0.2f, 0.4f, 0.6f};
    tree02Surface.directionalLightColor = {0.03f, 0.04f, 0.05f};
    tree02Surface.rimColor02 = {0.06f, 0.07f, 0.08f};
    tree02Surface.vertexColor = {0.5f, 0.75f, 1.0f, 0.25f};
    tree02Surface.discardThreshold = 0.6f;
    tree02Surface.rimLightMin = 0.0f;
    tree02Surface.rimLightMax = 1.0f;
    tree02Surface.rimLightStrength = 1.0f;
    tree02Surface.normalDotView = 0.0f;
    const auto evaluatedTree02 =
        engine::render::lgpe_field_tree02::evaluateSurface(tree02Surface);
    if (evaluatedTree02.discarded ||
        !near(evaluatedTree02.color[0], 0.028795f) ||
        !near(evaluatedTree02.color[1], 0.0846f) ||
        !near(evaluatedTree02.color[2], 0.178125f) ||
        !near(evaluatedTree02.color[3], 0.7f)) {
        outFail =
            "The deterministic FieldTreeShader02 oracle changed source tint, vertex alpha, toon, rim, or directional-light order.";
        return false;
    }
    tree02Surface.texture01[3] = 0.6f;
    if (!engine::render::lgpe_field_tree02::evaluateSurface(tree02Surface)
             .discarded) {
        outFail =
            "FieldTreeShader02 no longer discards Texture01 alpha at its exact threshold.";
        return false;
    }

    auto tree04Source = makeTree04Scene();
    PreparedScene tree04Prepared;
    if (!prepareCanonicalScene(tree04Source, tree04Prepared, &error)) {
        outFail = "LGPE FieldTreeShader04 fixture failed: " + error;
        return false;
    }
    const auto& tree04 = tree04Prepared.registry.materials[0];
    if (tree04Prepared.stats.fieldTree04SurfaceMaterialCount != 1u ||
        tree04Prepared.stats.fieldTree05SurfaceMaterialCount != 0u ||
        tree04.materialMode !=
            engine::render::lgpe_field_tree05::kMaterialMode ||
        !near(tree04.alphaCutoff, 0.777439f) ||
        !near(tree04.normalScale, 0.2541925f) ||
        !near(tree04.metallicFactor, 0.3857143f) ||
        !near(tree04.roughnessFactor, 0.3688962f) ||
        !near(tree04.materialTimeSec, 0.475537032f) ||
        !near(tree04.materialFlags, 1.0f) ||
        !near(tree04.materialAtlasWidth, 1.0f) ||
        !near(tree04.materialAtlasHeight, 0.0841252059f) ||
        !near(tree04.materialRect0U, 0.699449658f) ||
        !near(tree04.materialRect0V, 0.2343589f) ||
        !near(tree04.materialRect0W, 0.3641765f) ||
        !near(tree04.materialRect0H, 0.4077916f) ||
        !near(tree04.materialRect1U, 0.06737146f) ||
        !near(tree04.materialFlipbook1Frames, 0.0f) ||
        !near(tree04.materialFlipbook1Fps, 0.5f) ||
        !near(tree04.materialFlipbook0Fps, 0.9f)) {
        outFail =
            "FieldTreeShader04 did not reuse its byte-identical shader program with the exact explicit source constants.";
        return false;
    }

    auto treeSource = makeTree05Scene();
    PreparedScene treePrepared;
    if (!prepareCanonicalScene(treeSource, treePrepared, &error)) {
        outFail = "LGPE FieldTreeShader05 fixture failed: " + error;
        return false;
    }
    const auto& tree = treePrepared.registry.materials[0];
    const auto& treeGeometry = treePrepared.registry.geometries[0];
    using namespace engine::render::backend;
    if (treePrepared.stats.fieldTree05SurfaceMaterialCount != 1u ||
        treePrepared.stats.materialWithPreviewTextureCount != 0u ||
        tree.materialMode !=
            engine::render::lgpe_field_tree05::kMaterialMode ||
        tree.alphaMode != 1u ||
        !near(tree.alphaCutoff, 0.85f) ||
        (tree.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchDiscardEnable) == 0u ||
        (tree.sourceEnabledSwitchMask &
         WorldSceneSourceMaterialSwitchDepthWrite) == 0u ||
        tree.textureRgba[0] != 10u ||
        tree.normalTextureRgba[0] != 20u ||
        tree.metallicRoughnessTextureRgba[0] != 30u ||
        tree.occlusionTextureRgba[0] != 40u ||
        tree.emissiveTextureRgba[0] != 50u ||
        tree.environmentTextureRgba[0] != 60u ||
        tree.textureSrgb == 0u ||
        tree.normalTextureSrgb == 0u ||
        tree.metallicRoughnessTextureSrgb == 0u ||
        tree.occlusionTextureSrgb != 0u ||
        tree.emissiveTextureSrgb == 0u ||
        tree.environmentTextureSrgb != 0u ||
        !near(tree.normalScale, 0.198068246f) ||
        !near(tree.metallicFactor, 0.3005508f) ||
        !near(tree.roughnessFactor, 0.287446022f) ||
        !near(tree.materialTimeSec, 0.5f) ||
        !near(tree.materialFlags, 1.0f) ||
        !near(tree.materialAtlasWidth, 1.0f) ||
        !near(tree.materialAtlasHeight, 0.06551f) ||
        !near(tree.materialRect0U, 0.530335f) ||
        !near(tree.materialRect0V, 0.381026f) ||
        !near(tree.materialRect0W, 0.118644066f) ||
        !near(tree.materialRect0H, 0.115226671f) ||
        !near(tree.materialRect1U, 0.0408391841f) ||
        !near(tree.materialFlipbook1Frames, 0.0f) ||
        !near(tree.materialFlipbook1Fps, 0.15f) ||
        !near(tree.materialFlipbook0Fps, 0.9f) ||
        !near(treeGeometry.vertices[0].sourceUv1U, 0.4f) ||
        !near(treeGeometry.vertices[0].sourceUv1V, 0.5f)) {
        outFail =
            "FieldTreeShader05 did not bind its six source roles, Common switches, cutout, UV1, and source colors.";
        return false;
    }

    engine::render::lgpe_field_tree05::SurfaceInputs treeSurface{};
    treeSurface.texture01 = {0.1f, 0.2f, 0.3f, 0.9f};
    treeSurface.texture02 = {0.4f, 0.5f, 0.6f, 1.0f};
    treeSurface.texture03 = {0.8f, 0.0f, 0.0f, 1.0f};
    treeSurface.toon = 0.5f;
    treeSurface.shadowColor = {0.2f, 0.4f, 0.6f};
    treeSurface.rimColor = {0.1f, 0.2f, 0.3f};
    treeSurface.rimColor02 = {0.05f, 0.1f, 0.15f};
    treeSurface.sourceLightColor = {0.2f, 0.3f, 0.4f};
    treeSurface.discardThreshold = 0.85f;
    treeSurface.rimLightMin = 0.5f;
    treeSurface.rimLightMax = 1.0f;
    treeSurface.rimLightStrength = 1.0f;
    treeSurface.secondaryMin = 0.0f;
    treeSurface.secondaryMax = 0.15f;
    treeSurface.normalDotView = 0.0f;
    treeSurface.normalDotLight = 1.0f;
    treeSurface.normalDotSecondary = 1.0f;
    const auto evaluatedTree =
        engine::render::lgpe_field_tree05::evaluateSurface(treeSurface);
    if (evaluatedTree.discarded ||
        !near(evaluatedTree.color[0], 0.228f) ||
        !near(evaluatedTree.color[1], 0.476f) ||
        !near(evaluatedTree.color[2], 0.8f) ||
        !near(evaluatedTree.color[3], 0.9f)) {
        outFail =
            "The deterministic FieldTreeShader05 surface oracle changed source texture, toon, rim, or highlight order.";
        return false;
    }
    treeSurface.texture01[3] = 0.85f;
    if (!engine::render::lgpe_field_tree05::evaluateSurface(treeSurface)
             .discarded) {
        outFail =
            "FieldTreeShader05 no longer discards Texture01 alpha at its exact threshold.";
        return false;
    }

    auto trunkSource = makeTreeMikiScene();
    PreparedScene trunkPrepared;
    if (!prepareCanonicalScene(trunkSource, trunkPrepared, &error)) {
        outFail =
            "LGPE tree-miki FieldObjectShader fixture failed: " + error;
        return false;
    }
    const auto& trunk = trunkPrepared.registry.materials[0];
    const auto& trunkGeometry = trunkPrepared.registry.geometries[0];
    if (trunkPrepared.stats.fieldObjectTreeMikiSurfaceMaterialCount != 1u ||
        trunkPrepared.stats.materialWithPreviewTextureCount != 0u ||
        trunk.materialMode !=
            engine::render::lgpe_field_object_tree_miki::kMaterialMode ||
        trunk.alphaMode != 0u ||
        trunk.textureKey.find(":stem:Texture01") == std::string::npos ||
        trunk.normalTextureKey.find(":stem:HighlightMap") ==
            std::string::npos ||
        trunk.occlusionTextureKey.find(":shadow_toon:ShadowToonTable") ==
            std::string::npos ||
        trunk.environmentTextureKey.find(":depth_buffer:DepthBuffer") ==
            std::string::npos ||
        !near(trunk.normalScale, 0.234547868f) ||
        !near(trunk.metallicFactor, 0.3613101f) ||
        !near(trunk.roughnessFactor, 0.391571164f) ||
        !near(trunk.materialTimeSec, 0.0f) ||
        !near(trunk.materialFlags, 1.0f) ||
        !near(trunk.materialAtlasWidth, 1.0f) ||
        !near(trunk.materialAtlasHeight, 0.455134153f) ||
        !near(trunk.materialRect0U, 1.00002408f) ||
        !near(trunk.materialRect0V, 0.833229f) ||
        !near(trunkGeometry.vertices[0].sourceUv1U, 0.4f) ||
        !near(trunkGeometry.vertices[0].sourceUv1V, 0.5f)) {
        outFail =
            "Tree-miki FieldObjectShader did not bind its source texture roles, UV1, vertex color, rim, and toon constants.";
        return false;
    }

    engine::render::lgpe_field_object_tree_miki::SurfaceInputs
        trunkSurface{};
    trunkSurface.texture01 = {0.2f, 0.4f, 0.6f, 0.8f};
    trunkSurface.highlightAlpha = 0.5f;
    trunkSurface.toon = 0.5f;
    trunkSurface.projectedShadow = 0.8f;
    trunkSurface.vertexColor = {0.5f, 0.75f, 1.0f, 0.25f};
    trunkSurface.shadowColor = {0.2f, 0.4f, 0.6f};
    trunkSurface.rimColor = {0.1f, 0.2f, 0.3f};
    trunkSurface.rimLightMin = 0.0f;
    trunkSurface.rimLightMax = 1.0f;
    trunkSurface.rimLightStrength = 1.0f;
    trunkSurface.normalDotView = 0.0f;
    const auto evaluatedTrunk =
        engine::render::lgpe_field_object_tree_miki::evaluateSurface(
            trunkSurface);
    if (!near(evaluatedTrunk[0], 0.065f) ||
        !near(evaluatedTrunk[1], 0.24f) ||
        !near(evaluatedTrunk[2], 0.57f) ||
        !near(evaluatedTrunk[3], 0.2f)) {
        outFail =
            "The deterministic tree-miki surface oracle changed source toon, rim-highlight, vertex-color, or alpha order.";
        return false;
    }
    return true;
}
