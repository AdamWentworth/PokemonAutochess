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
    const std::array<std::uint8_t, 23u> whiteStripPpm{
        'P', '6', '\n', '4', ' ', '1', '\n', '2', '5', '5', '\n',
        255u, 255u, 255u,
        255u, 255u, 255u,
        255u, 255u, 255u,
        255u, 255u, 255u};
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
    const std::vector<std::uint8_t> highlightNormalPpm = [] {
        constexpr int kSize = 32;
        constexpr float kCenter = 15.5f;
        constexpr float kRadius = 14.0f;
        std::vector<std::uint8_t> bytes{
            'P', '6', '\n', '3', '2', ' ', '3', '2', '\n',
            '2', '5', '5', '\n'};
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const float nx =
                    (static_cast<float>(x) - kCenter) / kRadius;
                const float ny =
                    (static_cast<float>(y) - kCenter) / kRadius;
                const float radiusSquared = nx * nx + ny * ny;
                if (radiusSquared > 1.0f) {
                    bytes.insert(bytes.end(), {128u, 128u, 255u});
                    continue;
                }
                const float nz = std::sqrt(
                    std::max(0.0f, 1.0f - radiusSquared));
                const auto encode = [](float value) {
                    return static_cast<std::uint8_t>(std::lround(
                        std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) *
                        255.0f));
                };
                bytes.push_back(encode(nx));
                bytes.push_back(encode(ny));
                bytes.push_back(encode(nz));
            }
        }
        return bytes;
    }();
    const std::vector<std::uint8_t> smallEyeMaskPpm = [] {
        constexpr int kSize = 32;
        constexpr float kCenterX = 20.0f;
        constexpr float kCenterY = 11.0f;
        constexpr float kRadiusX = 3.5f;
        constexpr float kRadiusY = 7.0f;
        std::vector<std::uint8_t> bytes{
            'P', '6', '\n', '3', '2', ' ', '3', '2', '\n',
            '2', '5', '5', '\n'};
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const float nx =
                    (static_cast<float>(x) - kCenterX) / kRadiusX;
                const float ny =
                    (static_cast<float>(y) - kCenterY) / kRadiusY;
                if (nx * nx + ny * ny <= 1.0f) {
                    bytes.insert(bytes.end(), {0u, 255u, 0u});
                } else {
                    bytes.insert(bytes.end(), {255u, 0u, 0u});
                }
            }
        }
        return bytes;
    }();
    const std::vector<std::uint8_t> smallEyeNormalPpm = [] {
        constexpr int kSize = 32;
        constexpr float kCenterX = 20.0f;
        constexpr float kCenterY = 11.0f;
        constexpr float kRadiusX = 3.5f;
        constexpr float kRadiusY = 7.0f;
        std::vector<std::uint8_t> bytes{
            'P', '6', '\n', '3', '2', ' ', '3', '2', '\n',
            '2', '5', '5', '\n'};
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const float nx =
                    (static_cast<float>(x) - kCenterX) / kRadiusX;
                const float ny =
                    (static_cast<float>(y) - kCenterY) / kRadiusY;
                const float radiusSquared = nx * nx + ny * ny;
                if (radiusSquared > 1.0f) {
                    bytes.insert(bytes.end(), {128u, 128u, 255u});
                    continue;
                }
                const float nz = std::sqrt(
                    std::max(0.0f, 1.0f - radiusSquared));
                const auto encode = [](float value) {
                    return static_cast<std::uint8_t>(std::lround(
                        std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) *
                        255.0f));
                };
                bytes.push_back(encode(nx));
                bytes.push_back(encode(ny));
                bytes.push_back(encode(nz));
            }
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
              {"file", "highlight-normal.ppm"},
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
        {"source", {{"profile", "unit-test"}}},
        {"coordinate_system",
         {{"texcoords_0", "gamefreak_native"},
          {"unit_scale_to_meters", 0.01f}}},
        {"payload",
         {{"file",
           "_payloads/sha256/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bin"},
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
    const fs::path payloadPath =
        temp.root / "_payloads" / "sha256" /
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bin";
    const fs::path whitePath = temp.root / "white.png";
    const fs::path maskPath = temp.root / "mask.png";
    const fs::path stripPath = temp.root / "strip.ppm";
    const fs::path blackStripPath = temp.root / "black-strip.ppm";
    const fs::path whiteStripPath = temp.root / "white-strip.ppm";
    const fs::path redPath = temp.root / "red.ppm";
    const fs::path blueMaskPath = temp.root / "blue-mask.ppm";
    const fs::path accessoryNormalPath =
        temp.root / "accessory-normal.ppm";
    const fs::path flatNormalPath = temp.root / "flat-normal.ppm";
    const fs::path highlightNormalPath =
        temp.root / "highlight-normal.ppm";
    const fs::path smallEyeMaskPath = temp.root / "small-eye-mask.ppm";
    const fs::path smallEyeNormalPath =
        temp.root / "small-eye-normal.ppm";
    const fs::path lgpeLayerMaskPath = temp.root / "lgpe-layer-mask.tga";
    const fs::path lgpeIrisPath = temp.root / "lgpe-iris.tga";
    {
        fs::create_directories(payloadPath.parent_path(), error);
        if (error) {
            outFail = "could not create shared native payload directory";
            return false;
        }
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
        std::ofstream output(whiteStripPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(whiteStripPpm.data()),
            static_cast<std::streamsize>(whiteStripPpm.size()));
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
        std::ofstream output(highlightNormalPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(highlightNormalPpm.data()),
            static_cast<std::streamsize>(highlightNormalPpm.size()));
    }
    {
        std::ofstream output(smallEyeMaskPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(smallEyeMaskPpm.data()),
            static_cast<std::streamsize>(smallEyeMaskPpm.size()));
    }
    {
        std::ofstream output(smallEyeNormalPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(smallEyeNormalPpm.data()),
            static_cast<std::streamsize>(smallEyeNormalPpm.size()));
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
        !nearlyEqual(visibility.sourceFrameRate, 60.0f) ||
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
    {
        std::vector<std::uint8_t> negativeEyePayload = payload.bytes;
        const std::size_t uvOffset =
            texcoords.at("offset_bytes").get<std::size_t>();
        const std::array<float, 3u> signedEyeU{
            -0.15f,
            -0.85f,
            -0.15f};
        for (std::size_t vertex = 0u; vertex < signedEyeU.size(); ++vertex) {
            std::memcpy(
                negativeEyePayload.data() + uvOffset + vertex * 8u,
                &signedEyeU[vertex],
                sizeof(float));
        }
        const fs::path negativePayloadPath =
            temp.root / "negative-eye-u.bin";
        const fs::path negativeManifestPath =
            temp.root / "negative-eye-u.phmodel";
        json negativeDocument = document;
        negativeDocument["payload"]["file"] =
            negativePayloadPath.filename().generic_string();
        negativeDocument["payload"]["byte_length"] =
            negativeEyePayload.size();
        {
            std::ofstream output(negativePayloadPath, std::ios::binary);
            output.write(
                reinterpret_cast<const char*>(negativeEyePayload.data()),
                static_cast<std::streamsize>(negativeEyePayload.size()));
        }
        {
            std::ofstream output(negativeManifestPath);
            output << negativeDocument.dump(2);
        }
        game::runtime::render_model::MeshData negativeEyeMesh;
        if (!tools::phlosion_native_model_ir::load(
                negativeManifestPath.string(),
                negativeEyeMesh,
                &outFail) ||
            negativeEyeMesh.vertices.size() != 3u ||
            !nearlyEqual(negativeEyeMesh.vertices[0].uv.x, 0.85f) ||
            !nearlyEqual(negativeEyeMesh.vertices[1].uv.x, 0.15f) ||
            !nearlyEqual(negativeEyeMesh.vertices[2].uv.x, 0.85f)) {
            if (outFail.empty()) {
                outFail =
                    "signed Scarlet EyeClearCoat UV tile did not fold into the runtime texture domain";
            }
            return false;
        }
    }
    if (mesh.hasVertexColor || mesh.hasVertexBaseColor ||
        !nearlyEqual(mesh.vertices[0].color.r, 0.25f)) {
        outFail =
            "native vertex colors were not preserved as non-albedo evidence";
        return false;
    }
    const auto baseTextureByte = [](
        const game::runtime::render_model::MeshData& source,
        int x,
        int y,
        std::size_t channel) -> std::uint8_t {
        if (source.submeshBaseTextures.empty()) return 0u;
        const auto& texture = source.submeshBaseTextures[0];
        if (x < 0 || y < 0 || x >= texture.width || y >= texture.height) {
            return 0u;
        }
        const std::size_t offset =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(texture.width) +
             static_cast<std::size_t>(x)) * 4u + channel;
        return offset < texture.rgba.size() ? texture.rgba[offset] : 0u;
    };
    if (mesh.submeshBaseTextures.size() != 1u ||
        !mesh.submeshBaseTextures[0].hasPixels() ||
        mesh.submeshBaseTextures[0].rgba[0] < 220u ||
        mesh.submeshBaseTextures[0].rgba[1] < 80u ||
        mesh.submeshBaseTextures[0].rgba[1] > 100u ||
        mesh.submeshBaseTextures[0].rgba[2] < 55u ||
        mesh.submeshBaseTextures[0].rgba[2] > 75u ||
        mesh.submeshBaseTextures[0].width != 32 ||
        mesh.submeshBaseTextures[0].height != 32 ||
        baseTextureByte(mesh, 20, 11, 0u) < 245u ||
        baseTextureByte(mesh, 20, 11, 1u) < 245u ||
        baseTextureByte(mesh, 20, 11, 2u) < 245u ||
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
                                  std::to_string(baseTextureByte(mesh, 20, 11, 0u)) + "," +
                                  std::to_string(baseTextureByte(mesh, 20, 11, 1u)) + "," +
                                  std::to_string(baseTextureByte(mesh, 20, 11, 2u))
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
        baseTextureByte(projectedEyeMesh, 16, 25, 0u) < 210u ||
        baseTextureByte(projectedEyeMesh, 20, 11, 0u) > 235u) {
        outFail =
            "Scarlet eye catchlight did not follow its authored pointlight/mesh projection";
        return false;
    }
    document["materials"][0]["shader_options"].erase(
        "PointLightIndex");
    document["skeleton"]["bones"][0]["name"] = "Root";

    // NormalMap1 scales Scarlet's highlight-normal sphere to the authored
    // iris/pupil footprint. A Pikachu-sized absolute catchlight erased the
    // much narrower Drowzee and Hypno pupils, so a small vertical fixture
    // must retain most of its black pupil while still receiving a glint.
    document["materials"][0]["textures"][1]["file"] =
        "small-eye-mask.ppm";
    document["materials"][0]["textures"][3]["file"] =
        "small-eye-normal.ppm";
    document["materials"][0]["vec4_parameters"]["BaseColorLayer1"] =
        {0.7157f, 0.7157f, 0.7157f, 1.0f};
    document["materials"][0]["vec4_parameters"]["BaseColorLayer2"] =
        {0.0047770697f, 0.0047770697f, 0.0047770697f, 1.0f};
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData narrowPupilMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), narrowPupilMesh, &outFail)) {
        return false;
    }
    std::size_t darkPupilPixels = 0u;
    std::size_t brightPupilPixels = 0u;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const float nx =
                (static_cast<float>(x) - 20.0f) / 3.5f;
            const float ny =
                (static_cast<float>(y) - 11.0f) / 7.0f;
            if (nx * nx + ny * ny > 1.0f) continue;
            const std::uint8_t red =
                baseTextureByte(narrowPupilMesh, x, y, 0u);
            if (red < 64u) ++darkPupilPixels;
            if (red > 220u) ++brightPupilPixels;
        }
    }
    if (darkPupilPixels < 55u ||
        brightPupilPixels == 0u ||
        brightPupilPixels > 16u) {
        outFail =
            "Scarlet EyeClearCoat catchlight did not preserve a narrow authored pupil";
        return false;
    }
    document["materials"][0]["textures"][1]["file"] = "mask.png";
    document["materials"][0]["textures"][3]["file"] =
        "highlight-normal.ppm";
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorLayer1");
    document["materials"][0]["vec4_parameters"]["BaseColorLayer2"] =
        {0.8f, 0.1f, 0.05f, 1.0f};

    // Scarlet uses EyeClearCoat for glossy body accessories as well as eyes.
    // Golduck's Blender graph resolves body_c to plain PBR: untouched red
    // BaseColorMap, body_a NormalMap with its opaque alpha feeding normal Z,
    // 0.45 base roughness, and no layer emission or synthetic catchlight.
    // Its saved Blender viewport has coat weight zero; keep NormalMap1 out of
    // the portable surface-normal slot and leave this as ordinary PBR.
    document["model"]["name"] = "pm0055_00_00";
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
        clearCoatAccessoryMesh.submeshNormalTextures[0].rgba[2] != 255u ||
        clearCoatAccessoryMesh.submeshNormalScale.size() != 1u ||
        !nearlyEqual(clearCoatAccessoryMesh.submeshNormalScale[0], 0.0f) ||
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
    document["model"]["name"] = "native_test";
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
    fs::create_directories(staleTexture.parent_path());
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
    std::vector<game::runtime::phlosion::ModelTextureDependency>
        firstDependencies;
    if (!game::runtime::phlosion::listModelObjectTextureDependencies(
            cookedObject.string(), firstDependencies, &outFail) ||
        firstDependencies.empty() ||
        std::any_of(
            firstDependencies.begin(),
            firstDependencies.end(),
            [](const auto& dependency) {
                return !std::string_view(dependency.assetId).starts_with(
                    "dependencies/ktx2/");
            })) {
        if (outFail.empty()) {
            outFail = "PHLO cook did not publish shared KTX2 dependencies";
        }
        return false;
    }
    const fs::path privateTextureDirectory =
        cookedObject.parent_path() / "textures";
    if (fs::exists(privateTextureDirectory) &&
        !fs::is_empty(privateTextureDirectory, error)) {
        outFail = "PHLO cook retained private object texture payloads";
        return false;
    }

    const fs::path siblingManifest = temp.root / "native_test_sibling.phmodel";
    {
        std::ofstream output(siblingManifest);
        output << document.dump(2);
    }
    game::runtime::phlosion::ModelCookStats siblingCook;
    if (!game::runtime::phlosion::cookModelObject(
            siblingManifest.string(),
            mesh,
            cookedRoot.string(),
            "Character",
            siblingCook,
            &outFail)) {
        return false;
    }
    std::vector<game::runtime::phlosion::ModelTextureDependency>
        siblingDependencies;
    if (!game::runtime::phlosion::listModelObjectTextureDependencies(
            game::runtime::phlosion::objectPathForModel(
                siblingManifest.string(), cookedRoot.string()),
            siblingDependencies,
            &outFail)) {
        return false;
    }
    const auto dependencyIds = [](const auto& dependencies) {
        std::vector<std::string> ids;
        for (const auto& dependency : dependencies) {
            ids.push_back(dependency.assetId);
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    if (dependencyIds(firstDependencies) !=
        dependencyIds(siblingDependencies)) {
        outFail =
            "byte-identical textures with identical semantics did not share storage";
        return false;
    }

    auto samplerVariant = mesh;
    bool changedSampler = false;
    const auto changeFirstSampler = [&](auto& textures) {
        if (changedSampler) return;
        for (auto& texture : textures) {
            if (!texture.hasPixels()) continue;
            texture.wrapS = texture.wrapS == 33071 ? 10497 : 33071;
            changedSampler = true;
            return;
        }
    };
    changeFirstSampler(samplerVariant.submeshBaseTextures);
    changeFirstSampler(samplerVariant.submeshNormalTextures);
    changeFirstSampler(samplerVariant.submeshMetallicRoughnessTextures);
    changeFirstSampler(samplerVariant.submeshOcclusionTextures);
    changeFirstSampler(samplerVariant.submeshEmissiveTextures);
    const fs::path samplerManifest = temp.root / "native_test_sampler.phmodel";
    {
        std::ofstream output(samplerManifest);
        output << document.dump(2);
    }
    game::runtime::phlosion::ModelCookStats samplerCook;
    if (!game::runtime::phlosion::cookModelObject(
            samplerManifest.string(),
            samplerVariant,
            cookedRoot.string(),
            "Character",
            samplerCook,
            &outFail)) {
        return false;
    }
    std::vector<game::runtime::phlosion::ModelTextureDependency>
        samplerDependencies;
    if (!game::runtime::phlosion::listModelObjectTextureDependencies(
            game::runtime::phlosion::objectPathForModel(
                samplerManifest.string(), cookedRoot.string()),
            samplerDependencies,
            &outFail) ||
        dependencyIds(firstDependencies) ==
            dependencyIds(samplerDependencies)) {
        if (outFail.empty()) {
            outFail =
                "sampler-incompatible textures aliased one shared identity";
        }
        return false;
    }

    const fs::path immutablePayload = firstDependencies.front().physicalPath;
    std::vector<std::uint8_t> originalPayload(
        static_cast<std::size_t>(fs::file_size(immutablePayload)));
    {
        std::ifstream input(immutablePayload, std::ios::binary);
        input.read(
            reinterpret_cast<char*>(originalPayload.data()),
            static_cast<std::streamsize>(originalPayload.size()));
    }
    std::vector<std::uint8_t> originalObject(
        static_cast<std::size_t>(fs::file_size(cookedObject)));
    {
        std::ifstream input(cookedObject, std::ios::binary);
        input.read(
            reinterpret_cast<char*>(originalObject.data()),
            static_cast<std::streamsize>(originalObject.size()));
    }
    {
        std::ofstream output(immutablePayload, std::ios::binary | std::ios::trunc);
        output << "collision";
    }
    game::runtime::phlosion::ModelCookStats rejectedCook;
    std::string collisionError;
    const bool acceptedCollision =
        game::runtime::phlosion::cookModelObject(
            manifestPath.string(),
            mesh,
            cookedRoot.string(),
            "Character",
            rejectedCook,
            &collisionError);
    std::vector<std::uint8_t> objectAfterRejectedCook(
        static_cast<std::size_t>(fs::file_size(cookedObject)));
    {
        std::ifstream input(cookedObject, std::ios::binary);
        input.read(
            reinterpret_cast<char*>(objectAfterRejectedCook.data()),
            static_cast<std::streamsize>(objectAfterRejectedCook.size()));
    }
    {
        std::ofstream output(immutablePayload, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(originalPayload.data()),
            static_cast<std::streamsize>(originalPayload.size()));
    }
    if (acceptedCollision ||
        collisionError.find("identity collision") == std::string::npos ||
        objectAfterRejectedCook != originalObject) {
        outFail =
            "immutable dependency collision did not preserve the published object";
        return false;
    }

    // Chansey's Scarlet SSS shader keeps the egg softly illuminated with an
    // EnableJewel response. The egg is part of the shared body mesh, so the
    // portable fallback must derive only its exact neutral/glossy atlas region
    // as supplemental emission and must not affect generic SSS materials.
    {
        const json savedDocument = document;
        document["materials"][0]["name"] = "body";
        document["materials"][0]["shader_family"] = "SSS";
        document["materials"][0]["shader_options"] = {
            {"EnableJewel", "1"}};
        document["materials"][0]["float_parameters"] = json::object();
        document["materials"][0]["vec4_parameters"] = json::object();
        document["materials"][0]["textures"] = json::array({
            {{"role", "BaseColorMap"},
             {"source", "pm0113_00_00_body_alb.bntx"},
             {"file", "white.png"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
            {{"role", "RoughnessMap"},
             {"source", "pm0113_00_00_body_rgn.bntx"},
             {"file", "black-strip.ppm"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
        });
        document["materials"][0]["runtime_translation"] = {
            {"base_color_texture", "white.png"},
            {"normal_texture", nullptr},
            {"roughness_texture", "black-strip.ppm"},
            {"metallic_texture", nullptr},
            {"occlusion_texture", nullptr},
            {"emissive_texture", nullptr},
            {"normal_scale", 1.0f},
            {"metallic_factor", 0.0f},
            {"roughness_factor", 1.0f},
            {"occlusion_strength", 1.0f},
            {"alpha_mode", "opaque"},
            {"alpha_cutoff", 0.5f},
        };
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData chanseyJewelMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), chanseyJewelMesh, &outFail)) {
            return false;
        }
        document["materials"][0]["textures"][0]["source"] =
            "generic_body_alb.bntx";
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData genericSssMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), genericSssMesh, &outFail)) {
            return false;
        }
        if (chanseyJewelMesh.submeshEmissiveTextures.size() != 1u ||
            !chanseyJewelMesh.submeshEmissiveTextures[0].hasPixels() ||
            chanseyJewelMesh.submeshEmissiveTextures[0].rgba[0] < 100u ||
            chanseyJewelMesh.submeshEmissiveFactors.size() != 1u ||
            !nearlyEqual(
                chanseyJewelMesh.submeshEmissiveFactors[0].x,
                1.0f) ||
            genericSssMesh.submeshEmissiveTextures.size() != 1u ||
            genericSssMesh.submeshEmissiveTextures[0].hasPixels()) {
            outFail =
                "Chansey jewel response was missing or leaked into generic SSS materials";
            return false;
        }
        document = savedDocument;
    }

    // Z-A stores the Staryu-family jewel color in FresnelEffect's BaseColor
    // constant while its BaseColorMap remains neutral white. Preserve that
    // regular/shiny tint in the portable PBR fallback, and keep the exception
    // qualified to the two source jewel textures.
    {
        const json savedDocument = document;
        document["materials"][0]["name"] = "body_01";
        document["materials"][0]["shader_family"] = "FresnelEffect";
        document["materials"][0]["shader_options"] = json::object();
        document["materials"][0]["float_parameters"] = json::object();
        document["materials"][0]["vec4_parameters"] = {
            {"BaseColor", {0.5209858f, 0.026241764f, 0.04817167f, 1.0f}}};
        document["materials"][0]["textures"] = json::array({
            {{"role", "BaseColorMap"},
             {"source", "pm0120_00_00_body_01_alb_.bntx"},
             {"file", "white.png"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
        });
        document["materials"][0]["runtime_translation"] = {
            {"base_color_texture", "white.png"},
            {"normal_texture", nullptr},
            {"roughness_texture", nullptr},
            {"metallic_texture", nullptr},
            {"occlusion_texture", nullptr},
            {"emissive_texture", nullptr},
            {"normal_scale", 1.0f},
            {"metallic_factor", 0.5f},
            {"roughness_factor", 0.2f},
            {"occlusion_strength", 1.0f},
            {"alpha_mode", "opaque"},
            {"alpha_cutoff", 0.5f},
        };
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData staryuJewelMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), staryuJewelMesh, &outFail)) {
            return false;
        }
        document["materials"][0]["textures"][0]["source"] =
            "generic_fresnel_alb.bntx";
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData genericFresnelMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), genericFresnelMesh, &outFail)) {
            return false;
        }
        if (baseTextureByte(staryuJewelMesh, 0, 0, 0u) < 180u ||
            baseTextureByte(staryuJewelMesh, 0, 0, 1u) > 70u ||
            baseTextureByte(staryuJewelMesh, 0, 0, 2u) > 90u ||
            baseTextureByte(genericFresnelMesh, 0, 0, 0u) < 245u ||
            baseTextureByte(genericFresnelMesh, 0, 0, 1u) < 245u ||
            baseTextureByte(genericFresnelMesh, 0, 0, 2u) < 245u) {
            outFail =
                "Staryu-family Fresnel jewel tint was missing or leaked into generic materials";
            return false;
        }
        document = savedDocument;
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

    // The same PLA option is also used by Paras and later Pokemon body
    // materials whose equal-resolution layer mask makes red a literal Layer1
    // selector. Collapsing both responses to Ponyta's red-as-base rule leaves
    // their principal body tint white.
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

    // The qualified Ponyta-family native body sources use that same red
    // channel as base-map coverage, so the preceding fix must not tint either
    // pale coat. Rapidash has separate body_a/body_b atlases with this exact
    // response.
    document["materials"][0]["vec4_parameters"]["UVScaleOffset"] =
        {1.0f, 1.0f, 0.0f, 0.0f};
    document["materials"][0]["textures"][0]["file"] =
        "white-strip.ppm";
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "white-strip.ppm";
    for (const std::string& source : {
             std::string("pm0077_00_00_body_alb.bntx"),
             std::string("pm0078_00_00_body_a_alb.bntx"),
             std::string("pm0078_00_00_body_b_alb.bntx")}) {
        document["materials"][0]["textures"][0]["source"] = source;
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData fireHorseBodyMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), fireHorseBodyMesh, &outFail)) {
            return false;
        }
        if (fireHorseBodyMesh.submeshBaseTextures.size() != 1u ||
            !fireHorseBodyMesh.submeshBaseTextures[0].hasPixels() ||
            fireHorseBodyMesh.submeshBaseTextures[0].rgba[0] < 250u ||
            fireHorseBodyMesh.submeshBaseTextures[0].rgba[1] < 250u ||
            fireHorseBodyMesh.submeshBaseTextures[0].rgba[2] < 250u) {
            outFail =
                "PLA Ponyta-family material lost its red/base-map selector: " +
                source;
            return false;
        }
    }
    document["materials"][0]["vec4_parameters"].erase(
        "UVScaleOffset");
    document["materials"][0]["textures"][0]["file"] = "white.png";
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "white.png";

    // Other PLA Standard body layers - including a higher-resolution albedo over
    // a smaller mask - tint the authored atlas instead of replacing it. Preserve
    // its tonal detail and literal red/Layer1 coverage so Abra's eyelids and
    // Machamp's blue-gray limb/foot definition survive the offline bake.
    document["materials"][0]["textures"][0]["source"] =
        "pm0068_00_00_body_b_alb.bntx";
    document["materials"][0]["textures"][0]["file"] = "strip.ppm";
    document["materials"][0]["textures"][1]["file"] = "white.png";
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "strip.ppm";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData plaTintedDetailMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), plaTintedDetailMesh, &outFail)) {
        return false;
    }
    if (plaTintedDetailMesh.submeshBaseTextures.size() != 1u ||
        !plaTintedDetailMesh.submeshBaseTextures[0].hasPixels() ||
        plaTintedDetailMesh.submeshBaseTextures[0].width != 4 ||
        plaTintedDetailMesh.submeshBaseTextures[0].rgba.size() < 16u ||
        plaTintedDetailMesh.submeshBaseTextures[0].rgba[0] > 10u ||
        plaTintedDetailMesh.submeshBaseTextures[0].rgba[12u] < 150u ||
        plaTintedDetailMesh.submeshBaseTextures[0].rgba[12u] -
                plaTintedDetailMesh.submeshBaseTextures[0].rgba[0] <
            140u) {
        outFail =
            "PLA Standard layer tint flattened authored base-color detail";
        return false;
    }
    document["materials"][0]["textures"][0]["file"] = "white.png";
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "white.png";

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

    // IkCharacter's RGBA layer mask is an ordered selector stack. Where two
    // channels overlap, the later layer lerps over the earlier result; it is
    // not normalized as premultiplied coverage. A 50% red + 50% green mask
    // over white therefore resolves to 25% white, 25% Layer1, 50% Layer2.
    const json savedZaLayerDocument = document;
    document["materials"][0]["textures"][0]["file"] = "white.png";
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "white.png";
    document["materials"][0]["textures"][1]["file"] =
        "overlap-mask.ppm";
    document["materials"][0]["vec4_parameters"]["BaseColor"] =
        {1.0f, 1.0f, 1.0f, 1.0f};
    document["materials"][0]["vec4_parameters"]["BaseColorLayer1"] =
        {1.0f, 0.0f, 0.0f, 1.0f};
    document["materials"][0]["vec4_parameters"]["BaseColorLayer2"] =
        {0.0f, 1.0f, 0.0f, 1.0f};
    {
        std::ofstream output(temp.root / "overlap-mask.ppm", std::ios::binary);
        output << "P6\n1 1\n255\n";
        const unsigned char pixel[3]{128u, 128u, 0u};
        output.write(
            reinterpret_cast<const char*>(pixel),
            static_cast<std::streamsize>(sizeof(pixel)));
    }
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData zaOverlappingLayersMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), zaOverlappingLayersMesh, &outFail)) {
        return false;
    }
    if (zaOverlappingLayersMesh.submeshBaseTextures.size() != 1u ||
        !zaOverlappingLayersMesh.submeshBaseTextures[0].hasPixels() ||
        zaOverlappingLayersMesh.submeshBaseTextures[0].rgba[0] < 185u ||
        zaOverlappingLayersMesh.submeshBaseTextures[0].rgba[0] > 191u ||
        zaOverlappingLayersMesh.submeshBaseTextures[0].rgba[1] < 222u ||
        zaOverlappingLayersMesh.submeshBaseTextures[0].rgba[1] > 228u ||
        zaOverlappingLayersMesh.submeshBaseTextures[0].rgba[2] < 134u ||
        zaOverlappingLayersMesh.submeshBaseTextures[0].rgba[2] > 141u) {
        outFail =
            "Z-A IkCharacter overlapping layers did not use ordered source lerps";
        return false;
    }
    document = savedZaLayerDocument;

    // SV's SSS body family samples scalar roughness alongside its normal, AO,
    // SSSMaskMap, and SubsurfaceColor payloads. Preserve that exact transport
    // for every qualified SSS material; Eevee alone opts into Phlosion's
    // additional, explicitly reconstructed fibre response.
    {
        const json savedDocument = document;
        document["source"]["profile"] = "pokemon-scarlet-v3.0.1";
        document["model"]["name"] = "pm0133_00_00";
        document["materials"][0]["name"] = "body_a";
        document["materials"][0]["shader_family"] = "SSS";
        document["materials"][0]["shader_options"] = {
            {"EnableNormalMap", "True"},
            {"EnableRoughnessMap", "True"},
            {"EnableSSSMaskMap", "True"}};
        document["materials"][0]["float_parameters"] = json::object();
        document["materials"][0]["vec4_parameters"] = {
            {"SubsurfaceColor", {0.20f, 0.30f, 0.40f, 1.0f}}};
        document["materials"][0]["textures"] = json::array({
            {{"role", "BaseColorMap"},
             {"file", "white.png"},
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
            {{"role", "RoughnessMap"},
             {"file", "strip.ppm"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
            {{"role", "AOMap"},
             {"file", "white.png"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
            {{"role", "SSSMaskMap"},
             {"file", "white.png"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
        });
        document["materials"][0]["runtime_translation"] = {
            {"base_color_texture", "white.png"},
            {"normal_texture", "flat-normal.ppm"},
            {"roughness_texture", "strip.ppm"},
            {"metallic_texture", nullptr},
            {"occlusion_texture", "white.png"},
            {"emissive_texture", nullptr},
            {"normal_scale", 1.0f},
            {"metallic_factor", 0.0f},
            {"roughness_factor", 1.0f},
            {"occlusion_strength", 1.0f},
            {"alpha_mode", "opaque"},
            {"alpha_cutoff", 0.5f},
        };
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData svEeveeFurMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), svEeveeFurMesh, &outFail)) {
            return false;
        }
        if (svEeveeFurMesh.submeshMaterialModes.size() != 1u ||
            svEeveeFurMesh.submeshMaterialModes[0] !=
                game::runtime::render_model::kNativeSssMaterialMode ||
            svEeveeFurMesh.submeshMaterialFlags.size() != 1u ||
            !nearlyEqual(
                svEeveeFurMesh.submeshMaterialFlags[0],
                game::runtime::render_model::kNativeSssSurfaceFibre) ||
            svEeveeFurMesh.submeshMetallicRoughnessTextures.size() != 1u ||
            !svEeveeFurMesh.submeshMetallicRoughnessTextures[0].hasPixels() ||
            svEeveeFurMesh.submeshEmissiveTextures.size() != 1u ||
            !svEeveeFurMesh.submeshEmissiveTextures[0].hasPixels() ||
            svEeveeFurMesh.submeshEmissiveFactors.size() != 1u ||
            !nearlyEqual(svEeveeFurMesh.submeshEmissiveFactors[0].x, 0.20f) ||
            !nearlyEqual(svEeveeFurMesh.submeshEmissiveFactors[0].y, 0.30f) ||
            !nearlyEqual(svEeveeFurMesh.submeshEmissiveFactors[0].z, 0.40f)) {
            outFail =
                "SV Eevee SSS fur maps, mode, or subsurface color were not preserved";
            return false;
        }
        document["model"]["name"] = "pm0001_00_00";
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData svGenericSssMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), svGenericSssMesh, &outFail)) {
            return false;
        }
        if (svGenericSssMesh.submeshMaterialModes.size() != 1u ||
            svGenericSssMesh.submeshMaterialModes[0] !=
                game::runtime::render_model::kNativeSssMaterialMode ||
            svGenericSssMesh.submeshMaterialFlags.size() != 1u ||
            !nearlyEqual(
                svGenericSssMesh.submeshMaterialFlags[0],
                game::runtime::render_model::kNativeSssSurfaceDefault) ||
            svGenericSssMesh.submeshEmissiveTextures.size() != 1u ||
            !svGenericSssMesh.submeshEmissiveTextures[0].hasPixels()) {
            outFail =
                "SV non-Eevee SSS mask, mode, or neutral surface qualifier was not preserved";
            return false;
        }
        document = savedDocument;
    }

    // Z-A's IkCharacter body shader is not generic PBR. Bake its
    // layer-resolved shadow/specular, AO/metal/specular-shape, and rim response
    // into the auxiliary texture slots, then retain its material-wide
    // reflection/diffusion controls in the native parameter payload.
    document["model"]["name"] = "pm0134_00_00";
    document["source"]["profile"] = "pokemon-legends-za-v2.0.0";
    document["materials"][0]["name"] = "body";
    // Earlier contract cases intentionally replace this shared fixture's
    // layer mask. Restore the green (Layer 2) mask so this case exercises
    // the authored Layer 2 surface controls below.
    document["materials"][0]["textures"][1]["file"] = "mask.png";
    document["materials"][0]["float_parameters"]["SpecularIntensity"] =
        0.10f;
    document["materials"][0]["float_parameters"]["SpecularLayer2Intensity"] =
        0.25f;
    document["materials"][0]["float_parameters"]["SpecularLayer2Offset"] =
        0.40f;
    document["materials"][0]["float_parameters"]["SpecularLayer2Contrast"] =
        3.0f;
    document["materials"][0]["float_parameters"]["MetallicLayer2"] =
        0.65f;
    document["materials"][0]["float_parameters"]["ReflectionsBlur"] =
        2.5f;
    document["materials"][0]["float_parameters"]["DiffusionLevels"] =
        0.28f;
    document["materials"][0]["float_parameters"]["ShadowingGIGain"] =
        0.45f;
    document["materials"][0]["float_parameters"]["ShadowingBias"] =
        0.82f;
    document["materials"][0]["float_parameters"]["ShadowingShift"] =
        -0.35f;
    document["materials"][0]["float_parameters"]["ShadowingContrast"] =
        0.18f;
    document["materials"][0]["float_parameters"]["HueShiftBias"] =
        0.57f;
    document["materials"][0]["float_parameters"]["MidAreaShift"] =
        0.12f;
    document["materials"][0]["float_parameters"]["MidAreaContrast"] =
        0.21f;
    document["materials"][0]["float_parameters"]["MidAreaHueOffset"] =
        40.0f;
    document["materials"][0]["float_parameters"]["DarkAreaShift"] =
        -0.14f;
    document["materials"][0]["float_parameters"]["DarkAreaContrast"] =
        0.31f;
    document["materials"][0]["float_parameters"]["DarkAreaHueOffset"] =
        300.0f;
    document["materials"][0]["float_parameters"]["HueShiftAreaValue"] =
        0.07f;
    document["materials"][0]["float_parameters"]["OcclusionStrength"] =
        2.0f;
    document["materials"][0]["float_parameters"]["HalfLambertBias"] =
        0.20f;
    document["materials"][0]["float_parameters"]["ShadowStrength"] =
        0.70f;
    document["materials"][0]["float_parameters"]["RimLightOffset"] =
        0.30f;
    document["materials"][0]["float_parameters"]["RimLightContrast"] =
        3.0f;
    document["materials"][0]["float_parameters"]["RimLightIntensity"] =
        0.80f;
    document["materials"][0]["float_parameters"]["BackRimLightIntensity"] =
        0.04f;
    document["materials"][0]["textures"].push_back(
        {{"role", "SpecularMaskMap"},
         {"file", "strip.ppm"},
         {"wrap_s", 33071},
         {"wrap_t", 33071},
         {"min_filter", 9729},
         {"mag_filter", 9729}});
    document["materials"][0]["textures"].push_back(
        {{"role", "RimLightMaskMap"},
         {"file", "white.png"},
         {"wrap_s", 33071},
         {"wrap_t", 33071},
         {"min_filter", 9729},
         {"mag_filter", 9729}});
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData zaSpecularMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), zaSpecularMesh, &outFail)) {
        return false;
    }
    if (zaSpecularMesh.submeshMetallicRoughnessTextures.size() != 1u ||
        !zaSpecularMesh.submeshMetallicRoughnessTextures[0].hasPixels() ||
        zaSpecularMesh.submeshMetallicRoughnessTextures[0].width != 4 ||
        zaSpecularMesh.submeshMetallicRoughnessTextures[0].height != 1 ||
        zaSpecularMesh.submeshMetallicRoughnessTextures[0].rgba.size() != 16u ||
        zaSpecularMesh.submeshMetallicRoughnessTextures[0].rgba[3] != 0u ||
        zaSpecularMesh.submeshMetallicRoughnessTextures[0].rgba[7] != 10u ||
        zaSpecularMesh.submeshMetallicRoughnessTextures[0].rgba[11] != 40u ||
        zaSpecularMesh.submeshMetallicRoughnessTextures[0].rgba[15] != 60u ||
        zaSpecularMesh.submeshOcclusionTextures.size() != 1u ||
        !zaSpecularMesh.submeshOcclusionTextures[0].hasPixels() ||
        zaSpecularMesh.submeshOcclusionTextures[0].rgba[0] != 255u ||
        zaSpecularMesh.submeshOcclusionTextures[0].rgba[1] != 166u ||
        zaSpecularMesh.submeshOcclusionTextures[0].rgba[2] != 153u ||
        zaSpecularMesh.submeshOcclusionTextures[0].rgba[3] != 153u ||
        zaSpecularMesh.submeshEmissiveTextures.size() != 1u ||
        !zaSpecularMesh.submeshEmissiveTextures[0].hasPixels() ||
        zaSpecularMesh.submeshEmissiveTextures[0].rgba[0] != 51u ||
        zaSpecularMesh.submeshEmissiveTextures[0].rgba[1] != 3u ||
        zaSpecularMesh.submeshMaterialModes.size() != 1u ||
        zaSpecularMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::
                kNativeIkCharacterMaterialMode ||
        zaSpecularMesh.submeshMetallicFactor.size() != 1u ||
        !nearlyEqual(
            zaSpecularMesh.submeshMetallicFactor[0],
            0.20f) ||
        zaSpecularMesh.submeshRoughnessFactor.size() != 1u ||
        !nearlyEqual(
            zaSpecularMesh.submeshRoughnessFactor[0],
            0.70f) ||
        zaSpecularMesh.submeshOcclusionStrength.size() != 1u ||
        !nearlyEqual(
            zaSpecularMesh.submeshOcclusionStrength[0],
            2.0f) ||
        zaSpecularMesh.submeshEmissiveFactors.size() != 1u ||
        !nearlyEqual(
            zaSpecularMesh.submeshEmissiveFactors[0].x,
            0.30f) ||
        !nearlyEqual(
            zaSpecularMesh.submeshEmissiveFactors[0].y,
            3.0f) ||
        zaSpecularMesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(
            zaSpecularMesh.submeshMaterialParams0[0].x,
            2.5f) ||
        !nearlyEqual(
            zaSpecularMesh.submeshMaterialParams0[0].y,
            0.28f) ||
        !nearlyEqual(
            zaSpecularMesh.submeshMaterialParams0[0].w,
            0.45f) ||
        zaSpecularMesh.submeshMaterialParams1.size() != 1u ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams1[0].x, 0.82f) ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams1[0].y, -0.35f) ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams1[0].z, 0.18f) ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams1[0].w, 0.57f) ||
        zaSpecularMesh.submeshMaterialParams2.size() != 1u ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams2[0].x, 0.12f) ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams2[0].y, 0.21f) ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams2[0].z, 40.0f / 360.0f) ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams2[0].w, -0.14f) ||
        zaSpecularMesh.submeshMaterialParams3.size() != 1u ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams3[0].x, 0.31f) ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams3[0].y, 300.0f / 360.0f) ||
        !nearlyEqual(zaSpecularMesh.submeshMaterialParams3[0].w, 0.07f)) {
        const auto textureBytes = [](const auto& textures) {
            if (textures.empty() || !textures[0].hasPixels()) {
                return std::string("missing");
            }
            std::string value;
            const auto& rgba = textures[0].rgba;
            const std::size_t count = std::min<std::size_t>(rgba.size(), 16u);
            for (std::size_t index = 0u; index < count; ++index) {
                if (!value.empty()) value += ',';
                value += std::to_string(rgba[index]);
            }
            return value;
        };
        outFail =
            "Z-A IkCharacter layered surface controls were not preserved for the native body path; shadow=" +
            textureBytes(zaSpecularMesh.submeshMetallicRoughnessTextures) +
            " surface=" +
            textureBytes(zaSpecularMesh.submeshOcclusionTextures) +
            " params=" +
            (zaSpecularMesh.submeshMaterialParams0.empty()
                 ? std::string("missing")
                 : std::to_string(zaSpecularMesh.submeshMaterialParams0[0].x) +
                       "," +
                       std::to_string(zaSpecularMesh.submeshMaterialParams0[0].y));
        return false;
    }
    document["materials"][0]["shader_options"]["EnableEyeOptions"] =
        "True";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData zaEyeSpecularMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), zaEyeSpecularMesh, &outFail)) {
        return false;
    }
    if (zaEyeSpecularMesh.submeshMaterialFlags.size() != 1u ||
        nearlyEqual(
            zaEyeSpecularMesh.submeshMaterialFlags[0],
            game::runtime::render_model::
                kNativeSpecularStrengthMaterialFlag)) {
        outFail =
            "Z-A EyeOptions material incorrectly opted into the ordinary body specular path";
        return false;
    }
    document["materials"][0]["shader_options"].erase(
        "EnableEyeOptions");

    // Mode selection follows the Z-A source profile, not a short species
    // allowlist. A non-Eeveelution body using the same native contract must
    // retain the same lighting payload.
    document["model"]["name"] = "pm0016_00_00";
    constexpr std::string_view kPidgeyBase =
        "pm0016_00_00_body_a_alb_BaseColorMap_33fb59404718.png";
    {
        std::ofstream output(temp.root / kPidgeyBase, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(whitePng.data()),
            static_cast<std::streamsize>(whitePng.size()));
    }
    document["materials"][0]["textures"][0]["file"] = kPidgeyBase;
    document["materials"][0]["runtime_translation"]
            ["base_color_texture"] = kPidgeyBase;
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData zaPidgeyBodyMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), zaPidgeyBodyMesh, &outFail)) {
        return false;
    }
    if (zaPidgeyBodyMesh.submeshMaterialModes.size() != 1u ||
        zaPidgeyBodyMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::
                kNativeIkCharacterMaterialMode ||
        zaPidgeyBodyMesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(
            zaPidgeyBodyMesh.submeshMaterialParams0[0].z,
            game::runtime::render_model::
                kNativeIkCharacterSurfaceFeather)) {
        outFail =
            "Z-A IkCharacter body or exact feather-atlas qualification was not preserved";
        return false;
    }

    // A compatible SV fibre atlas is an explicit opt-in keyed by an exact
    // Z-A base-atlas identity. Pack its green channel into the native rim
    // payload alpha; a non-mapped body above must remain neutral instead.
    {
        const json savedDocument = document;
        constexpr std::string_view kJolteonBase =
            "pm0135_00_00_body_a_alb_BaseColorMap_ba05f885ec40.png";
        constexpr std::string_view kJolteonFibre =
            "pm0135_00_00_body_a_rgn_RoughnessMap_8aa2e91a967a.png";
        const fs::path basePath = temp.root / kJolteonBase;
        const fs::path fibrePath =
            temp.root / "za_sv_surface_maps" / kJolteonFibre;
        fs::create_directories(fibrePath.parent_path(), error);
        if (error) {
            outFail = "could not create supplemental Z-A fibre test directory";
            return false;
        }
        {
            std::ofstream output(basePath, std::ios::binary);
            output.write(
                reinterpret_cast<const char*>(whitePng.data()),
                static_cast<std::streamsize>(whitePng.size()));
        }
        {
            std::ofstream output(fibrePath, std::ios::binary);
            output.write(
                reinterpret_cast<const char*>(grayscaleStripPpm.data()),
                static_cast<std::streamsize>(grayscaleStripPpm.size()));
        }
        document["model"]["name"] = "pm0135_00_00";
        document["materials"][0]["textures"][0]["file"] = kJolteonBase;
        document["materials"][0]["runtime_translation"]
                ["base_color_texture"] = kJolteonBase;
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData zaJolteonFibreMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), zaJolteonFibreMesh, &outFail)) {
            return false;
        }
        if (zaJolteonFibreMesh.submeshEmissiveTextures.size() != 1u ||
            !zaJolteonFibreMesh.submeshEmissiveTextures[0].hasPixels() ||
            zaJolteonFibreMesh.submeshEmissiveTextures[0].width != 4 ||
            zaJolteonFibreMesh.submeshEmissiveTextures[0].rgba.size() != 16u ||
            zaJolteonFibreMesh.submeshEmissiveTextures[0].rgba[3] != 0u ||
            zaJolteonFibreMesh.submeshEmissiveTextures[0].rgba[7] != 40u ||
            zaJolteonFibreMesh.submeshEmissiveTextures[0].rgba[11] != 160u ||
            zaJolteonFibreMesh.submeshEmissiveTextures[0].rgba[15] != 240u ||
            zaJolteonFibreMesh.submeshMaterialParams0.size() != 1u ||
            !nearlyEqual(
                zaJolteonFibreMesh.submeshMaterialParams0[0].z,
                game::runtime::render_model::
                    kNativeIkCharacterSurfaceFibre)) {
            outFail =
                "Z-A compatible fibre evidence was not isolated in the native rim alpha lane";
            return false;
        }
        document = savedDocument;
    }

    document["materials"][0]["textures"].erase(
        document["materials"][0]["textures"].end() - 2,
        document["materials"][0]["textures"].end());
    document["materials"][0]["float_parameters"].erase(
        "SpecularIntensity");
    document["materials"][0]["name"] = "r_eye";
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

    // Kangaskhan's child eye is an unusual zero-mask Z-A EyeOptions material:
    // its white 2x2 albedo is tinted by BaseColorLayer1, while a separate mask
    // supplies the white catchlight. Preserve both only for the exact child-eye
    // source instead of turning every zero-mask IkCharacter material dark.
    {
        const json savedDocument = document;
        document["materials"][0]["name"] = "r_eye_b";
        document["materials"][0]["shader_family"] = "IkCharacter";
        document["materials"][0]["shader_options"] = {
            {"EnableEyeOptions", "True"},
            {"EnableHighlight", "True"},
            {"BaseColorMultiply", "True"},
        };
        document["materials"][0]["float_parameters"] = {
            {"EmissionIntensityLayer5", 1.0f},
        };
        document["materials"][0]["vec4_parameters"] = {
            {"BaseColorLayer1", {0.01938217f, 0.01938217f, 0.01938217f, 1.0f}},
            {"EmissionColorLayer5", {1.0f, 1.0f, 1.0f, 1.0f}},
        };
        document["materials"][0]["textures"] = json::array({
            {{"role", "BaseColorMap"},
             {"source", "pm0115_00_00_eye_b_alb.bntx"},
             {"file", "white.png"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
            {{"role", "LayerMaskMap"},
             {"source", "pm0115_00_00_eye_b_lym.bntx"},
             {"file", "black-strip.ppm"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
            {{"role", "HighlightMaskMap"},
             {"source", "pm0115_00_00_eye_b_msk.bntx"},
             {"file", "white.png"},
             {"wrap_s", 33071},
             {"wrap_t", 33071},
             {"min_filter", 9729},
             {"mag_filter", 9729}},
        });
        document["materials"][0]["runtime_translation"] = {
            {"base_color_texture", "white.png"},
            {"normal_texture", nullptr},
            {"roughness_texture", nullptr},
            {"metallic_texture", nullptr},
            {"occlusion_texture", nullptr},
            {"emissive_texture", nullptr},
            {"normal_scale", 1.0f},
            {"metallic_factor", 0.0f},
            {"roughness_factor", 0.5f},
            {"occlusion_strength", 1.0f},
            {"alpha_mode", "opaque"},
            {"alpha_cutoff", 0.5f},
        };
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData kangaskhanBabyEyeMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), kangaskhanBabyEyeMesh, &outFail)) {
            return false;
        }
        document["materials"][0]["textures"][0]["source"] =
            "generic_eye_b_alb.bntx";
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData genericZaEyeMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), genericZaEyeMesh, &outFail)) {
            return false;
        }
        document["materials"][0]["shader_options"]["EnableEyeOptions"] =
            "False";
        {
            std::ofstream output(manifestPath);
            output << document.dump(2);
        }
        game::runtime::render_model::MeshData genericZaNonEyeMesh;
        if (!tools::phlosion_native_model_ir::load(
                manifestPath.string(), genericZaNonEyeMesh, &outFail)) {
            return false;
        }
        const auto& babyBase =
            kangaskhanBabyEyeMesh.submeshBaseTextures[0].rgba;
        if (babyBase.empty() || babyBase[0] < 35u || babyBase[0] > 45u ||
            kangaskhanBabyEyeMesh.submeshEmissiveTextures.size() != 1u ||
            !kangaskhanBabyEyeMesh.submeshEmissiveTextures[0].hasPixels() ||
            kangaskhanBabyEyeMesh.submeshEmissiveTextures[0].rgba[0] < 250u ||
            kangaskhanBabyEyeMesh.submeshEmissiveFactors.size() != 1u ||
            !nearlyEqual(
                kangaskhanBabyEyeMesh.submeshEmissiveFactors[0].x,
                1.0f) ||
            genericZaEyeMesh.submeshBaseTextures[0].rgba[0] < 250u ||
            genericZaEyeMesh.submeshEmissiveTextures.size() != 1u ||
            !genericZaEyeMesh.submeshEmissiveTextures[0].hasPixels() ||
            genericZaEyeMesh.submeshEmissiveTextures[0].rgba[0] < 250u ||
            genericZaNonEyeMesh.submeshEmissiveTextures[0].hasPixels()) {
            outFail =
                "Z-A EyeOptions highlight bake lost its glint, Kangaskhan tint, or material qualification";
            return false;
        }
        document = savedDocument;
    }

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

    // The PLA Magnemite-family eye vertices already address the neutral atlas
    // tile. Their native Eye materials still report
    // UVScaleOffset=(2,4,0,0), whose x/y describe the atlas layout rather
    // than a second mesh-UV transform. A generic Eye source remains
    // transformed, proving this is source-qualified rather than a
    // shader-family-wide exception.
    document["materials"][0]["shader_family"] = "Eye";
    document["materials"][0]["shader_options"] = json::object();
    document["materials"][0]["textures"][0]["source"] =
        "pm0081_00_00_eye_alb.bntx";
    document["materials"][0]["textures"][0]["file"] =
        "white-strip.ppm";
    document["materials"][0]["textures"][0]["wrap_s"] = 33071;
    document["materials"][0]["textures"][1]["file"] = "strip.ppm";
    document["materials"][0]["textures"][1]["wrap_s"] = 33071;
    document["materials"][0]["runtime_translation"]
            ["base_color_texture"] = "white-strip.ppm";
    document["materials"][0]["runtime_translation"]
            ["occlusion_texture"] = nullptr;
    document["materials"][0]["vec4_parameters"] = {
        {"UVScaleOffset", {2.0f, 4.0f, 0.0f, 0.0f}},
        {"BaseColorLayer1", {1.0f, 0.0f, 0.0f, 1.0f}},
    };
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData magnemiteEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), magnemiteEyeMesh, &outFail)) {
        return false;
    }
    document["materials"][0]["textures"][0]["source"] =
        "generic_eye_alb.bntx";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData genericEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), genericEyeMesh, &outFail)) {
        return false;
    }
    document["materials"][0]["textures"][0]["source"] =
        "pm0082_00_00_eye_alb.bntx";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData magnetonEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), magnetonEyeMesh, &outFail)) {
        return false;
    }
    const auto& magnemitePixels =
        magnemiteEyeMesh.submeshBaseTextures[0].rgba;
    const auto& magnetonPixels =
        magnetonEyeMesh.submeshBaseTextures[0].rgba;
    const auto& genericPixels =
        genericEyeMesh.submeshBaseTextures[0].rgba;
    std::vector<std::uint8_t> blackEyeCarrier(magnemitePixels.size(), 0u);
    for (std::size_t pixel = 3u;
         pixel < blackEyeCarrier.size();
         pixel += 4u) {
        blackEyeCarrier[pixel] = 255u;
    }
    if (magnemitePixels.size() < 16u || genericPixels.size() < 16u ||
        magnemitePixels[1u] < 250u ||
        magnemitePixels[5u] <= magnemitePixels[9u] ||
        magnemitePixels[9u] <= magnemitePixels[13u] ||
        genericPixels[9u] != genericPixels[13u] ||
        magnetonPixels != magnemitePixels ||
        magnemitePixels == genericPixels) {
        outFail =
            "PLA Magnemite-family eye atlas was transformed into an unused tile";
        return false;
    }

    // A clip-bound Eye UV track must keep the complete source atlas in the
    // cooked textures and remain parallel to its body clip. Applying the
    // default transform during baking would make every later eye expression
    // sample the already-collapsed neutral tile.
    document["materials"][0]["textures"][0]["source"] =
        "pm0081_00_00_eye_alb.bntx";
    document["materials"][0]["name"] = "l_eye";
    document["materials"][0]["vec4_parameters"]["UVScaleOffset"] =
        {2.0f, 4.0f, 0.0f, 0.0f};
    document["animations"] = json::array({
        {{"name", "pm0081_00_00_00010_defaultidle01"},
         {"duration_seconds", 1.0f},
         {"frame_rate", 60},
         {"loop", true},
         {"tracks", json::array()},
         {"mesh_visibility", json::array()},
         {"material_parameters", json::array({
             {{"mesh", "Triangle"},
              {"material", "l_eye"},
              {"parameter", "UVScaleOffset"},
              {"x", json::array({
                  {{"frame", 0.0f}, {"value", 2.0f}},
                  {{"frame", 60.0f}, {"value", 2.0f}},
              })},
              {"y", json::array({
                  {{"frame", 0.0f}, {"value", 4.0f}},
                  {{"frame", 60.0f}, {"value", 4.0f}},
              })},
              {"z", json::array({
                  {{"frame", 0.0f}, {"value", 0.0f}},
                  {{"frame", 30.0f}, {"value", 0.5f}},
                  {{"frame", 60.0f}, {"value", 0.0f}},
              })},
              {"w", json::array({
                  {{"frame", 0.0f}, {"value", 0.0f}},
                  {{"frame", 30.0f}, {"value", 0.25f}},
                  {{"frame", 60.0f}, {"value", 0.0f}},
              })}},
         })}},
    });
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData animatedEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), animatedEyeMesh, &outFail)) {
        return false;
    }
    if (animatedEyeMesh.submeshMaterialModes.size() != 1u ||
        animatedEyeMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::
                kNativeAnimatedEyeMaterialMode ||
        animatedEyeMesh.submeshNormalScale.size() != 1u ||
        !nearlyEqual(animatedEyeMesh.submeshNormalScale[0], 0.0f) ||
        animatedEyeMesh.submeshBaseTextures.size() != 1u ||
        animatedEyeMesh.submeshBaseTextures[0].width !=
            magnemiteEyeMesh.submeshBaseTextures[0].width ||
        animatedEyeMesh.submeshBaseTextures[0].height !=
            magnemiteEyeMesh.submeshBaseTextures[0].height ||
        animatedEyeMesh.submeshBaseTextures[0].rgba != blackEyeCarrier ||
        animatedEyeMesh.submeshEmissiveTextures.size() != 1u ||
        animatedEyeMesh.submeshEmissiveTextures[0].rgba != magnemitePixels ||
        animatedEyeMesh.submeshMetallicRoughnessTextures.size() != 1u ||
        animatedEyeMesh.submeshMetallicRoughnessTextures[0].rgba !=
            std::vector<std::uint8_t>({255u, 255u, 255u, 255u}) ||
        animatedEyeMesh.submeshMaterialFlags.size() != 1u ||
        !nearlyEqual(animatedEyeMesh.submeshMaterialFlags[0], 0.0f) ||
        animatedEyeMesh.submeshMetallicFactor.size() != 1u ||
        !nearlyEqual(animatedEyeMesh.submeshMetallicFactor[0], 1.0f) ||
        animatedEyeMesh.submeshRoughnessFactor.size() != 1u ||
        !nearlyEqual(animatedEyeMesh.submeshRoughnessFactor[0], 1.0f) ||
        animatedEyeMesh.submeshEmissiveFactors.size() != 1u ||
        !nearlyEqual(
            animatedEyeMesh.submeshEmissiveFactors[0].x,
            1.0f) ||
        animatedEyeMesh.submeshMaterialParams2.size() != 1u ||
        !nearlyEqual(animatedEyeMesh.submeshMaterialParams2[0].x, 1.0f) ||
        !nearlyEqual(animatedEyeMesh.submeshMaterialParams2[0].y, 1.0f) ||
        animatedEyeMesh.animationMaterialParameters.size() != 1u ||
        animatedEyeMesh.animationMaterialParameters[0].size() != 1u ||
        animatedEyeMesh.animationMaterialParameters[0][0].sampling !=
            game::runtime::render_model::
                MaterialAnimationSampling::HoldSourceFrame ||
        animatedEyeMesh.animationMaterialParameters[0][0]
                .components[2]
                .keys.size() != 3u ||
        !nearlyEqual(
            animatedEyeMesh.animationMaterialParameters[0][0]
                .components[0]
                .keys[0]
                .value,
            1.0f) ||
        !nearlyEqual(
            animatedEyeMesh.animationMaterialParameters[0][0]
                .components[1]
                .keys[0]
                .value,
            1.0f) ||
        !nearlyEqual(
            animatedEyeMesh.animationMaterialParameters[0][0]
                .components[2]
                .keys[1]
                .value,
            0.5f) ||
        !animatedEyeMesh.continuousMaterialAnimations.empty()) {
        outFail =
            "clip-bound eye animation was collapsed into its neutral atlas tile or continuous material clock";
        return false;
    }

    // Tangela's eye uses the same flat animated-atlas transport without the
    // Magnemite family's already-addressed (2,4) UV-scale exception. Qualify
    // it by its exact source texture so another PLA Eye remains on the normal
    // clear-coat path.
    document["materials"][0]["textures"][0]["source"] =
        "pm0114_00_00_eye_alb.bntx";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData tangelaEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), tangelaEyeMesh, &outFail)) {
        return false;
    }
    if (tangelaEyeMesh.submeshMaterialModes.size() != 1u ||
        tangelaEyeMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::kNativeAnimatedEyeMaterialMode ||
        tangelaEyeMesh.submeshNormalScale.size() != 1u ||
        !nearlyEqual(tangelaEyeMesh.submeshNormalScale[0], 0.0f) ||
        tangelaEyeMesh.submeshBaseTextures.size() != 1u ||
        tangelaEyeMesh.submeshBaseTextures[0].rgba != blackEyeCarrier ||
        tangelaEyeMesh.submeshEmissiveTextures.size() != 1u ||
        tangelaEyeMesh.submeshEmissiveTextures[0].rgba != magnemitePixels) {
        outFail =
            "Tangela eye atlas was relit as a projected PBR normal sphere";
        return false;
    }
    document["materials"][0]["textures"][0]["source"] =
        "pm0081_00_00_eye_alb.bntx";

    // Eye materials also carry genuine smooth gaze curves. Values that only
    // happen to lie between atlas-sized endpoints must keep linear sampling;
    // otherwise broad rational snapping makes those eyes stutter.
    document["animations"][0]["material_parameters"][0]["z"][1]["value"] =
        -0.09941497f;
    document["animations"][0]["material_parameters"][0]["w"][1]["value"] =
        -0.08228606f;
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData smoothAnimatedEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), smoothAnimatedEyeMesh, &outFail)) {
        return false;
    }
    if (smoothAnimatedEyeMesh.animationMaterialParameters.size() != 1u ||
        smoothAnimatedEyeMesh.animationMaterialParameters[0].size() != 1u ||
        smoothAnimatedEyeMesh.animationMaterialParameters[0][0].sampling !=
            game::runtime::render_model::MaterialAnimationSampling::Linear) {
        outFail =
            "smooth eye gaze curve was mistaken for a discrete atlas selector";
        return false;
    }

    // Some authored atlases store exact twelfth-cell offsets rounded to three
    // decimal places (for example Dodrio EyeB). Preserve those cells without
    // widening the tolerance used for arbitrary smooth gaze curves.
    document["animations"][0]["material_parameters"][0]["z"][1]["value"] =
        0.083f;
    document["animations"][0]["material_parameters"][0]["w"][1]["value"] =
        0.583f;
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData roundedAtlasEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), roundedAtlasEyeMesh, &outFail)) {
        return false;
    }
    if (roundedAtlasEyeMesh.animationMaterialParameters.size() != 1u ||
        roundedAtlasEyeMesh.animationMaterialParameters[0].size() != 1u ||
        roundedAtlasEyeMesh.animationMaterialParameters[0][0].sampling !=
            game::runtime::render_model::
                MaterialAnimationSampling::HoldSourceFrame) {
        outFail =
            "rounded discrete eye-atlas cell lost held source-frame sampling";
        return false;
    }

    document["animations"][0]["material_parameters"][0]["z"][1]["value"] =
        0.5f;
    document["animations"][0]["material_parameters"][0]["w"][1]["value"] =
        0.25f;

    // Trinity sometimes binds the same authored eye atlas through the
    // numbered UVScaleOffset1 channel (Pikachu, Diglett, Weedle, Bellsprout).
    // Use it when no unnumbered eye channel exists, but do not mistake the
    // separate normal-map transform for an eye-expression channel.
    document["animations"][0]["material_parameters"][0]
            ["parameter"] = "UVScaleOffset1";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData numberedAnimatedEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), numberedAnimatedEyeMesh, &outFail)) {
        return false;
    }
    if (numberedAnimatedEyeMesh.submeshMaterialModes.size() != 1u ||
        numberedAnimatedEyeMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::
                kNativeAnimatedEyeMaterialMode ||
        numberedAnimatedEyeMesh.animationMaterialParameters.size() != 1u ||
        numberedAnimatedEyeMesh.animationMaterialParameters[0].size() != 1u ||
        !nearlyEqual(
            numberedAnimatedEyeMesh.animationMaterialParameters[0][0]
                .components[2]
                .keys[1]
                .value,
            0.5f) ||
        !nearlyEqual(
            numberedAnimatedEyeMesh.submeshMaterialParams2[0].x,
            1.0f)) {
        outFail =
            "numbered Trinity eye UV channel was not promoted to the primary eye atlas transform";
        return false;
    }
    document["animations"][0]["material_parameters"][0]
            ["parameter"] = "UVScaleOffsetNormal";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData normalOnlyEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), normalOnlyEyeMesh, &outFail)) {
        return false;
    }
    if (normalOnlyEyeMesh.submeshMaterialModes.size() != 1u ||
        normalOnlyEyeMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::kNativeEyeClearCoatMaterialMode ||
        !normalOnlyEyeMesh.animationMaterialParameters.empty() &&
            !normalOnlyEyeMesh.animationMaterialParameters[0].empty()) {
        outFail =
            "normal-map UV animation was mistaken for an eye-expression channel";
        return false;
    }
    document["animations"][0]["material_parameters"][0]
            ["parameter"] = "UVScaleOffset";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::phlosion::ModelCookStats animatedEyeCookStats;
    if (!game::runtime::phlosion::cookModelObject(
            manifestPath.string(),
            animatedEyeMesh,
            cookedRoot.string(),
            "Character",
            animatedEyeCookStats,
            &outFail)) {
        return false;
    }
    game::runtime::render_model::MeshData reloadedAnimatedEyeMesh;
    if (!game::runtime::phlosion::loadModelObject(
            game::runtime::phlosion::objectPathForModel(
                manifestPath.string(),
                cookedRoot.string()),
        reloadedAnimatedEyeMesh,
        &outFail) ||
        reloadedAnimatedEyeMesh.submeshBaseTextures.size() != 1u ||
        reloadedAnimatedEyeMesh.submeshBaseTextures[0].rgba !=
            blackEyeCarrier ||
        reloadedAnimatedEyeMesh.submeshEmissiveTextures.size() != 1u ||
        reloadedAnimatedEyeMesh.submeshEmissiveTextures[0].rgba !=
            magnemitePixels ||
        reloadedAnimatedEyeMesh.submeshMetallicRoughnessTextures.size() != 1u ||
        reloadedAnimatedEyeMesh.submeshMetallicRoughnessTextures[0].rgba !=
            std::vector<std::uint8_t>({255u, 255u, 255u, 255u}) ||
        reloadedAnimatedEyeMesh.submeshMaterialFlags.size() != 1u ||
        !nearlyEqual(reloadedAnimatedEyeMesh.submeshMaterialFlags[0], 0.0f) ||
        reloadedAnimatedEyeMesh.submeshEmissiveFactors.size() != 1u ||
        !nearlyEqual(
            reloadedAnimatedEyeMesh.submeshEmissiveFactors[0].x,
            1.0f) ||
        reloadedAnimatedEyeMesh.animationMaterialParameters.size() != 1u ||
        reloadedAnimatedEyeMesh.animationMaterialParameters[0].size() != 1u ||
        reloadedAnimatedEyeMesh.animationMaterialParameters[0][0].sampling !=
            game::runtime::render_model::
                MaterialAnimationSampling::HoldSourceFrame ||
        !nearlyEqual(
            reloadedAnimatedEyeMesh.animationMaterialParameters[0][0]
                .components[3]
                .keys[1]
                .value,
            0.25f)) {
        if (outFail.empty()) {
            outFail =
                "PHLO round trip discarded clip-bound eye textures, emission, or material animation";
        }
        return false;
    }

    // Gastly's native body and eye shells are composited ahead of its opaque
    // displaced smoke volume. Preserve their projected positions and carry
    // only the source-qualified depth ordering into the runtime material.
    document["animations"] = json::array();
    document["materials"][0]["shader_family"] = "IkCharacter";
    document["materials"][0]["name"] = "body";
    document["materials"][0]["textures"][0]["source"] =
        "pm0092_00_00_body_alb.bntx";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData gastlyFaceMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), gastlyFaceMesh, &outFail)) {
        return false;
    }
    if (gastlyFaceMesh.submeshMaterialModes.size() != 1u ||
        gastlyFaceMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::
                kNativeFacialOverlayMaterialMode ||
        gastlyFaceMesh.submeshMaterialFlags.size() != 1u ||
        !nearlyEqual(gastlyFaceMesh.submeshMaterialFlags[0], 4.0f) ||
        gastlyFaceMesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(
            gastlyFaceMesh.submeshMaterialParams0[0].x,
            0.020f) ||
        gastlyFaceMesh.vertices.size() != 3u ||
        !nearlyEqual(gastlyFaceMesh.vertices[1].position.x, 1.0f) ||
        !nearlyEqual(gastlyFaceMesh.vertices[1].position.z, 0.0f)) {
        outFail =
            "Gastly face overlay did not preserve geometry and source depth ordering";
        return false;
    }

    document["materials"][0]["name"] = "l_eye";
    document["materials"][0]["textures"][0]["source"] =
        "pm0092_00_00_eye_alb.bntx";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData gastlyEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), gastlyEyeMesh, &outFail)) {
        return false;
    }
    if (gastlyEyeMesh.submeshMaterialModes.size() != 1u ||
        gastlyEyeMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::
                kNativeFacialOverlayMaterialMode ||
        gastlyEyeMesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(
            gastlyEyeMesh.submeshMaterialParams0[0].x,
            0.022f)) {
        outFail =
            "Gastly eye overlay did not retain priority over the face shell";
        return false;
    }

    // Scarlet/Violet uses its ordinary Standard and EyeClearCoat materials
    // for the same nested shells. The portable payload must carry only their
    // source draw priority in the spare params3 lane; selecting Z-A's facial
    // material mode here would reintroduce its source-specific tongue bake.
    document["materials"][0]["shader_family"] = "Standard";
    document["materials"][0]["name"] = "body";
    document["materials"][0]["textures"][0]["source"] =
        "pm0092_00_00_body_alb.bntx";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData scarletGastlyFaceMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), scarletGastlyFaceMesh, &outFail)) {
        return false;
    }
    if (scarletGastlyFaceMesh.submeshMaterialModes.size() != 1u ||
        scarletGastlyFaceMesh.submeshMaterialModes[0] != 2u ||
        scarletGastlyFaceMesh.submeshMaterialParams3.size() != 1u ||
        !nearlyEqual(
            scarletGastlyFaceMesh.submeshMaterialParams3[0].x,
            0.020f)) {
        outFail =
            "Scarlet Gastly face lost ordinary shading or source depth ordering";
        return false;
    }

    document["materials"][0]["shader_family"] = "EyeClearCoat";
    document["materials"][0]["name"] = "l_eye";
    document["materials"][0]["textures"][0]["source"] =
        "pm0092_00_00_eye_alb.bntx";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData scarletGastlyEyeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), scarletGastlyEyeMesh, &outFail)) {
        return false;
    }
    if (scarletGastlyEyeMesh.submeshMaterialModes.size() != 1u ||
        scarletGastlyEyeMesh.submeshMaterialModes[0] != 2u ||
        scarletGastlyEyeMesh.submeshMaterialParams3.size() != 1u ||
        !nearlyEqual(
            scarletGastlyEyeMesh.submeshMaterialParams3[0].x,
            0.022f)) {
        outFail =
            "Scarlet Gastly eye lost ordinary shading or source depth ordering";
        return false;
    }

    document["materials"][0]["textures"][0].erase("source");
    document["materials"][0]["name"] = "test_material";
    document["materials"][0]["textures"][0]["file"] = "white.png";
    document["materials"][0]["textures"][0]["wrap_s"] = 33071;
    document["materials"][0]["textures"][1]["file"] = "mask.png";
    document["materials"][0]["textures"][1]["wrap_s"] = 33071;
    document["materials"][0]["runtime_translation"]["base_color_texture"] =
        "white.png";
    document["materials"][0]["runtime_translation"]["occlusion_texture"] =
        nullptr;
    document["materials"][0]["vec4_parameters"].erase(
        "BaseColorLayer1");
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
    if (unlitMesh.continuousMaterialAnimations.size() != 2u ||
        std::any_of(
            unlitMesh.animations.begin(),
            unlitMesh.animations.end(),
            [](const auto& clip) {
                return clip.name.find("_08201_loop01_loop") !=
                    std::string::npos;
            })) {
        outFail =
            "native continuous material tracks were collapsed, discarded, or exposed as a body clip";
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

    // Gastly's IkCharacter smoke opts into the same authored layer-mask and
    // displacement contract even though it is not labelled Unlit. Qualify it
    // by both native texture identities so another displaced IkCharacter
    // surface cannot silently inherit the specialized smoke path.
    document["animations"] = json::array();
    document["materials"][0]["name"] = "smoke";
    document["materials"][0]["shader_family"] = "IkCharacter";
    document["materials"][0]["textures"][0]["source"] =
        "pm0092_00_00_smoke_alb.bntx";
    document["materials"][0]["textures"].back()["source"] =
        "pm0092_00_00_smoke_msk.bntx";
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData gastlySmokeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), gastlySmokeMesh, &outFail)) {
        return false;
    }
    if (gastlySmokeMesh.submeshMaterialModes.size() != 1u ||
        gastlySmokeMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::
                kNativeLayeredUnlitMaterialMode ||
        gastlySmokeMesh.submeshMaterialParams0.size() != 1u ||
        !nearlyEqual(
            gastlySmokeMesh.submeshMaterialParams0[0].x,
            0.05f) ||
        !nearlyEqual(
            gastlySmokeMesh.submeshMaterialParams0[0].y,
            1.0f) ||
        gastlySmokeMesh.submeshMaterialFlags.size() != 1u ||
        !nearlyEqual(gastlySmokeMesh.submeshMaterialFlags[0], 3.0f) ||
        gastlySmokeMesh.submeshNormalTextures.size() != 1u ||
        !gastlySmokeMesh.submeshNormalTextures[0].hasPixels() ||
        gastlySmokeMesh.submeshMetallicRoughnessTextures.size() != 1u ||
        gastlySmokeMesh.submeshMetallicRoughnessTextures[0].rgba !=
            std::vector<std::uint8_t>({0u, 0u, 0u, 255u})) {
        outFail =
            "Gastly smoke lost its source-qualified displacement path";
        return false;
    }

    // Z-A's 28201 controller is an always-running material layer, equivalent
    // to Scarlet's 08201 controller. Gastly's smoke UV and displacement UV
    // tracks must therefore keep running while any ordinary body clip plays.
    document["animations"] = json::array({
        {{"name", "pm0092_00_00_28201_loop01_loop"},
         {"duration_seconds", 2.0f},
         {"frame_rate", 60},
         {"loop", true},
         {"tracks", json::array()},
         {"mesh_visibility", json::array()},
         {"material_parameters", json::array({
             {{"mesh", "Triangle"},
              {"material", "smoke"},
              {"parameter", "UVScaleOffset"},
              {"x", json::array()},
              {"y", json::array()},
              {"z", json::array({
                  {{"frame", 0.0f}, {"value", 0.0f}},
                  {{"frame", 120.0f}, {"value", 1.0f}},
              })},
              {"w", json::array({
                  {{"frame", 0.0f}, {"value", 0.0f}},
                  {{"frame", 120.0f}, {"value", 1.0f}},
              })}},
             {{"mesh", "Triangle"},
              {"material", "smoke"},
              {"parameter", "UVScaleOffset3"},
              {"x", json::array()},
              {"y", json::array()},
              {"z", json::array({
                  {{"frame", 0.0f}, {"value", 0.0f}},
                  {{"frame", 120.0f}, {"value", 1.0f}},
              })},
              {"w", json::array({
                  {{"frame", 0.0f}, {"value", 0.0f}},
                  {{"frame", 120.0f}, {"value", 1.0f}},
              })}},
         })}},
    });
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData animatedGastlySmokeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), animatedGastlySmokeMesh, &outFail)) {
        return false;
    }
    if (animatedGastlySmokeMesh.submeshMaterialFlags.size() != 1u ||
        !nearlyEqual(
            animatedGastlySmokeMesh.submeshMaterialFlags[0],
            3.0f) ||
        animatedGastlySmokeMesh.continuousMaterialAnimations.size() != 2u ||
        std::any_of(
            animatedGastlySmokeMesh.animations.begin(),
            animatedGastlySmokeMesh.animations.end(),
            [](const auto& clip) {
                return clip.name.find("_28201_loop01_loop") !=
                    std::string::npos;
            }) ||
        animatedGastlySmokeMesh.continuousMaterialAnimations[0]
                .components[2]
                .keys.size() != 2u ||
        !nearlyEqual(
            animatedGastlySmokeMesh.continuousMaterialAnimations[0]
                .components[2]
                .keys[1]
                .timeSec,
            2.0f)) {
        outFail =
            "Z-A 28201 continuous smoke controller was frozen or discarded";
        return false;
    }

    // A loop01 controller stops being material-only when it also authors an
    // intermittent geometry lifecycle. SV Weezing uses exactly this contract
    // for its repeating idle smoke, so the controller clip and its visibility
    // keys must survive cooking for the runtime overlay path.
    document["animations"][0]["mesh_visibility"] = json::array({
        {{"mesh", "Triangle"},
         {"key_frames", json::array({0, 10, 41})},
         {"values", json::array({false, true, false})}},
    });
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData lifecycleControllerMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), lifecycleControllerMesh, &outFail)) {
        return false;
    }
    if (lifecycleControllerMesh.animations.size() != 1u ||
        lifecycleControllerMesh.animations[0].name.find(
            "_28201_loop01_loop") == std::string::npos ||
        lifecycleControllerMesh.animationMeshVisibility.size() != 1u ||
        lifecycleControllerMesh.animationMeshVisibility[0].size() != 1u ||
        lifecycleControllerMesh.animationMeshVisibility[0][0].values !=
            std::vector<std::uint8_t>({0u, 1u, 0u})) {
        outFail =
            "A continuous controller with an authored visibility lifecycle "
            "was discarded as material-only";
        return false;
    }
    document["animations"][0]["mesh_visibility"] = json::array();

    // SV Koffing's one-second 28201 controller contains the complete paired
    // side-cloud skeletal/material cycle, but its TRACM leaves all smoke
    // meshes fixed hidden. The importer restores the missing family-standard
    // visibility gates so that the controller can actually puff during idle.
    // Keep this exact-name correction narrow: unrelated all-hidden continuous
    // controllers must remain material-only.
    json koffingIdleDocument = document;
    koffingIdleDocument["animations"][0]["name"] =
        "pm0109_00_00_28201_loop01_loop";
    koffingIdleDocument["animations"][0]["duration_seconds"] = 1.0f;
    koffingIdleDocument["animations"][0]["frame_count"] = 61;
    koffingIdleDocument["animations"][0]["mesh_visibility"] =
        json::array({
            {{"mesh", "pm0109_00_00_smokegeom_b1_mesh_shape"},
             {"key_frames", json::array({0})},
             {"values", json::array({false})}},
            {{"mesh", "pm0109_00_00_smokemask_b1_mesh_shape"},
             {"key_frames", json::array({0})},
             {"values", json::array({false})}},
            {{"mesh", "pm0109_00_00_smokegeom_b2_mesh_shape"},
             {"key_frames", json::array({0})},
             {"values", json::array({false})}},
            {{"mesh", "pm0109_00_00_smokemask_b2_mesh_shape"},
             {"key_frames", json::array({0})},
             {"values", json::array({false})}},
        });
    const json sourceKoffingSubmesh =
        koffingIdleDocument["model"]["submeshes"][0];
    koffingIdleDocument["model"]["submeshes"] = json::array();
    for (const std::string& name : {
             "pm0109_00_00_smokegeom_b1_mesh_shape:smoke",
             "pm0109_00_00_smokemask_b1_mesh_shape:smoke",
             "pm0109_00_00_smokegeom_b2_mesh_shape:smoke",
             "pm0109_00_00_smokemask_b2_mesh_shape:smoke"}) {
        json submesh = sourceKoffingSubmesh;
        submesh["name"] = name;
        koffingIdleDocument["model"]["submeshes"].push_back(
            std::move(submesh));
    }
    koffingIdleDocument["model"]["submesh_count"] = 4;
    {
        std::ofstream output(manifestPath);
        output << koffingIdleDocument.dump(2);
    }
    game::runtime::render_model::MeshData koffingIdleControllerMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(),
            koffingIdleControllerMesh,
            &outFail)) {
        return false;
    }
    const auto& koffingVisibility =
        koffingIdleControllerMesh.animationMeshVisibility;
    if (koffingIdleControllerMesh.animations.size() != 1u ||
        koffingVisibility.size() != 1u ||
        koffingVisibility[0].size() != 4u ||
        koffingVisibility[0][0].values !=
            std::vector<std::uint8_t>({0u, 1u, 0u}) ||
        koffingVisibility[0][1].inputs !=
            std::vector<float>({0.0f, 10.0f / 60.0f, 41.0f / 60.0f}) ||
        koffingVisibility[0][2].inputs !=
            std::vector<float>({0.0f, 12.0f / 60.0f, 44.0f / 60.0f})) {
        outFail =
            "SV Koffing's all-hidden 28201 smoke controller did not recover "
            "its paired one-second idle-puff visibility gates";
        return false;
    }

    // Scarlet/Violet's Koffing-family smoke uses SSSEffect rather than Unlit
    // or IkCharacter, but carries the same authored layer-mask,
    // displacement, and always-running UV controller contract. It must not
    // silently fall back to a static generic PBR puff.
    document["materials"][0]["shader_family"] = "SSSEffect";
    document["materials"][0]["float_parameters"]["EmissionIntensity"] =
        0.0f;
    document["materials"][0]["textures"][0].erase("source");
    document["materials"][0]["textures"].back().erase("source");
    json sssEffectMaskSubmesh =
        document["model"]["submeshes"][0];
    sssEffectMaskSubmesh["name"] =
        "pm0109_00_00_smokemask_b1_mesh_shape:smoke";
    document["model"]["submeshes"].push_back(
        std::move(sssEffectMaskSubmesh));
    document["model"]["submesh_count"] = 2;
    document["animations"].push_back({
        {"name", "pm0109_00_00_20450_rangeattack01"},
        {"duration_seconds", 1.0f},
        {"frame_rate", 60},
        {"loop", false},
        {"tracks", json::array()},
        {"mesh_visibility", json::array()},
        {"material_parameters", json::array({
             {{"mesh", "Triangle"},
              {"material", "smoke"},
              {"parameter", "UVScaleOffset"},
              {"x", json::array({
                  {{"frame", 0.0f}, {"value", 1.0f}},
                  {{"frame", 60.0f}, {"value", 1.0f}},
              })},
              {"y", json::array({
                  {{"frame", 0.0f}, {"value", 1.0f}},
                  {{"frame", 60.0f}, {"value", 1.0f}},
              })},
              {"z", json::array({
                  {{"frame", 0.0f}, {"value", 0.0f}},
                  {{"frame", 60.0f}, {"value", 0.0f}},
              })},
              {"w", json::array({
                  {{"frame", 0.0f}, {"value", 0.125f}},
                  {{"frame", 60.0f}, {"value", 0.375f}},
              })}},
             {{"mesh", "Triangle"},
              {"material", "smoke"},
              {"parameter", "UVScaleOffset3"},
              {"x", json::array({
                  {{"frame", 0.0f}, {"value", 1.0f}},
                  {{"frame", 60.0f}, {"value", 1.0f}},
              })},
              {"y", json::array({
                  {{"frame", 0.0f}, {"value", 1.0f}},
                  {{"frame", 60.0f}, {"value", 1.0f}},
              })},
              {"z", json::array({
                  {{"frame", 0.0f}, {"value", 0.0f}},
                  {{"frame", 60.0f}, {"value", 0.0f}},
              })},
              {"w", json::array({
                  {{"frame", 0.0f}, {"value", 0.25f}},
                  {{"frame", 60.0f}, {"value", 0.75f}},
              })}},
         })},
    });
    {
        std::ofstream output(manifestPath);
        output << document.dump(2);
    }
    game::runtime::render_model::MeshData sssEffectSmokeMesh;
    if (!tools::phlosion_native_model_ir::load(
            manifestPath.string(), sssEffectSmokeMesh, &outFail)) {
        return false;
    }
    if (sssEffectSmokeMesh.submeshMaterialModes.size() != 2u ||
        sssEffectSmokeMesh.submeshMaterialModes[0] !=
            game::runtime::render_model::
                kNativeLayeredUnlitMaterialMode ||
        sssEffectSmokeMesh.submeshMaterialFlags.size() != 2u ||
        !nearlyEqual(
            sssEffectSmokeMesh.submeshMaterialFlags[0],
            3.0f) ||
        sssEffectSmokeMesh.submeshMaterialParams0.size() != 2u ||
        !nearlyEqual(
            sssEffectSmokeMesh.submeshMaterialParams0[0].y,
            1.0f) ||
        sssEffectSmokeMesh.submeshAlphaMode.size() != 2u ||
        sssEffectSmokeMesh.submeshAlphaMode[0] != 2u ||
        sssEffectSmokeMesh.submeshAlphaMode[1] != 2u ||
        sssEffectSmokeMesh.continuousMaterialAnimations.size() != 2u ||
        sssEffectSmokeMesh.animationMaterialParameters.size() != 1u ||
        sssEffectSmokeMesh.animationMaterialParameters[0].size() != 2u ||
        sssEffectSmokeMesh.animationMaterialParameters[0][0]
                .submeshIndex != 0u ||
        sssEffectSmokeMesh.animationMaterialParameters[0][0]
                .components[3]
                .keys.size() != 2u ||
        !nearlyEqual(
            sssEffectSmokeMesh.animationMaterialParameters[0][0]
                .components[3]
                .keys[1]
                .value,
            0.375f) ||
        sssEffectSmokeMesh.animationMaterialParameters[0][1]
                .parameter != game::runtime::render_model::
                    MaterialAnimationParameter::UvScaleOffset3 ||
        !nearlyEqual(
            sssEffectSmokeMesh.animationMaterialParameters[0][1]
                .components[3]
                .keys[1]
                .value,
            0.75f) ||
        sssEffectSmokeMesh.submeshMetallicRoughnessTextures.size() != 2u ||
        sssEffectSmokeMesh.submeshMetallicRoughnessTextures[0].rgba !=
            std::vector<std::uint8_t>({0u, 0u, 0u, 255u}) ||
        sssEffectSmokeMesh.indices.size() != 6u ||
        sssEffectSmokeMesh.submeshIndexCount.size() != 2u ||
        sssEffectSmokeMesh.submeshIndexCount[0] != 3u ||
        sssEffectSmokeMesh.submeshIndexCount[1] != 3u) {
        outFail =
            "SSSEffect smoke lost its paired puff geometry, layered displacement, continuous UV, or clip-bound animation contract";
        return false;
    }
    document["model"]["submeshes"].erase(
        document["model"]["submeshes"].end() - 1);
    document["model"]["submesh_count"] = 1;

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
