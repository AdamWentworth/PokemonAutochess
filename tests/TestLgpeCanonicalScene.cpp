#include "engine/assets/lgpe/LgpeCanonicalScene.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

class MemoryAssetStore final : public engine::IAssetStore {
public:
    bool readText(const std::string& virtualPath,
                  std::string& outText,
                  std::string* outError) const override {
        const auto found = texts.find(virtualPath);
        if (found == texts.end()) {
            if (outError) *outError = "missing text";
            return false;
        }
        outText = found->second;
        return true;
    }

    bool readBytes(const std::string& virtualPath,
                   std::vector<std::uint8_t>& outBytes,
                   std::string* outError) const override {
        const auto found = bytes.find(virtualPath);
        if (found == bytes.end()) {
            if (outError) *outError = "missing bytes";
            return false;
        }
        outBytes = found->second;
        return true;
    }

    bool exists(const std::string& virtualPath) const override {
        return texts.contains(virtualPath) || bytes.contains(virtualPath);
    }

    std::unordered_map<std::string, std::string> texts;
    std::unordered_map<std::string, std::vector<std::uint8_t>> bytes;
};

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t byte = 0u; byte < 4u; ++byte) {
        bytes.push_back(
            static_cast<std::uint8_t>((value >> (byte * 8u)) & 0xffu));
    }
}

void appendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t byte = 0u; byte < 8u; ++byte) {
        bytes.push_back(
            static_cast<std::uint8_t>((value >> (byte * 8u)) & 0xffu));
    }
}

void appendFloat(std::vector<std::uint8_t>& bytes, float value) {
    appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

void appendMagic(std::vector<std::uint8_t>& bytes, const char* magic) {
    for (std::size_t index = 0u; index < 8u; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(magic[index]));
    }
}

void align(std::vector<std::uint8_t>& bytes, std::size_t alignment) {
    while (bytes.size() % alignment != 0u) bytes.push_back(0u);
}

void appendTestVertex(std::vector<std::uint8_t>& bytes,
                      float x,
                      float texcoord1U,
                      float color1R) {
    std::array<float, 47> fields{};
    fields[0] = x;
    fields[4] = 1.0f;
    fields[6] = 1.0f;
    fields[9] = 1.0f;
    fields[16] = texcoord1U;
    fields[22] = 1.0f;
    fields[23] = 1.0f;
    fields[24] = 1.0f;
    fields[25] = 1.0f;
    fields[26] = color1R;
    fields[27] = 1.0f;
    fields[28] = 1.0f;
    fields[29] = 1.0f;
    fields[30] = 1.0f;
    fields[31] = 1.0f;
    fields[32] = 1.0f;
    fields[33] = 1.0f;
    fields[34] = 1.0f;
    fields[35] = 1.0f;
    fields[36] = 1.0f;
    fields[37] = 1.0f;
    fields[38] = 1.0f;
    for (float value : fields) appendFloat(bytes, value);
}

nlohmann::json vector2(float x, float y) {
    return {{"x", x}, {"y", y}};
}

nlohmann::json vector3(float x, float y, float z) {
    return {{"x", x}, {"y", y}, {"z", z}};
}

nlohmann::json vector4(float x, float y, float z, float w) {
    return {{"x", x}, {"y", y}, {"z", z}, {"w", w}};
}

struct Fixture {
    MemoryAssetStore store;
    std::size_t firstIndexOffset = 0u;
};

