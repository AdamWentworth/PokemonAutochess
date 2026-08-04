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

    const json material = {
        {"name", "test_material"},
        {"shader_family", "EyeClearCoat"},
        {"shader_options", {{"EnableHighlight", "True"}}},
        {"float_parameters",
         {{"RoughnessHighlight", 0.51f},
          {"EmissionIntensityLayer5", 0.8f},
          {"MetallicLayer2", 1.0f},
          {"RoughnessLayer2", 0.8f}}},
        {"vec4_parameters",
         {{"BaseColorLayer2", {0.8f, 0.1f, 0.05f, 1.0f}},
          {"EmissionColorLayer5",
           {0.87135625f, 0.87135625f, 0.87135625f, 1.0f}},
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
             {{"role", "HighlightMaskMap"},
              {"file", "white.png"},
              {"wrap_s", 33648},
              {"wrap_t", 33648},
              {"min_filter", 9729},
              {"mag_filter", 9729}},
         })},
        {"runtime_translation",
         {{"base_color_texture", "white.png"},
          {"normal_texture", nullptr},
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
        mesh.submeshMaterialModes.size() != 1u ||
        mesh.submeshMaterialModes[0] !=
            game::runtime::render_model::kNativeEyeClearCoatMaterialMode ||
        mesh.submeshMetallicRoughnessTextures.size() != 1u ||
        !mesh.submeshMetallicRoughnessTextures[0].hasPixels() ||
        mesh.submeshMetallicRoughnessTextures[0].rgba[1] < 195u ||
        mesh.submeshMetallicRoughnessTextures[0].rgba[1] > 210u ||
        mesh.submeshMetallicRoughnessTextures[0].rgba[2] < 250u ||
        !nearlyEqual(mesh.submeshMetallicFactor[0], 1.0f) ||
        !nearlyEqual(mesh.submeshRoughnessFactor[0], 1.0f) ||
        mesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(mesh.submeshMaterialParams0[0].x, 0.2f) ||
        !nearlyEqual(mesh.submeshMaterialParams0[0].y, 0.51f) ||
        mesh.submeshMaterialParams1.size() != 1u ||
        !nearlyEqual(mesh.submeshMaterialParams1[0].x, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams1[0].y, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams1[0].z, 0.0f) ||
        !nearlyEqual(mesh.submeshMaterialParams1[0].w, 0.0f)) {
        outFail =
            "EyeClearCoat layer response was not preserved for its dedicated runtime material";
        return false;
    }

    // PLA names this family `Eye` and stores its authored glint in a
    // dedicated HighlightMaskMap / layer-5 emission pair. It must use the eye
    // material path without inventing Scarlet's clear-coat coverage.
    document["materials"][0]["shader_family"] = "Eye";
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorClearCoat");
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
        !nearlyEqual(plaEyeMesh.submeshMaterialParams1[0].w, 0.0f) ||
        plaEyeMesh.submeshEmissiveTextures.size() != 1u ||
        !plaEyeMesh.submeshEmissiveTextures[0].hasPixels() ||
        plaEyeMesh.submeshEmissiveTextures[0].rgba[0] < 210u ||
        plaEyeMesh.submeshEmissiveFactors.size() != 1u ||
        plaEyeMesh.submeshEmissiveFactors[0].x < 0.99f) {
        outFail =
            "PLA Eye highlight mask or no-clear-coat contract was not preserved";
        return false;
    }

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
