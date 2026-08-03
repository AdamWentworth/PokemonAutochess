#include "../tools/PhlosionNativeModelIr.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

struct PayloadBuilder {
    std::vector<std::uint8_t> bytes;

    template <typename T>
    json append(
        const std::vector<T>& values,
        std::size_t components,
        std::string_view componentType) {
        while (bytes.size() % 16u != 0u) bytes.push_back(0u);
        const std::size_t offset = bytes.size();
        const std::size_t byteLength = values.size() * sizeof(T);
        bytes.resize(offset + byteLength);
        if (byteLength > 0u) {
            std::memcpy(bytes.data() + offset, values.data(), byteLength);
        }
        return {
            {"offset_bytes", offset},
            {"element_count", values.size() / components},
            {"components", components},
            {"component_type", componentType},
            {"byte_length", byteLength},
        };
    }
};

struct TempTree {
    fs::path root;

    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

bool nearlyEqual(float left, float right) {
    return std::abs(left - right) < 1e-6f;
}

} // namespace

bool test_phlosion_native_model_ir_contract(std::string& outFail) {
    TempTree temp{
        fs::temp_directory_path() /
        "pokemon-autochess-phmodel-contract-test"};
    std::error_code error;
    fs::remove_all(temp.root, error);
    error.clear();
    fs::create_directories(temp.root, error);
    if (error) {
        outFail = "could not create native IR test directory";
        return false;
    }

    PayloadBuilder payload;
    const json positions = payload.append<float>(
        {0.0f, 0.0f, 0.0f,
         1.0f, 0.0f, 0.0f,
         0.0f, 1.0f, 0.0f},
        3u,
        "float32");
    const json normals = payload.append<float>(
        {0.0f, 0.0f, 1.0f,
         0.0f, 0.0f, 1.0f,
         0.0f, 0.0f, 1.0f},
        3u,
        "float32");
    const json texcoords = payload.append<float>(
        {0.15f, 0.20f, 0.85f, 0.20f, 0.15f, 0.90f},
        2u,
        "float32");
    const json colors = payload.append<float>(
        {0.25f, 0.50f, 0.75f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f},
        4u,
        "float32");
    const json tangents = payload.append<float>(
        {1.0f, 0.0f, 0.0f, 1.0f,
         1.0f, 0.0f, 0.0f, 1.0f,
         1.0f, 0.0f, 0.0f, 1.0f},
        4u,
        "float32");
    const json joints = payload.append<std::uint16_t>(
        {0u, 0u, 0u, 0u,
         0u, 0u, 0u, 0u,
         0u, 0u, 0u, 0u},
        4u,
        "uint16");
    const json weights = payload.append<float>(
        {1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, 0.0f, 0.0f, 0.0f},
        4u,
        "float32");
    const json indices = payload.append<std::uint32_t>(
        {0u, 1u, 2u},
        1u,
        "uint32");
    const json inverseBind = payload.append<float>(
        {1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, 1.0f, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f, 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f},
        16u,
        "float32");
    const json animationTranslation = payload.append<float>(
        {0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f},
        3u,
        "float32");

    const json material = {
        {"name", "test_material"},
        {"shader_family", "SSS"},
        {"textures", json::array()},
        {"runtime_translation",
         {{"base_color_texture", nullptr},
          {"normal_texture", nullptr},
          {"roughness_texture", nullptr},
          {"metallic_texture", nullptr},
          {"occlusion_texture", nullptr},
          {"emissive_texture", nullptr},
          {"normal_scale", 1.0f},
          {"metallic_factor", 0.0f},
          {"roughness_factor", 1.0f},
          {"occlusion_strength", 1.0f},
          {"alpha_mode", "opaque"},
          {"alpha_cutoff", 0.5f}}},
    };
    json document = {
        {"schema", "phlosion-native-model-ir-v1"},
        {"schema_version", 1},
        {"coordinate_system", {{"texcoords_0", "gamefreak_native"}}},
        {"payload",
         {{"file", "test.bin"},
          {"byte_length", payload.bytes.size()},
          {"byte_order", "little_endian"}}},
        {"model",
         {{"name", "NativeIrTest"},
          {"vertex_count", 3},
          {"index_count", 3},
          {"submesh_count", 1},
          {"submeshes",
           json::array({
               {{"name", "Triangle"},
                {"material", 0},
                {"vertex_count", 3},
                {"index_count", 3},
                {"has_skinning", true},
                {"positions", positions},
                {"normals", normals},
                {"texcoords_0", texcoords},
                {"colors_0", colors},
                {"tangents", tangents},
                {"joints_0", joints},
                {"weights_0", weights},
                {"indices", indices}},
           })}}},
        {"skeleton",
         {{"bones",
           json::array({
               {{"name", "Root"},
                {"parent", -1},
                {"translation", {0.0f, 0.0f, 0.0f}},
                {"rotation", {0.0f, 0.0f, 0.0f, 1.0f}},
                {"scale", {1.0f, 1.0f, 1.0f}},
                {"inverse_bind", inverseBind}},
           })}}},
        {"materials", json::array({material})},
        {"animations",
         json::array({
             {{"name", "idle"},
              {"duration_seconds", 1.0f / 60.0f},
              {"frame_rate", 60},
              {"tracks",
               json::array({
                   {{"bone", 0},
                    {"translation", animationTranslation},
                    {"rotation", nullptr},
                    {"scale", nullptr}},
               })}},
         })},
    };

    const fs::path manifestPath = temp.root / "test.phmodel";
    const fs::path payloadPath = temp.root / "test.bin";
    {
        std::ofstream output(payloadPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(payload.bytes.data()),
            static_cast<std::streamsize>(payload.bytes.size()));
    }
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }

    game::runtime::render_model::MeshData mesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), mesh, &outFail)) {
        return false;
    }
    if (mesh.vertices.size() != 3u || mesh.indices.size() != 3u ||
        mesh.skins.size() != 1u || mesh.animations.size() != 1u ||
        mesh.animations[0].samplers.size() != 1u) {
        outFail = "native IR counts changed during import";
        return false;
    }
    if (!nearlyEqual(mesh.vertices[0].uv.x, 0.15f) ||
        !nearlyEqual(mesh.vertices[0].uv.y, 0.20f)) {
        outFail = "native Game Freak UVs were transformed";
        return false;
    }
    if (!mesh.hasVertexColor || !mesh.hasVertexBaseColor ||
        !nearlyEqual(mesh.vertices[0].color.r, 0.25f)) {
        outFail = "native vertex colors were discarded";
        return false;
    }

    document["coordinate_system"]["texcoords_0"] =
        "source_v_flipped_for_phlosion";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    std::string rejectedError;
    if (tools::phlosion_native_model_ir::load(
            manifestPath.string(), mesh, &rejectedError) ||
        rejectedError.find("Game Freak UV") == std::string::npos) {
        outFail = "legacy flipped-UV native IR was not rejected";
        return false;
    }
    return true;
}