Fixture makeFixture() {
    using nlohmann::json;

    Fixture fixture;
    std::vector<std::uint8_t> geometry;
    appendMagic(geometry, "LGPEGEOM");
    appendU32(geometry, 1u);
    appendU32(geometry, 188u);
    appendU32(geometry, 1u);
    appendU32(geometry, 0u);
    appendU64(geometry, 3u);
    appendU64(geometry, 3u);

    align(geometry, 16u);
    const std::uint64_t verticesOffset = geometry.size();
    appendTestVertex(geometry, 0.0f, 0.25f, 0.75f);
    appendTestVertex(geometry, 1.0f, 0.5f, 0.5f);
    appendTestVertex(geometry, 2.0f, 0.75f, 0.25f);
    const std::uint64_t verticesSize = 3u * 188u;

    align(geometry, 16u);
    const std::uint64_t rawOffset = geometry.size();
    geometry.insert(geometry.end(), {0x10u, 0x20u, 0x30u, 0x40u});

    align(geometry, 4u);
    const std::uint64_t indicesOffset = geometry.size();
    fixture.firstIndexOffset = geometry.size();
    appendU16(geometry, 0u);
    appendU16(geometry, 1u);
    appendU16(geometry, 2u);

    std::vector<std::uint8_t> textures;
    appendMagic(textures, "LGPETEXS");
    appendU32(textures, 1u);
    appendU32(textures, 1u);
    align(textures, 16u);
    const std::uint64_t textureOffset = textures.size();
    textures.insert(textures.end(), {10u, 20u, 30u, 255u});

    const json identity = {
        {"row0", vector4(1.0f, 0.0f, 0.0f, 0.0f)},
        {"row1", vector4(0.0f, 1.0f, 0.0f, 0.0f)},
        {"row2", vector4(0.0f, 0.0f, 1.0f, 0.0f)},
        {"row3", vector4(0.0f, 0.0f, 0.0f, 1.0f)},
    };
    const json manifestMesh = {
        {"index", 0u},
        {"name", "fixture_mesh"},
        {"transform", identity},
        {"bounds",
         {{"minimum", vector3(0.0f, 0.0f, 0.0f)},
          {"maximum", vector3(2.0f, 0.0f, 0.0f)}}},
        {"attributes",
         {{{"vertex_type", 0u},
           {"semantic_hint", "POSITION"},
           {"buffer_format", 0u},
           {"element_count", 3u}},
          {{"vertex_type", 4u},
           {"semantic_hint", "TEXCOORD_1"},
           {"buffer_format", 0u},
           {"element_count", 2u}}}},
        {"polygon_groups",
         {{{"index", 0u},
           {"material_index", 0u},
           {"primitive_type", "Triangles"},
           {"index_count", 3u},
           {"triangle_record_count", 1u},
           {"indices_sha256", "fixture"}}}},
    };
    const json payloadMesh = {
        {"index", 0u},
        {"name", "fixture_mesh"},
        {"vertex_count", 3u},
        {"decoded_vertex_format", "lgpe_canonical_vertex_v1"},
        {"decoded_vertex_stride_bytes", 188u},
        {"decoded_vertices_offset_bytes", verticesOffset},
        {"decoded_vertices_size_bytes", verticesSize},
        {"source_raw_vertices_offset_bytes", rawOffset},
        {"source_raw_vertices_size_bytes", 4u},
        {"source_raw_vertices_sha256", "fixture"},
        {"polygon_groups",
         {{{"index", 0u},
           {"material_index", 0u},
           {"index_component_type", "uint16_le"},
           {"index_count", 3u},
           {"indices_offset_bytes", indicesOffset},
           {"indices_size_bytes", 6u},
           {"source_indices_sha256", "fixture"}}}},
    };
    const json sourceManifest = {
        {"schema_version", 1u},
        {"profile_id", "fixture"},
        {"ingestion",
         {{"mode", "direct_source"}, {"canonical_bridge", "none"}}},
        {"source", {{"model", {{"sha256", "fixture_source_hash"}}}}},
        {"scene",
         {{"mesh_count", 1u},
          {"material_count", 1u},
          {"bone_count", 1u},
          {"polygon_group_count", 1u},
          {"vertex_count", 3u},
          {"triangle_record_count", 1u},
          {"unique_material_indexed_triangle_count", 1u},
          {"duplicate_material_indexed_triangle_record_count", 0u},
          {"required_texture_count", 1u}}},
        {"meshes", {manifestMesh}},
        {"materials",
         {{{"index", 0u},
           {"name", "fixture_material"},
           {"shader_group", "FieldGrassShader01"},
           {"source_metadata",
            {{"Switches",
              {{{"Name", "SkipMainRendering"}, {"Value", false}}}}}},
           {"texture_bindings",
            {{{"texture_name", "fixture_texture"},
              {"sampler_name", "ColorSampler"},
              {"texture_type", "Diffuse"},
              {"texture_unit", 0},
              {"wrap_s", "Repeat"},
              {"wrap_t", "Clamp"},
              {"wrap_w", "Repeat"},
              {"min_filter", "Linear"},
              {"mag_filter", "Linear"},
              {"scale", vector2(1.0f, 1.0f)},
              {"translate", vector2(0.0f, 0.0f)}}}}}}},
        {"skeleton",
         {{"bones",
           {{{"index", 0u},
             {"name", "root"},
             {"parent_index", -1},
             {"has_skinning", false},
             {"position", vector3(0.0f, 0.0f, 0.0f)},
             {"rotation", vector4(0.0f, 0.0f, 0.0f, 1.0f)},
             {"scale", vector3(1.0f, 1.0f, 1.0f)}}}}}},
        {"validation", {{"passed", true}}},
    };
    const json payloadSubresource = {
        {"array_level", 0u},
        {"mip_level", 0u},
        {"depth_level", 0u},
        {"width", 1u},
        {"height", 1u},
        {"format", "rgba8_unorm"},
        {"offset_bytes", textureOffset},
        {"size_bytes", 4u},
        {"sha256", "fixture"},
    };
    const json payloadTexture = {
        {"name", "fixture_texture"},
        {"source_container_relative_path", "field/fixture.bntx"},
        {"source_format", "R8G8B8A8_UNORM"},
        {"source_is_srgb", true},
        {"width", 1u},
        {"height", 1u},
        {"depth", 1u},
        {"array_count", 1u},
        {"mip_count", 1u},
        {"subresources", json::array({payloadSubresource})},
    };
    json scene;
    scene["schema_version"] = 1u;
    scene["kind"] = "lgpe_canonical_scene_directory";
    scene["stability"] = "provisional_not_a_frozen_runtime_cache";
    scene["profile_id"] = "fixture";
    scene["source_manifest_sha256"] = "fixture";
    scene["source_manifest"] = sourceManifest;
    scene["payloads"]["geometry"] = {
        {"file_name", "geometry.bin"},
        {"size_bytes", geometry.size()},
        {"meshes", json::array({payloadMesh})},
    };
    scene["payloads"]["textures"] = {
        {"file_name", "textures.bin"},
        {"size_bytes", textures.size()},
        {"textures", json::array({payloadTexture})},
    };

    fixture.store.texts.emplace("fixture/scene.json", scene.dump());
    fixture.store.bytes.emplace("fixture/geometry.bin", std::move(geometry));
    fixture.store.bytes.emplace("fixture/textures.bin", std::move(textures));
    return fixture;
}

