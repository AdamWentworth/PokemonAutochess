// FastGLTFLoader.h

#pragma once

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace pac::fastgltf_loader {

inline bool envFlagEnabled(const char* name) {
    if (name == nullptr) return false;
    const char* v = std::getenv(name);
    if (v == nullptr) return false;

    std::string s(v);
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return (s == "1" || s == "true" || s == "yes" || s == "on");
}

inline bool shouldUseFastGLTF() {
    // Set: PAC_USE_FASTGLTF=1
    return envFlagEnabled("PAC_USE_FASTGLTF");
}

inline const char* errorName(fastgltf::Error e) {
    switch (e) {
        case fastgltf::Error::None: return "None";
        case fastgltf::Error::InvalidPath: return "InvalidPath";
        case fastgltf::Error::MissingExtensions: return "MissingExtensions";
        case fastgltf::Error::UnknownRequiredExtension: return "UnknownRequiredExtension";
        case fastgltf::Error::InvalidJson: return "InvalidJson";
        case fastgltf::Error::InvalidGltf: return "InvalidGltf";
        case fastgltf::Error::InvalidOrMissingAssetField: return "InvalidOrMissingAssetField";
        case fastgltf::Error::InvalidGLB: return "InvalidGLB";
        case fastgltf::Error::MissingField: return "MissingField";
        case fastgltf::Error::MissingExternalBuffer: return "MissingExternalBuffer";
        case fastgltf::Error::UnsupportedVersion: return "UnsupportedVersion";
        case fastgltf::Error::InvalidURI: return "InvalidURI";
        case fastgltf::Error::InvalidFileData: return "InvalidFileData";
        case fastgltf::Error::FailedWritingFiles: return "FailedWritingFiles";
        default: return "UnknownError";
    }
}

// “Enable everything fastgltf documents as supported extensions”.
constexpr fastgltf::Extensions kSupportedExtensionsMask =
    fastgltf::Extensions::KHR_texture_transform |
    fastgltf::Extensions::KHR_texture_basisu |
    fastgltf::Extensions::MSFT_texture_dds |
    fastgltf::Extensions::KHR_mesh_quantization |
    fastgltf::Extensions::EXT_meshopt_compression |
    fastgltf::Extensions::KHR_lights_punctual |
    fastgltf::Extensions::EXT_texture_webp |
    fastgltf::Extensions::KHR_materials_specular |
    fastgltf::Extensions::KHR_materials_ior |
    fastgltf::Extensions::KHR_materials_iridescence |
    fastgltf::Extensions::KHR_materials_volume |
    fastgltf::Extensions::KHR_materials_transmission |
    fastgltf::Extensions::KHR_materials_clearcoat |
    fastgltf::Extensions::KHR_materials_emissive_strength |
    fastgltf::Extensions::KHR_materials_sheen |
    fastgltf::Extensions::KHR_materials_unlit |
    fastgltf::Extensions::KHR_materials_anisotropy |
    fastgltf::Extensions::EXT_mesh_gpu_instancing |
    fastgltf::Extensions::MSFT_packing_normalRoughnessMetallic |
    fastgltf::Extensions::MSFT_packing_occlusionRoughnessMetallic |
    fastgltf::Extensions::KHR_materials_dispersion |
    fastgltf::Extensions::KHR_materials_variants;

struct LoadResult {
    fastgltf::Asset asset;
    std::filesystem::path baseDir;
};

inline std::optional<LoadResult> tryLoad(const std::string& filepath) {
    std::filesystem::path path(filepath);
    std::error_code ec;
    auto absPath = std::filesystem::absolute(path, ec);
    const auto& usePath = ec ? path : absPath;

    auto data = fastgltf::GltfDataBuffer::FromPath(usePath);
    if (data.error() != fastgltf::Error::None) {
        std::cerr << "[fastgltf] read failed: " << usePath.string()
                  << " (Error=" << errorName(data.error()) << ")\n";
        return std::nullopt;
    }

    fastgltf::Parser parser(kSupportedExtensionsMask);

    auto baseDir = usePath.parent_path();
    auto asset = parser.loadGltf(data.get(), baseDir, fastgltf::Options::None);
    if (asset.error() != fastgltf::Error::None) {
        std::cerr << "[fastgltf] parse failed: " << usePath.string()
                  << " (Error=" << errorName(asset.error()) << ")\n";
        return std::nullopt;
    }

    std::cout << "[fastgltf] Parsed OK (toggle path): " << usePath.filename().string() << "\n";
    return LoadResult{ std::move(asset.get()), baseDir };
}

} // namespace pac::fastgltf_loader
