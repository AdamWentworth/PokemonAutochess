#include "../tools/PhlosionNativeModelIr.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
         100.0f, 0.0f, 0.0f,
         0.0f, 100.0f, 0.0f},
        3u,
        "float32");
    const json normals = payload.append<float>(
        {0.0f, 0.0f, 1.0f,
         0.0f, 0.0f, 1.0f,
         0.0f, 0.0f, 1.0f},
        3u,
        "float32");
    const json texcoords = payload.append<float>(
        {0.15f, 0.20f, 0.85f, 1.20f, 0.15f, 1.90f},
        2u,
        "float32");
    const json colors = payload.append<float>(
        {0.25f, 0.50f, 0.75f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f},
        4u,
        "float32");
    const json tangents = payload.append<float>(
        {1.0f, 2.0f, 3.0f, 1.0f,
         1.0f, 2.0f, 3.0f, 1.0f,
         1.0f, 2.0f, 3.0f, 1.0f},
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
         -50.0f, 0.0f, 0.0f, 1.0f},
        16u,
        "float32");
    const json animationTranslation = payload.append<float>(
        {0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f},
        3u,
        "float32");

    const std::array<std::uint8_t, 120u> whitePng{
        0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au, 0x00u, 0x00u,
        0x00u, 0x0Du, 0x49u, 0x48u, 0x44u, 0x52u, 0x00u, 0x00u, 0x00u, 0x01u,
        0x00u, 0x00u, 0x00u, 0x01u, 0x08u, 0x06u, 0x00u, 0x00u, 0x00u, 0x1Fu,
        0x15u, 0xC4u, 0x89u, 0x00u, 0x00u, 0x00u, 0x01u, 0x73u, 0x52u, 0x47u,
        0x42u, 0x00u, 0xAEu, 0xCEu, 0x1Cu, 0xE9u, 0x00u, 0x00u, 0x00u, 0x04u,
        0x67u, 0x41u, 0x4Du, 0x41u, 0x00u, 0x00u, 0xB1u, 0x8Fu, 0x0Bu, 0xFCu,
        0x61u, 0x05u, 0x00u, 0x00u, 0x00u, 0x09u, 0x70u, 0x48u, 0x59u, 0x73u,
        0x00u, 0x00u, 0x0Eu, 0xC3u, 0x00u, 0x00u, 0x0Eu, 0xC3u, 0x01u, 0xC7u,
        0x6Fu, 0xA8u, 0x64u, 0x00u, 0x00u, 0x00u, 0x0Du, 0x49u, 0x44u, 0x41u,
        0x54u, 0x18u, 0x57u, 0x63u, 0xF8u, 0xFFu, 0xFFu, 0xFFu, 0x7Fu, 0x00u,
        0x09u, 0xFBu, 0x03u, 0xFDu, 0x05u, 0x43u, 0x45u, 0xCAu, 0x00u, 0x00u,
        0x00u, 0x00u, 0x49u, 0x45u, 0x4Eu, 0x44u, 0xAEu, 0x42u, 0x60u, 0x82u};
    const std::array<std::uint8_t, 120u> greenMaskPng{
        0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au, 0x00u, 0x00u,
        0x00u, 0x0Du, 0x49u, 0x48u, 0x44u, 0x52u, 0x00u, 0x00u, 0x00u, 0x01u,
        0x00u, 0x00u, 0x00u, 0x01u, 0x08u, 0x06u, 0x00u, 0x00u, 0x00u, 0x1Fu,
        0x15u, 0xC4u, 0x89u, 0x00u, 0x00u, 0x00u, 0x01u, 0x73u, 0x52u, 0x47u,
        0x42u, 0x00u, 0xAEu, 0xCEu, 0x1Cu, 0xE9u, 0x00u, 0x00u, 0x00u, 0x04u,
        0x67u, 0x41u, 0x4Du, 0x41u, 0x00u, 0x00u, 0xB1u, 0x8Fu, 0x0Bu, 0xFCu,
        0x61u, 0x05u, 0x00u, 0x00u, 0x00u, 0x09u, 0x70u, 0x48u, 0x59u, 0x73u,
        0x00u, 0x00u, 0x0Eu, 0xC3u, 0x00u, 0x00u, 0x0Eu, 0xC3u, 0x01u, 0xC7u,
        0x6Fu, 0xA8u, 0x64u, 0x00u, 0x00u, 0x00u, 0x0Du, 0x49u, 0x44u, 0x41u,
        0x54u, 0x18u, 0x57u, 0x63u, 0x60u, 0xF8u, 0xCFu, 0xC0u, 0x00u, 0x00u,
        0x03u, 0x02u, 0x01u, 0x00u, 0xB6u, 0x5Eu, 0x9Du, 0xD4u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x49u, 0x45u, 0x4Eu, 0x44u, 0xAEu, 0x42u, 0x60u, 0x82u};
    const std::array<std::uint8_t, 23u> grayscaleStripPpm{
        'P', '6', '\n', '4', ' ', '1', '\n', '2', '5', '5', '\n',
        0u, 0u, 0u,
        40u, 40u, 40u,
        160u, 160u, 160u,
        240u, 240u, 240u};
    const std::array<std::uint8_t, 23u> blackStripPpm{
        'P', '6', '\n', '4', ' ', '1', '\n', '2', '5', '5', '\n',
        0u, 0u, 0u,
        0u, 0u, 0u,
        0u, 0u, 0u,
        0u, 0u, 0u};
    const std::vector<std::uint8_t> redPpm = [] {
        std::vector<std::uint8_t> bytes{
            'P', '6', '\n', '3', '2', ' ', '3', '2', '\n',
            '2', '5', '5', '\n'};
        for (std::size_t pixel = 0u; pixel < 32u * 32u; ++pixel) {
            bytes.push_back(224u);
            bytes.push_back(32u);
            bytes.push_back(16u);
        }
        return bytes;
    }();
    const std::array<std::uint8_t, 14u> blueMaskPpm{
        'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n',
        0u, 0u, 255u};
    const std::array<std::uint8_t, 14u> accessoryNormalPpm{
        'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n',
        64u, 192u, 255u};
    const std::vector<std::uint8_t> flatNormalPpm = [] {
        std::vector<std::uint8_t> bytes{
            'P', '6', '\n', '4', ' ', '4', '\n', '2', '5', '5', '\n'};
        for (std::size_t pixel = 0u; pixel < 16u; ++pixel) {
            bytes.push_back(128u);
            bytes.push_back(128u);
            bytes.push_back(255u);
        }
        return bytes;
    }();
    const std::array<std::uint8_t, 26u> lgpeLayerMaskTga{
        0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 2u, 0u, 1u, 0u, 32u, 0x28u,
        0u, 255u, 0u, 0u,
        255u, 0u, 0u, 255u};
    const std::array<std::uint8_t, 34u> lgpeIrisTga{
        0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 4u, 0u, 1u, 0u, 32u, 0x28u,
        0u, 0u, 255u, 255u,
        255u, 0u, 0u, 255u,
        255u, 0u, 0u, 255u,
        0u, 0u, 255u, 255u};

    const json material = {
        {"name", "r_eye"},
        {"shader_family", "EyeClearCoat"},
        {"shader_options", {{"EnableHighlight", "True"}}},
        {"float_parameters",
         {{"RoughnessHighlight", 0.51f},
          {"EmissionIntensityLayer5", 0.8f},
          {"EmissionIntensityLayer2", 0.2f},
          {"MetallicLayer2", 1.0f},
          {"RoughnessLayer2", 0.8f}}},
        {"vec4_parameters",
         {{"BaseColorLayer2", {0.8f, 0.1f, 0.05f, 1.0f}},
          {"EmissionColorLayer2", {0.0f, 0.5f, 0.0f, 1.0f}},
          {"EmissionColorLayer5", {1.0f, 1.0f, 1.0f, 1.0f}},
          {"BaseColorClearCoat", {0.0f, 0.0f, 0.0f, 0.0f}}}},
        {"textures",
         json::array({
             {{"role", "BaseColorMap"},
              {"file", "white.png"},
              {"wrap_s", 33071},
              {"wrap_t", 33071},
              {"min_filter", 9729},
              {"mag_filter", 9729}},
             {{"role", "LayerMaskMap"},
              {"file", "mask.png"},
              {"wrap_s", 33071},
              {"wrap_t", 33071},
              {"min_filter", 9729},
              {"mag_filter", 9729}},
             {{"role", "NormalMap"},
              {"file", "flat-normal.ppm"},
              {"wrap_s", 33071},
              {"wrap_t", 33071},
              {"min_filter", 9729},
              {"mag_filter", 9729}},
             {{"role", "NormalMap1"},
              {"file", "flat-normal.ppm"},
              {"wrap_s", 33648},
              {"wrap_t", 33648},
              {"min_filter", 9729},
              {"mag_filter", 9729}},
         })},
        {"runtime_translation",
         {{"base_color_texture", "white.png"},
          {"normal_texture", "flat-normal.ppm"},
          {"roughness_texture", nullptr},
          {"metallic_texture", nullptr},
          {"occlusion_texture", nullptr},
          {"emissive_texture", nullptr},
          {"normal_scale", 1.0f},
          {"metallic_factor", 0.0f},
          {"roughness_factor", 0.1f},
          {"occlusion_strength", 1.0f},
          {"alpha_mode", "opaque"},
          {"alpha_cutoff", 0.5f}}},
    };
    json document = {
        {"schema", "phlosion-native-model-ir-v1"},
        {"schema_version", 1},
        {"coordinate_system",
         {{"texcoords_0", "gamefreak_native"},
          {"unit_scale_to_meters", 0.01f}}},
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
                {"translation", {50.0f, 0.0f, 0.0f}},
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
               })},
              {"mesh_visibility",
               json::array({
                   {{"mesh", "Triangle"},
                    {"key_frames", {0, 1, 2}},
                    {"values", {false, true, false}}},
               })}},
         })},
    };

    const fs::path manifestPath = temp.root / "test.phmodel";
    const fs::path payloadPath = temp.root / "test.bin";
    const fs::path whitePath = temp.root / "white.png";
    const fs::path maskPath = temp.root / "mask.png";
    const fs::path stripPath = temp.root / "strip.ppm";
    const fs::path blackStripPath = temp.root / "black-strip.ppm";
    const fs::path redPath = temp.root / "red.ppm";
    const fs::path blueMaskPath = temp.root / "blue-mask.ppm";
    const fs::path accessoryNormalPath =
        temp.root / "accessory-normal.ppm";
    const fs::path flatNormalPath = temp.root / "flat-normal.ppm";
    const fs::path lgpeLayerMaskPath = temp.root / "lgpe-layer-mask.tga";
    const fs::path lgpeIrisPath = temp.root / "lgpe-iris.tga";
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
    {
        std::ofstream output(whitePath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(whitePng.data()),
            static_cast<std::streamsize>(whitePng.size()));
    }
    {
        std::ofstream output(maskPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(greenMaskPng.data()),
            static_cast<std::streamsize>(greenMaskPng.size()));
    }
    {
        std::ofstream output(stripPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(grayscaleStripPpm.data()),
            static_cast<std::streamsize>(grayscaleStripPpm.size()));
    }
    {
        std::ofstream output(blackStripPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(blackStripPpm.data()),
            static_cast<std::streamsize>(blackStripPpm.size()));
    }
    {
        std::ofstream output(redPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(redPpm.data()),
            static_cast<std::streamsize>(redPpm.size()));
    }
    {
        std::ofstream output(blueMaskPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(blueMaskPpm.data()),
            static_cast<std::streamsize>(blueMaskPpm.size()));
    }
    {
        std::ofstream output(accessoryNormalPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(accessoryNormalPpm.data()),
            static_cast<std::streamsize>(accessoryNormalPpm.size()));
    }
    {
        std::ofstream output(flatNormalPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(flatNormalPpm.data()),
            static_cast<std::streamsize>(flatNormalPpm.size()));
    }
    {
        std::ofstream output(lgpeLayerMaskPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(lgpeLayerMaskTga.data()),
            static_cast<std::streamsize>(lgpeLayerMaskTga.size()));
    }
    {
        std::ofstream output(lgpeIrisPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(lgpeIrisTga.data()),
            static_cast<std::streamsize>(lgpeIrisTga.size()));
    }

    game::runtime::render_model::MeshData mesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), mesh, &outFail)) {
        return false;
    }
    if (mesh.vertices.size() != 3u || mesh.indices.size() != 3u ||
        mesh.skins.size() != 1u || mesh.animations.size() != 1u ||
        mesh.animations[0].samplers.size() != 1u ||
        mesh.animationMeshVisibility.size() != 1u ||
        mesh.animationMeshVisibility[0].size() != 1u) {
        outFail = "native IR counts changed during import";
        return false;
    }
    if (!nearlyEqual(mesh.vertices[1].position.x, 1.0f) ||
        !nearlyEqual(mesh.boundsMax.x, 1.0f) ||
        !nearlyEqual(mesh.nodesDefault[1].t.x, 0.5f) ||
        !nearlyEqual(mesh.skins[0].inverseBind[0][3].x, -0.5f) ||
        !nearlyEqual(mesh.animations[0].samplers[0].outputs[1].y, 0.1f)) {
        outFail =
            "native IR source units were not converted consistently to meters";
        return false;
    }
    const auto& visibility =
        mesh.animationMeshVisibility[0][0];
    if (visibility.nodeIndex != 2 ||
        visibility.inputs.size() != 3u ||
        visibility.values !=
            std::vector<std::uint8_t>({0u, 1u, 0u}) ||
        !nearlyEqual(visibility.inputs[1], 1.0f / 60.0f)) {
        outFail = "native mesh visibility animation was not preserved";
        return false;
    }
    if (!nearlyEqual(mesh.vertices[0].uv.x, 0.15f) ||
        !nearlyEqual(mesh.vertices[0].uv.y, 0.80f) ||
        !nearlyEqual(mesh.vertices[1].uv.y, 0.80f) ||
        !nearlyEqual(mesh.vertices[2].uv.y, 0.10f)) {
        outFail =
            "native tiled Game Freak UVs were not flipped within each tile";
        return false;
    }
    if (mesh.hasVertexColor || mesh.hasVertexBaseColor ||
        !nearlyEqual(mesh.vertices[0].color.r, 0.25f)) {
        outFail =
            "native vertex colors were not preserved as non-albedo evidence";
        return false;
    }
    if (mesh.submeshBaseTextures.size() != 1u ||
        !mesh.submeshBaseTextures[0].hasPixels() ||
        mesh.submeshBaseTextures[0].rgba[0] < 220u ||
        mesh.submeshBaseTextures[0].rgba[1] < 80u ||
        mesh.submeshBaseTextures[0].rgba[1] > 100u ||
        mesh.submeshBaseTextures[0].rgba[2] < 55u ||
        mesh.submeshBaseTextures[0].rgba[2] > 75u ||
        mesh.submeshBaseTextures[0].width != 4 ||
        mesh.submeshBaseTextures[0].height != 4 ||
        mesh.submeshBaseTextures[0].rgba[24] < 245u ||
        mesh.submeshBaseTextures[0].rgba[25] < 245u ||
        mesh.submeshBaseTextures[0].rgba[26] < 245u ||
        mesh.submeshMaterialModes.size() != 1u ||
        mesh.submeshMaterialModes[0] != 2u ||
        mesh.submeshNormalTextures.size() != 1u ||
        !mesh.submeshNormalTextures[0].hasPixels() ||
        mesh.submeshNormalTextures[0].rgba[0] != 128u ||
        mesh.submeshNormalTextures[0].rgba[1] != 128u ||
        mesh.submeshNormalTextures[0].rgba[2] != 255u ||
        mesh.submeshMetallicRoughnessTextures.size() != 1u ||
        mesh.submeshMetallicRoughnessTextures[0].hasPixels() ||
        !nearlyEqual(mesh.submeshMetallicFactor[0], 0.0f) ||
        !nearlyEqual(mesh.submeshRoughnessFactor[0], 0.5f) ||
        mesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(mesh.submeshMaterialParams0[0].x, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams0[0].y, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams0[0].z, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams0[0].w, 0.0f) ||
        mesh.submeshMaterialParams1.size() != 1u ||
        !nearlyEqual(mesh.submeshMaterialParams1[0].x, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams1[0].y, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams1[0].z, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams1[0].w, 0.0f)) {
        outFail =
            std::string(
                "Scarlet EyeClearCoat did not resolve to the GLB-compatible EyeFinal material") +
            " base=" +
            (mesh.submeshBaseTextures.empty()
                 ? std::string("missing")
                 : std::to_string(mesh.submeshBaseTextures[0].width) + "x" +
                       std::to_string(mesh.submeshBaseTextures[0].height) + " " +
                       (mesh.submeshBaseTextures[0].rgba.size() > 26u
                            ? std::to_string(mesh.submeshBaseTextures[0].rgba[0]) + "," +
                                  std::to_string(mesh.submeshBaseTextures[0].rgba[1]) + "," +
                                  std::to_string(mesh.submeshBaseTextures[0].rgba[2]) + "/" +
                                  std::to_string(mesh.submeshBaseTextures[0].rgba[24]) + "," +
                                  std::to_string(mesh.submeshBaseTextures[0].rgba[25]) + "," +
                                  std::to_string(mesh.submeshBaseTextures[0].rgba[26])
                            : std::string("short"))) +
            " mode=" +
            (mesh.submeshMaterialModes.empty()
                 ? std::string("missing")
                 : std::to_string(mesh.submeshMaterialModes[0])) +
            " mr=" +
            (mesh.submeshMetallicRoughnessTextures.empty()
                 ? std::string("missing")
                 : mesh.submeshMetallicRoughnessTextures[0].hasPixels()
                       ? std::string("pixels")
                       : std::string("empty")) +
            " factors=" +
            (mesh.submeshMetallicFactor.empty()
                 ? std::string("missing")
                 : std::to_string(mesh.submeshMetallicFactor[0]) + "," +
                       std::to_string(mesh.submeshRoughnessFactor[0]));
        return false;
    }
    const float inverseTangentLength =
        1.0f / std::sqrt(14.0f);
    if (!nearlyEqual(
            mesh.vertices[0].tangent.x,
            inverseTangentLength) ||
        !nearlyEqual(
            mesh.vertices[0].tangent.y,
            3.0f * inverseTangentLength) ||
        !nearlyEqual(
            mesh.vertices[0].tangent.z,
            -2.0f * inverseTangentLength) ||
        !nearlyEqual(mesh.vertices[0].tangent.w, 1.0f)) {
        outFail =
            "native Game Freak tangents were not converted to the runtime/glTF basis";
        return false;
    }
    if (mesh.submeshEmissiveTextures.size() != 1u ||
        mesh.submeshEmissiveTextures[0].hasPixels() ||
        mesh.submeshEmissiveFactors.size() != 1u ||
        mesh.submeshEmissiveFactors[0].x > 0.01f) {
        outFail = "Scarlet EyeFinal retained a second live emissive highlight";
        return false;
    }

    // Scarlet's pointlight marker must place the stable GLB-style disk from
    // the current eye mesh projection. Pichu and Raichu do not share
    // Pikachu's visible eye UV, so falling back to (0.64, 0.36) puts their
    // catchlight on an occluded part of the eye surface.
    document["materials"][0]["shader_options"]["PointLightIndex"] = "1";
    document["skeleton"]["bones"][0]["name"] = "pointlight1";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData projectedEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), projectedEyeMesh, &outFail)) {
        return false;
    }
    if (projectedEyeMesh.submeshBaseTextures.size() != 1u ||
        projectedEyeMesh.submeshBaseTextures[0].rgba.size() <= 42u ||
        projectedEyeMesh.submeshBaseTextures[0].rgba[40] < 210u ||
        projectedEyeMesh.submeshBaseTextures[0].rgba[24] > 235u) {
        outFail =
            "Scarlet eye catchlight did not follow its authored pointlight/mesh projection";
        return false;
    }
    document["materials"][0]["shader_options"].erase(
        "PointLightIndex");
    document["skeleton"]["bones"][0]["name"] = "Root";

    // Scarlet uses EyeClearCoat for glossy body accessories as well as eyes.
    // Golduck's known-good GLB resolves body_c to plain PBR: untouched red
    // BaseColorMap, packed-XY NormalMap, 0.45 roughness, and no layer emission,
    // synthetic catchlight, or live clear coat. Keep NormalMap1 out of the
    // portable surface-normal slot.
    document["materials"][0]["name"] = "body_c";
    document["materials"][0]["shader_options"]["EnableEyeClearCoat"] =
        "True";
    document["materials"][0]["vec4_parameters"]["BaseColorLayer3"] =
        {1.0f, 1.0f, 1.0f, 1.0f};
    document["materials"][0]["vec4_parameters"]["EmissionColorLayer3"] =
        {1.0f, 0.2f, 0.1f, 1.0f};
    document["materials"][0]["float_parameters"]
            ["EmissionIntensityLayer3"] = 0.2f;
    document["materials"][0]["float_parameters"]
            ["RoughnessLayer3"] = 0.15f;
    document["materials"][0]["float_parameters"]
            ["EmissionIntensityLayer5"] = 10.0f;
    document["materials"][0]["textures"][0]["file"] = "red.ppm";
    document["materials"][0]["textures"][1]["file"] =
        "blue-mask.ppm";
    document["materials"][0]["textures"][3]["file"] =
        "accessory-normal.ppm";
    document["materials"][0]["runtime_translation"]
            ["base_color_texture"] = "red.ppm";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData clearCoatAccessoryMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), clearCoatAccessoryMesh, &outFail)) {
        return false;
    }
    bool accessoryColorPreserved =
        clearCoatAccessoryMesh.submeshBaseTextures.size() == 1u &&
        clearCoatAccessoryMesh.submeshBaseTextures[0].hasPixels();
    if (accessoryColorPreserved) {
        const auto& pixels =
            clearCoatAccessoryMesh.submeshBaseTextures[0].rgba;
        for (std::size_t offset = 0u;
             offset + 2u < pixels.size();
             offset += 4u) {
            if (pixels[offset] != 224u ||
                pixels[offset + 1u] != 32u ||
                pixels[offset + 2u] != 16u) {
                accessoryColorPreserved = false;
                break;
            }
        }
    }
    if (!accessoryColorPreserved ||
        clearCoatAccessoryMesh.submeshMaterialModes.size() != 1u ||
        clearCoatAccessoryMesh.submeshMaterialModes[0] != 2u ||
        clearCoatAccessoryMesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMaterialParams0[0].x, 0.0f) ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMaterialParams0[0].y, 0.0f) ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMaterialParams0[0].z, 0.0f) ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMaterialParams0[0].w, 0.0f) ||
        clearCoatAccessoryMesh.submeshMaterialParams1.size() != 1u ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMaterialParams1[0].x, 0.0f) ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMaterialParams1[0].y, 0.0f) ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMaterialParams1[0].z, 0.0f) ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMaterialParams1[0].w, 0.0f) ||
        clearCoatAccessoryMesh.submeshNormalTextures.size() != 1u ||
        !clearCoatAccessoryMesh.submeshNormalTextures[0].hasPixels() ||
        clearCoatAccessoryMesh.submeshNormalTextures[0].rgba[0] != 128u ||
        clearCoatAccessoryMesh.submeshNormalTextures[0].rgba[1] != 128u ||
        clearCoatAccessoryMesh.submeshNormalTextures[0].rgba[2] != 0u ||
        clearCoatAccessoryMesh.submeshMetallicRoughnessTextures.size() != 1u ||
        clearCoatAccessoryMesh.submeshMetallicRoughnessTextures[0].hasPixels() ||
        clearCoatAccessoryMesh.submeshMetallicFactor.size() != 1u ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshMetallicFactor[0], 0.0f) ||
        clearCoatAccessoryMesh.submeshRoughnessFactor.size() != 1u ||
        !nearlyEqual(
            clearCoatAccessoryMesh.submeshRoughnessFactor[0],
            0.45f) ||
        clearCoatAccessoryMesh.submeshEmissiveTextures.size() != 1u ||
        clearCoatAccessoryMesh.submeshEmissiveTextures[0].hasPixels() ||
        clearCoatAccessoryMesh.submeshEmissiveFactors.size() != 1u ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshEmissiveFactors[0].x, 0.0f)) {
        outFail =
            std::string(
                "EyeClearCoat accessory did not match the reference GLB PBR material") +
            " color=" +
            (clearCoatAccessoryMesh.submeshBaseTextures.empty() ||
                     clearCoatAccessoryMesh.submeshBaseTextures[0].rgba.size() < 3u
                 ? std::string("missing")
                 : std::to_string(
                       clearCoatAccessoryMesh.submeshBaseTextures[0].rgba[0]) +
                       "," +
                       std::to_string(
                           clearCoatAccessoryMesh.submeshBaseTextures[0].rgba[1]) +
                       "," +
                       std::to_string(
                       clearCoatAccessoryMesh.submeshBaseTextures[0].rgba[2])) +
            " mode=" +
            (clearCoatAccessoryMesh.submeshMaterialModes.empty()
                 ? std::string("missing")
                 : std::to_string(
                       clearCoatAccessoryMesh.submeshMaterialModes[0]));
        return false;
    }
    document["materials"][0]["name"] = "test_material";
    document["materials"][0]["shader_options"].erase(
        "EnableEyeClearCoat");
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorLayer3");
    document["materials"][0]["vec4_parameters"].erase(
        "EmissionColorLayer3");
    document["materials"][0]["float_parameters"].erase(
        "EmissionIntensityLayer3");
    document["materials"][0]["textures"][0]["file"] = "white.png";
    document["materials"][0]["textures"][1]["file"] = "mask.png";
    document["materials"][0]["textures"][3]["file"] =
        "flat-normal.ppm";
    document["materials"][0]["runtime_translation"]
            ["base_color_texture"] = "white.png";

    // LayerMaskScale disables the corresponding source layer for every
    // layered property, not only albedo. Scarlet Pikachu sets layer 1 to zero
    // on its face patch; ignoring that scale flattened the patch roughness and
    // exposed its rectangular material boundary against the surrounding fur.
    document["materials"][0]["shader_family"] = "Standard";
    document["materials"][0]["float_parameters"]["LayerMaskScale2"] =
        0.0f;
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData scaledLayerMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), scaledLayerMesh, &outFail)) {
        return false;
    }
    if (scaledLayerMesh.submeshMetallicRoughnessTextures.size() != 1u ||
        !scaledLayerMesh.submeshMetallicRoughnessTextures[0].hasPixels() ||
        scaledLayerMesh.submeshMetallicRoughnessTextures[0].rgba[1] < 20u ||
        scaledLayerMesh.submeshMetallicRoughnessTextures[0].rgba[1] > 35u) {
        outFail =
            "LayerMaskScale did not disable the matching layered roughness selector";
        return false;
    }
    document["materials"][0]["float_parameters"].erase(
        "LayerMaskScale2");
    document["materials"][0]["shader_family"] = "EyeClearCoat";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }

    const fs::path cookedRoot = temp.root / "cooked";
    game::runtime::phlosion::ModelCookStats firstCook;
    if (!game::runtime::phlosion::cookModelObject(
            manifestPath.string(),
            mesh,
            cookedRoot.string(),
            "Character",
            firstCook,
            &outFail)) {
        return false;
    }
    const fs::path cookedObject =
        game::runtime::phlosion::objectPathForModel(
            manifestPath.string(), cookedRoot.string());
    const fs::path staleTexture =
        cookedObject.parent_path() / "textures" / "stale.ktx2";
    {
        std::ofstream output(staleTexture, std::ios::binary);
        output << "obsolete";
    }
    game::runtime::phlosion::ModelCookStats secondCook;
    if (!game::runtime::phlosion::cookModelObject(
            manifestPath.string(),
            mesh,
            cookedRoot.string(),
            "Character",
            secondCook,
            &outFail)) {
        return false;
    }
    if (fs::exists(staleTexture)) {
        outFail = "recooking a PHLO object retained an obsolete generated texture";
        return false;
    }

    // PLA uses an authored HighlightMaskMap rather than Scarlet's baked
    // EyeFinal disk. Keep that older path and its layered emission intact.
    document["materials"][0]["shader_family"] = "Eye";
    document["materials"][0]["vec4_parameters"]["EmissionColorLayer5"] =
        {0.87135625f, 0.0f, 0.0f, 1.0f};
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorClearCoat");
    document["materials"][0]["textures"].erase(
        document["materials"][0]["textures"].end() - 1);
    document["materials"][0]["textures"].push_back(
        {{"role", "HighlightMaskMap"},
         {"file", "white.png"},
         {"wrap_s", 33648},
         {"wrap_t", 33648},
         {"min_filter", 9729},
         {"mag_filter", 9729}});

    // The synthetic green mask is opaque in alpha. A fourth-layer pupil must
    // replace the iris emission rather than add to it.
    document["materials"][0]["vec4_parameters"]["EmissionColorLayer4"] =
        {0.0129f, 0.0129f, 0.0129f, 1.0f};
    document["materials"][0]["float_parameters"]["EmissionIntensityLayer4"] =
        0.2f;
    document["materials"][0]["textures"][1]["file"] = "white.png";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData overlappingPupilMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), overlappingPupilMesh, &outFail)) {
        return false;
    }
    if (overlappingPupilMesh.submeshEmissiveTextures.size() != 1u ||
        !overlappingPupilMesh.submeshEmissiveTextures[0].hasPixels() ||
        overlappingPupilMesh.submeshEmissiveTextures[0].rgba[1] >= 40u) {
        outFail =
            "Overlapping native eye emission did not preserve the black pupil layer";
        return false;
    }
    document["materials"][0]["vec4_parameters"].erase(
        "EmissionColorLayer4");
    document["materials"][0]["float_parameters"].erase(
        "EmissionIntensityLayer4");
    document["materials"][0]["textures"][1]["file"] = "mask.png";

    // The final PLA load must still use the dedicated authored-highlight path
    // without inventing Scarlet's clear-coat coverage.
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData plaEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), plaEyeMesh, &outFail)) {
        return false;
    }
    if (plaEyeMesh.submeshMaterialModes.size() != 1u ||
        plaEyeMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::kNativeEyeClearCoatMaterialMode ||
        plaEyeMesh.submeshMaterialParams1.size() != 1u ||
        !nearlyEqual(plaEyeMesh.submeshMaterialParams1[0].w, -1.0f) ||
        plaEyeMesh.submeshEmissiveTextures.size() != 1u ||
        !plaEyeMesh.submeshEmissiveTextures[0].hasPixels() ||
        plaEyeMesh.submeshEmissiveTextures[0].rgba[0] < 210u ||
        plaEyeMesh.submeshEmissiveTextures[0].rgba[1] < 80u ||
        plaEyeMesh.submeshEmissiveFactors.size() != 1u ||
        plaEyeMesh.submeshEmissiveFactors[0].x < 0.99f) {
        const std::vector<unsigned char>* emissive =
            !plaEyeMesh.submeshEmissiveTextures.empty()
                ? &plaEyeMesh.submeshEmissiveTextures[0].rgba
                : nullptr;
        outFail =
            "PLA Eye highlight/layer emission or no-clear-coat contract was not preserved" +
            (emissive != nullptr && emissive->size() >= 3u
                 ? " rgba=" + std::to_string((*emissive)[0]) + "," +
                       std::to_string((*emissive)[1]) + "," +
                       std::to_string((*emissive)[2])
                 : std::string{});
        return false;
    }

    // Paras builds its pupil and iris from two nested meshes sharing the same
    // opaque PLA Eye material. Flatten the source program's optical depth
    // composition by advancing eye_a to the visible surface while keeping
    // both Eye meshes opaque; the separate Transparent eye_c supplies glint.
    const json savedEyeSubmeshName =
        document["model"]["submeshes"][0]["name"];
    const json savedEyeVisibilityName =
        document["animations"][0]["mesh_visibility"][0]["mesh"];
    document["model"]["submeshes"][0]["name"] =
        "test_l_eye_a_mesh_shape";
    document["animations"][0]["mesh_visibility"][0]["mesh"] =
        "test_l_eye_a_mesh_shape";
    json nestedEyeSubmesh = document["model"]["submeshes"][0];
    nestedEyeSubmesh["name"] = "test_l_eye_b_mesh_shape";
    document["model"]["submeshes"].push_back(nestedEyeSubmesh);
    document["model"]["submesh_count"] = 2;
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData nestedPlaEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), nestedPlaEyeMesh, &outFail)) {
        return false;
    }
    if (nestedPlaEyeMesh.submeshAlphaMode.size() != 2u ||
        nestedPlaEyeMesh.submeshAlphaMode[0] != 0u ||
        nestedPlaEyeMesh.submeshAlphaMode[1] != 0u ||
        nestedPlaEyeMesh.submeshMaterialParams1.size() != 2u ||
        !nearlyEqual(
            nestedPlaEyeMesh.submeshMaterialParams1[0].w,
            -1.0f) ||
        !nearlyEqual(
            nestedPlaEyeMesh.submeshMaterialParams1[1].w,
            -1.0f) ||
        nestedPlaEyeMesh.submeshBaseTextures.size() != 2u ||
        !nestedPlaEyeMesh.submeshBaseTextures[0].hasPixels() ||
        !nestedPlaEyeMesh.submeshBaseTextures[1].hasPixels() ||
        nestedPlaEyeMesh.submeshBaseTextures[0].rgba[3] != 255u ||
        nestedPlaEyeMesh.submeshBaseTextures[1].rgba[3] != 255u ||
        nestedPlaEyeMesh.vertices.empty() ||
        plaEyeMesh.vertices.empty() ||
        nestedPlaEyeMesh.vertices[0].position.z <=
            plaEyeMesh.vertices[0].position.z) {
        outFail =
            "PLA pupil and iris did not flatten to an opaque visible composition";
        return false;
    }

    // Venomoth also has two meshes sharing an Eye material, but they are the
    // left and right eye_a surfaces; its lens and highlights use separate
    // Transparent materials. Repeated eye_a alone must not activate Paras's
    // nested-shell translucency.
    document["model"]["submeshes"][1]["name"] =
        "test_r_eye_a_mesh_shape";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData repeatedEyeAMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), repeatedEyeAMesh, &outFail)) {
        return false;
    }
    if (repeatedEyeAMesh.submeshAlphaMode.size() != 2u ||
        repeatedEyeAMesh.submeshAlphaMode[0] != 0u ||
        repeatedEyeAMesh.submeshAlphaMode[1] != 0u ||
        repeatedEyeAMesh.submeshBaseTextures.size() != 2u ||
        !repeatedEyeAMesh.submeshBaseTextures[0].hasPixels() ||
        !repeatedEyeAMesh.submeshBaseTextures[1].hasPixels() ||
        repeatedEyeAMesh.submeshBaseTextures[0].rgba[3] != 255u ||
        repeatedEyeAMesh.submeshBaseTextures[1].rgba[3] != 255u) {
        outFail =
            "Repeated single-shell Eye material was incorrectly made translucent";
        return false;
    }
    document["model"]["submeshes"].erase(
        document["model"]["submeshes"].end() - 1);
    document["model"]["submesh_count"] = 1;
    document["model"]["submeshes"][0]["name"] =
        savedEyeSubmeshName;
    document["animations"][0]["mesh_visibility"][0]["mesh"] =
        savedEyeVisibilityName;

    // Paras's third eye shell is authored with the Transparent family and
    // layer-local emission. Use that resolved emission as catchlight coverage
    // instead of drawing its body-atlas base map as another opaque eye disk.
    document["materials"][0]["shader_family"] = "Transparent";
    document["materials"][0]["vec4_parameters"]["EmissionColorLayer2"] =
        {0.87135625f, 0.87135625f, 0.87135625f, 1.0f};
    document["materials"][0]["float_parameters"]["EmissionIntensityLayer2"] =
        0.8f;
    document["model"]["submeshes"][0]["name"] =
        "test_l_eye_c_mesh_shape";
    document["animations"][0]["mesh_visibility"][0]["mesh"] =
        "test_l_eye_c_mesh_shape";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData transparentEyeShellMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), transparentEyeShellMesh, &outFail)) {
        return false;
    }
    if (transparentEyeShellMesh.submeshAlphaMode.size() != 1u ||
        transparentEyeShellMesh.submeshAlphaMode[0] != 2u ||
        transparentEyeShellMesh.submeshBaseTextures.size() != 1u ||
        !transparentEyeShellMesh.submeshBaseTextures[0].hasPixels() ||
        transparentEyeShellMesh.submeshBaseTextures[0].rgba[3] < 210u ||
        transparentEyeShellMesh.submeshBaseTextures[0].rgba[3] > 225u ||
        transparentEyeShellMesh.submeshEmissiveFactors.size() != 1u ||
        transparentEyeShellMesh.submeshEmissiveFactors[0].x < 0.99f ||
        transparentEyeShellMesh.vertices.empty() ||
        plaEyeMesh.vertices.empty() ||
        transparentEyeShellMesh.vertices[0].position.z <=
            plaEyeMesh.vertices[0].position.z) {
        outFail =
            "Transparent PLA eye shell did not resolve layer emission into catchlight coverage";
        return false;
    }

    // Venomoth's larger eye_b Transparent shell is a thin refractive lens,
    // not a sparse catchlight. Preserve the native forward-viewer fallback
    // as a 35%-covered dielectric clear-coat over eye_a's opaque core.
    document["model"]["submeshes"][0]["name"] =
        "test_l_eye_b_mesh_shape";
    document["animations"][0]["mesh_visibility"][0]["mesh"] =
        "test_l_eye_b_mesh_shape";
    document["materials"][0]["shader_options"]["RefractionMode"] =
        "Thin";
    document["materials"][0]["float_parameters"]["EmissionIntensityLayer2"] =
        0.01f;
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData transparentEyeLensMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), transparentEyeLensMesh, &outFail)) {
        return false;
    }
    if (transparentEyeLensMesh.submeshAlphaMode.size() != 1u ||
        transparentEyeLensMesh.submeshAlphaMode[0] != 2u ||
        transparentEyeLensMesh.submeshMaterialModes.size() != 1u ||
        transparentEyeLensMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::kNativeEyeClearCoatMaterialMode ||
        transparentEyeLensMesh.submeshMaterialParams1.size() != 1u ||
        transparentEyeLensMesh.submeshMaterialParams1[0].w < 0.99f ||
        transparentEyeLensMesh.submeshBaseTextures.size() != 1u ||
        !transparentEyeLensMesh.submeshBaseTextures[0].hasPixels() ||
        transparentEyeLensMesh.submeshBaseTextures[0].rgba[3] < 87u ||
        transparentEyeLensMesh.submeshBaseTextures[0].rgba[3] > 91u) {
        outFail =
            "Transparent eye_b lens did not preserve dielectric transmission coverage";
        return false;
    }
    document["model"]["submeshes"][0]["name"] =
        savedEyeSubmeshName;
    document["animations"][0]["mesh_visibility"][0]["mesh"] =
        savedEyeVisibilityName;

    // PLA Ponyta's Standard body material enables this path. Its layer mask
    // keeps red as the authored base selector while green/blue retain the
    // separately colored hoof and detail layers.
    document["materials"][0]["shader_family"] = "Standard";
    document["materials"][0]["shader_options"] = {
        {"EnableLerpBaseColorEmission", "True"},
    };
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData lerpBaseEmissionMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), lerpBaseEmissionMesh, &outFail)) {
        return false;
    }
    if (lerpBaseEmissionMesh.submeshBaseTextures.size() != 1u ||
        !lerpBaseEmissionMesh.submeshBaseTextures[0].hasPixels() ||
        lerpBaseEmissionMesh.submeshBaseTextures[0].rgba[0] < 220u ||
        lerpBaseEmissionMesh.submeshBaseTextures[0].rgba[1] < 80u ||
        lerpBaseEmissionMesh.submeshBaseTextures[0].rgba[1] > 100u ||
        lerpBaseEmissionMesh.submeshBaseTextures[0].rgba[2] < 55u ||
        lerpBaseEmissionMesh.submeshBaseTextures[0].rgba[2] > 75u) {
        outFail =
            "EnableLerpBaseColorEmission dropped a non-base selector layer";
        return false;
    }

    // The same PLA option is also used by Paras, whose mirrored two-wide
    // atlas makes red a literal orange Layer1 selector. Keep the authored UV
    // layout as the disambiguating source evidence: collapsing both variants
    // to Ponyta's red-as-base rule leaves Paras's body white.
    const json savedLayer2 =
        document["materials"][0]["vec4_parameters"]["BaseColorLayer2"];
    document["materials"][0]["textures"][1]["file"] = "white.png";
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorLayer2");
    document["materials"][0]["vec4_parameters"]["BaseColorLayer1"] =
        {0.484f, 0.059871793f, 0.025300812f, 1.0f};
    document["materials"][0]["vec4_parameters"]["UVScaleOffset"] =
        {2.0f, 1.0f, 0.0f, 0.0f};
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData mirroredLerpLayerMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), mirroredLerpLayerMesh, &outFail)) {
        return false;
    }
    if (mirroredLerpLayerMesh.submeshBaseTextures.size() != 1u ||
        !mirroredLerpLayerMesh.submeshBaseTextures[0].hasPixels() ||
        mirroredLerpLayerMesh.submeshBaseTextures[0].rgba[0] < 180u ||
        mirroredLerpLayerMesh.submeshBaseTextures[0].rgba[0] > 190u ||
        mirroredLerpLayerMesh.submeshBaseTextures[0].rgba[1] < 65u ||
        mirroredLerpLayerMesh.submeshBaseTextures[0].rgba[1] > 75u) {
        outFail =
            "Mirrored PLA Standard material dropped its literal red/Layer1 selector";
        return false;
    }

    // Ponyta's ordinary 1:1 atlas uses that same red channel as base-map
    // coverage, so the preceding fix must not tint its pale coat.
    document["materials"][0]["vec4_parameters"]["UVScaleOffset"] =
        {1.0f, 1.0f, 0.0f, 0.0f};
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData oneToOneLerpLayerMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), oneToOneLerpLayerMesh, &outFail)) {
        return false;
    }
    if (oneToOneLerpLayerMesh.submeshBaseTextures.size() != 1u ||
        !oneToOneLerpLayerMesh.submeshBaseTextures[0].hasPixels() ||
        oneToOneLerpLayerMesh.submeshBaseTextures[0].rgba[0] < 250u ||
        oneToOneLerpLayerMesh.submeshBaseTextures[0].rgba[1] < 250u ||
        oneToOneLerpLayerMesh.submeshBaseTextures[0].rgba[2] < 250u) {
        outFail =
            "One-to-one PLA Standard material lost its red/base-map selector";
        return false;
    }
    document["materials"][0]["vec4_parameters"].erase(
        "UVScaleOffset");

    // Z-A IkCharacter materials also enable LerpBaseColorEmission, but use
    // red as a literal Layer1 selector. Kakuna and Beedrill lose their yellow
    // body color if the Standard/Unlit base-selector exception leaks here.
    document["materials"][0]["shader_family"] = "IkCharacter";
    document["materials"][0]["textures"][1]["file"] = "white.png";
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorLayer2");
    document["materials"][0]["vec4_parameters"]["BaseColorLayer1"] =
        {0.61f, 0.5156f, 0.0305f, 1.0f};
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData zaCharacterMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), zaCharacterMesh, &outFail)) {
        return false;
    }
    if (zaCharacterMesh.submeshBaseTextures.size() != 1u ||
        !zaCharacterMesh.submeshBaseTextures[0].hasPixels() ||
        zaCharacterMesh.submeshBaseTextures[0].rgba[0] < 195u ||
        zaCharacterMesh.submeshBaseTextures[0].rgba[0] > 215u ||
        zaCharacterMesh.submeshBaseTextures[0].rgba[1] < 180u ||
        zaCharacterMesh.submeshBaseTextures[0].rgba[1] > 200u ||
        zaCharacterMesh.submeshBaseTextures[0].rgba[2] < 40u ||
        zaCharacterMesh.submeshBaseTextures[0].rgba[2] > 65u) {
        outFail =
            "Z-A IkCharacter red Layer1 selector was treated as a base selector";
        return false;
    }
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorLayer1");
    document["materials"][0]["vec4_parameters"]["BaseColorLayer2"] =
        savedLayer2;
    document["materials"][0]["textures"][1]["file"] = "mask.png";

    // Beedrill's Z-A wing material uses a solid-red Layer1 mask, but keeps
    // its vein/border artwork in BaseColorMap and enables BaseColorMultiply.
    // The layer tint must modulate that map rather than flattening it.
    document["materials"][0]["shader_options"]["BaseColorMultiply"] =
        "True";
    document["materials"][0]["textures"][0]["file"] = "strip.ppm";
    document["materials"][0]["textures"][1]["file"] = "white.png";
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "strip.ppm";
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorLayer2");
    document["materials"][0]["vec4_parameters"]["BaseColorLayer1"] =
        {0.5f, 0.5f, 0.5f, 1.0f};
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData multipliedLayerMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), multipliedLayerMesh, &outFail)) {
        return false;
    }
    if (multipliedLayerMesh.submeshBaseTextures.size() != 1u ||
        !multipliedLayerMesh.submeshBaseTextures[0].hasPixels() ||
        multipliedLayerMesh.submeshBaseTextures[0].width != 4 ||
        multipliedLayerMesh.submeshBaseTextures[0].rgba.size() < 16u ||
        multipliedLayerMesh.submeshBaseTextures[0].rgba[0] > 10u ||
        multipliedLayerMesh.submeshBaseTextures[0].rgba[12u] < 150u ||
        multipliedLayerMesh.submeshBaseTextures[0].rgba[12u] -
                multipliedLayerMesh.submeshBaseTextures[0].rgba[0] <
            140u) {
        outFail =
            "Z-A BaseColorMultiply flattened authored albedo detail under a material layer";
        return false;
    }
    document["materials"][0]["textures"][0]["file"] = "white.png";
    document["materials"][0]["textures"][1]["file"] = "mask.png";
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "white.png";
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorLayer1");
    document["materials"][0]["vec4_parameters"]["BaseColorLayer2"] =
        savedLayer2;

    // Z-A packs symmetric body islands into half of several maps, then uses
    // UVScaleOffset.x=2 with GL_MIRRORED_REPEAT. The canonical bake must
    // preserve that authored sampler transform for both color and standalone
    // material maps instead of clamping or sampling the unused half.
    document["materials"][0]["textures"][0]["file"] = "strip.ppm";
    document["materials"][0]["textures"][0]["wrap_s"] = 33648;
    document["materials"][0]["textures"][1]["file"] =
        "black-strip.ppm";
    document["materials"][0]["textures"][1]["wrap_s"] = 33648;
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "strip.ppm";
    document["materials"][0]["runtime_translation"]["occlusion_texture"] =
        "strip.ppm";
    document["materials"][0]["vec4_parameters"]["UVScaleOffset"] =
        {2.0f, 1.0f, 0.0f, 0.0f};
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData transformedUvMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), transformedUvMesh, &outFail)) {
        return false;
    }
    const auto transformedStripIsCorrect = [](const auto& texture) {
        if (!texture.hasPixels() || texture.width != 4 ||
            texture.height != 1 || texture.rgba.size() < 16u) {
            return false;
        }
        constexpr std::array<int, 4u> expected{20, 200, 200, 20};
        for (std::size_t pixel = 0u; pixel < expected.size(); ++pixel) {
            if (std::abs(
                    static_cast<int>(texture.rgba[pixel * 4u]) -
                    expected[pixel]) > 2) {
                return false;
            }
        }
        return true;
    };
    if (transformedUvMesh.submeshBaseTextures.size() != 1u ||
        transformedUvMesh.submeshOcclusionTextures.size() != 1u ||
        !transformedStripIsCorrect(
            transformedUvMesh.submeshBaseTextures[0]) ||
        !transformedStripIsCorrect(
            transformedUvMesh.submeshOcclusionTextures[0])) {
        outFail =
            "Z-A UVScaleOffset or mirrored-repeat material sampling was discarded";
        return false;
    }
    document["materials"][0]["textures"][0]["file"] = "white.png";
    document["materials"][0]["textures"][0]["wrap_s"] = 33071;
    document["materials"][0]["textures"][1]["file"] = "mask.png";
    document["materials"][0]["textures"][1]["wrap_s"] = 33071;
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "white.png";
    document["materials"][0]["runtime_translation"]["occlusion_texture"] =
        nullptr;
    document["materials"][0]["vec4_parameters"].erase(
        "UVScaleOffset");

    document["materials"][0]["shader_family"] = "Unlit";
    document["materials"][0]["shader_options"] = {
        {"EnableDisplacementMap", "True"},
    };
    document["materials"][0]["float_parameters"] = {
        {"DisplacementHeight", 0.05f},
        {"EmissionIntensity", 1.0f},
    };
    document["materials"][0]["vec4_parameters"]["UVScaleOffset3"] =
        {1.25f, 0.75f, 0.125f, 0.25f};
    document["materials"][0]["vec4_parameters"]["BaseColorLayer2"] =
        {4.0f, 0.8f, 0.18f, 1.0f};
    document["animations"].push_back({
        {"name", "pm0004_00_00_08201_loop01_loop"},
        {"duration_seconds", 2.0f},
        {"frame_rate", 60},
        {"loop", true},
        {"tracks", json::array()},
        {"mesh_visibility", json::array()},
        {"material_parameters", json::array({
            {{"mesh", "Triangle"},
             {"material", "test_material"},
             {"parameter", "UVScaleOffset"},
             {"x", json::array()},
             {"y", json::array()},
             {"z", json::array({
                 {{"frame", 0.0f}, {"value", 1.0f}},
                 {{"frame", 59.0f}, {"value", 0.0f}},
                 {{"frame", 60.0f}, {"value", 1.0f}},
                 {{"frame", 119.0f}, {"value", 0.0f}},
                 {{"frame", 120.0f}, {"value", 1.0f}},
             })},
             {"w", json::array()}},
            {{"mesh", "Triangle"},
             {"material", "test_material"},
             {"parameter", "UVScaleOffset3"},
             {"x", json::array()},
             {"y", json::array()},
             {"z", json::array({
                 {{"frame", 0.0f}, {"value", 0.0f}},
                 {{"frame", 39.0f}, {"value", 1.0f}},
                 {{"frame", 40.0f}, {"value", 0.0f}},
                 {{"frame", 79.0f}, {"value", 1.0f}},
                 {{"frame", 80.0f}, {"value", 0.0f}},
                 {{"frame", 119.0f}, {"value", 1.0f}},
                 {{"frame", 120.0f}, {"value", 0.0f}},
             })},
             {"w", json::array()}},
        })},
    });
    document["materials"][0]["textures"].push_back({
        {"role", "DisplacementMap"},
        {"file", "mask.png"},
        {"wrap_s", 10497},
        {"wrap_t", 10497},
        {"min_filter", 9729},
        {"mag_filter", 9729},
    });
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData unlitMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), unlitMesh, &outFail)) {
        return false;
    }
    if (unlitMesh.submeshMaterialModes.size() != 1u ||
        unlitMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::kNativeLayeredUnlitMaterialMode ||
        unlitMesh.submeshMaterialFlags.size() != 1u ||
        !nearlyEqual(unlitMesh.submeshMaterialFlags[0], 2.0f) ||
        unlitMesh.submeshBaseTextures.size() != 1u ||
        !unlitMesh.submeshBaseTextures[0].hasPixels() ||
        unlitMesh.submeshBaseTextures[0].rgba[0] < 220u ||
        unlitMesh.submeshBaseTextures[0].rgba[1] < 220u ||
        unlitMesh.submeshBaseTextures[0].rgba[2] < 220u ||
        unlitMesh.submeshNormalTextures.size() != 1u ||
        !unlitMesh.submeshNormalTextures[0].hasPixels() ||
        unlitMesh.submeshMetallicRoughnessTextures.size() != 1u ||
        !unlitMesh.submeshMetallicRoughnessTextures[0].hasPixels() ||
        unlitMesh.submeshMetallicRoughnessTextures[0].rgba[1] < 220u ||
        unlitMesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(unlitMesh.submeshMaterialParams0[0].x, 0.05f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams0[0].y, 1.0f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams0[0].z, 1.0f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams0[0].w, 1.5f) ||
        unlitMesh.submeshMaterialParams1.size() != 1u ||
        !nearlyEqual(unlitMesh.submeshMaterialParams1[0].x, 1.25f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams1[0].y, 0.75f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams1[0].z, 0.125f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams1[0].w, 0.25f) ||
        unlitMesh.submeshMaterialParams2.size() != 1u ||
        !nearlyEqual(unlitMesh.submeshMaterialParams2[0].r, 1.0f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams2[0].g, 1.0f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams2[0].b, 1.0f) ||
        unlitMesh.submeshMaterialParams3.size() != 1u ||
        !nearlyEqual(unlitMesh.submeshMaterialParams3[0].r, 4.0f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams3[0].g, 0.8f) ||
        !nearlyEqual(unlitMesh.submeshMaterialParams3[0].b, 0.18f)) {
        outFail =
            "native layered Unlit maps, HDR colors, and displacement were not preserved separately for runtime interpretation";
        return false;
    }
    if (unlitMesh.continuousMaterialAnimations.size() != 2u) {
        outFail =
            "native continuous material tracks were collapsed or discarded";
        return false;
    }
    const auto baseTrack = std::find_if(
        unlitMesh.continuousMaterialAnimations.begin(),
        unlitMesh.continuousMaterialAnimations.end(),
        [](const auto& track) {
            return track.parameter == game::runtime::render_model::
                                          MaterialAnimationParameter::
                                              UvScaleOffset;
        });
    const auto displacementTrack = std::find_if(
        unlitMesh.continuousMaterialAnimations.begin(),
        unlitMesh.continuousMaterialAnimations.end(),
        [](const auto& track) {
            return track.parameter == game::runtime::render_model::
                                          MaterialAnimationParameter::
                                              UvScaleOffset3;
        });
    if (baseTrack == unlitMesh.continuousMaterialAnimations.end() ||
        displacementTrack ==
            unlitMesh.continuousMaterialAnimations.end() ||
        !nearlyEqual(baseTrack->durationSec, 2.0f) ||
        !nearlyEqual(baseTrack->sourceFrameRate, 60.0f) ||
        !baseTrack->loop ||
        baseTrack->components[2].keys.size() != 5u ||
        !nearlyEqual(
            baseTrack->components[2].keys[2].timeSec,
            1.0f) ||
        !nearlyEqual(
            baseTrack->components[2].keys[2].value,
            1.0f) ||
        displacementTrack->components[2].keys.size() != 7u ||
        !nearlyEqual(
            displacementTrack->components[2].keys[2].timeSec,
            40.0f / 60.0f) ||
        !nearlyEqual(
            displacementTrack->components[2].keys[2].value,
            0.0f)) {
        outFail =
            "native continuous material key timing or reset values changed";
        return false;
    }

    // LGPE PokeDefaultShader eyes use Col0Tex alpha as a mask for the
    // independent LyCol0Tex iris atlas. Layer1BaseU is inside Layer1UVScaleU,
    // so 0.25 at a scale of four is a complete repeat rather than a quarter
    // atlas shift. It is not cutout transparency: an alpha-zero eye-white
    // pixel must reveal the iris layer, while an alpha-one eyelid pixel must
    // retain the base layer and both are opaque.
    document["materials"][0] = {
        {"name", "EyeL"},
        {"shader_family", "PokeDefaultShader"},
        {"shader_options", {{"Layer1Enable", "true"}}},
        {"float_parameters",
         {{"ColorUVScaleU", 1.0f},
          {"ColorUVScaleV", 1.0f},
          {"ColorUVTranslateU", 0.0f},
          {"ColorUVTranslateV", 0.0f},
          {"ColorBaseU", 0.0f},
          {"ColorBaseV", 0.0f},
          {"Layer1UVScaleU", 4.0f},
          {"Layer1UVScaleV", 1.0f},
          {"Layer1UVTranslateU", 0.0f},
          {"Layer1UVTranslateV", 0.0f},
          {"Layer1BaseU", 0.25f},
          {"Layer1BaseV", 0.0f}}},
        {"vec4_parameters", json::object()},
        {"textures",
         json::array({
             {{"role", "Col0Tex"},
              {"file", "lgpe-layer-mask.tga"},
              {"wrap_s", 10497},
              {"wrap_t", 10497}},
             {{"role", "LyCol0Tex"},
              {"file", "lgpe-iris.tga"},
              {"wrap_s", 10497},
              {"wrap_t", 10497}},
         })},
        {"runtime_translation",
         {{"base_color_texture", "lgpe-layer-mask.tga"},
          {"normal_texture", nullptr},
          {"roughness_texture", nullptr},
          {"metallic_texture", nullptr},
          {"occlusion_texture", nullptr},
          {"emissive_texture", nullptr},
          {"normal_scale", 1.0f},
          {"metallic_factor", 0.0f},
          {"roughness_factor", 0.5f},
          {"occlusion_strength", 1.0f},
          {"alpha_mode", "mask"},
          {"alpha_cutoff", 0.5f}}},
    };
    document["animations"] = json::array();
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData lgpeEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), lgpeEyeMesh, &outFail)) {
        return false;
    }
    if (lgpeEyeMesh.submeshBaseTextures.size() != 1u ||
        !lgpeEyeMesh.submeshBaseTextures[0].hasPixels() ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba.size() < 8u ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba[0] < 245u ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba[1] > 10u ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba[2] > 10u ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba[3] != 255u ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba[4] > 10u ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba[5] > 10u ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba[6] < 245u ||
        lgpeEyeMesh.submeshBaseTextures[0].rgba[7] != 255u ||
        lgpeEyeMesh.submeshAlphaMode.size() != 1u ||
        lgpeEyeMesh.submeshAlphaMode[0] != 0u) {
        outFail =
            "LGPE layered eye mask was treated as surface transparency or lost its iris layer";
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