bool near(float a, float b) {
    return std::fabs(a - b) <= 0.0001f;
}

} // namespace

bool test_lgpe_canonical_scene_contract(std::string& outFail) {
    using engine::assets::lgpe::CanonicalScene;
    using engine::assets::lgpe::loadCanonicalScene;

    Fixture fixture = makeFixture();
    CanonicalScene scene;
    std::string error;
    if (!loadCanonicalScene(fixture.store, "fixture", scene, &error)) {
        outFail = "Canonical LGPE fixture failed to load: " + error;
        return false;
    }
    if (scene.profileId != "fixture" ||
        scene.meshes.size() != 1u ||
        scene.meshes[0].vertices.size() != 3u ||
        scene.meshes[0].polygonGroups.size() != 1u ||
        scene.meshes[0].sourceRawVertexData !=
            std::vector<std::uint8_t>({0x10u, 0x20u, 0x30u, 0x40u}) ||
        !near(scene.meshes[0].vertices[0].texcoords[1][0], 0.25f) ||
        !near(scene.meshes[0].vertices[0].colors[1][0], 0.75f)) {
        outFail =
            "Canonical LGPE loader did not preserve extended source vertex streams.";
        return false;
    }
    if (scene.materials.size() != 1u ||
        scene.materials[0].shaderGroup != "FieldGrassShader01" ||
        scene.materials[0].skipMainRendering ||
        scene.materials[0].textureBindings.size() != 1u ||
        scene.bones.size() != 1u ||
        scene.bones[0].name != "root") {
        outFail =
            "Canonical LGPE loader did not preserve material or skeleton metadata.";
        return false;
    }
    if (scene.textures.size() != 1u ||
        scene.textures[0].subresources.size() != 1u ||
        scene.textures[0].subresources[0].rgba8 !=
            std::vector<std::uint8_t>({10u, 20u, 30u, 255u})) {
        outFail =
            "Canonical LGPE loader did not preserve decoded texture subresources.";
        return false;
    }

    auto& corruptGeometry = fixture.store.bytes.at("fixture/geometry.bin");
    corruptGeometry[fixture.firstIndexOffset] = 3u;
    corruptGeometry[fixture.firstIndexOffset + 1u] = 0u;
    scene.meshes.push_back({});
    error.clear();
    if (loadCanonicalScene(fixture.store, "fixture", scene, &error) ||
        error.find("index") == std::string::npos ||
        !scene.meshes.empty()) {
        outFail =
            "Canonical LGPE loader did not reject an out-of-range source index cleanly.";
        return false;
    }

    return true;
}
