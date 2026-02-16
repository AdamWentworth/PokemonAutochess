#include <filesystem>
#include <fstream>
#include <string>

namespace {

int countLines(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) return -1;

    int lines = 0;
    std::string ignored;
    while (std::getline(in, ignored)) {
        ++lines;
    }
    return lines;
}

bool expectMaxLines(const std::filesystem::path& path,
                    int maxLines,
                    std::string& outFail) {
    const int lines = countLines(path);
    if (lines < 0) {
        outFail = "failed to read file: " + path.string();
        return false;
    }
    if (lines > maxLines) {
        outFail = "line budget exceeded for " + path.string() +
                  " (" + std::to_string(lines) + " > " + std::to_string(maxLines) + ")";
        return false;
    }
    return true;
}

}  // namespace

bool test_model_loader_source_modularity(std::string& outFail) {
    const std::filesystem::path loaderPath = "src/engine/render/gltf/ModelFastGltfLoader.cpp";
    const std::filesystem::path helperPath = "src/engine/render/gltf/ModelFastGltfLoaderHelpers.cpp";
    const std::filesystem::path texturesPath = "src/engine/render/gltf/ModelFastGltfTextures.cpp";
    const std::filesystem::path materialPath = "src/engine/render/gltf/ModelFastGltfMaterial.cpp";
    const std::filesystem::path scenePath = "src/engine/render/gltf/ModelFastGltfSceneData.cpp";

    if (!std::filesystem::exists(loaderPath)) {
        outFail = "missing loader source file: " + loaderPath.string();
        return false;
    }
    if (!std::filesystem::exists(helperPath)) {
        outFail = "missing helper source file: " + helperPath.string();
        return false;
    }
    if (!std::filesystem::exists(texturesPath)) {
        outFail = "missing textures source file: " + texturesPath.string();
        return false;
    }
    if (!std::filesystem::exists(materialPath)) {
        outFail = "missing material source file: " + materialPath.string();
        return false;
    }
    if (!std::filesystem::exists(scenePath)) {
        outFail = "missing scene-data source file: " + scenePath.string();
        return false;
    }

    if (!expectMaxLines(loaderPath, 550, outFail)) return false;
    if (!expectMaxLines(helperPath, 250, outFail)) return false;
    if (!expectMaxLines(texturesPath, 650, outFail)) return false;
    if (!expectMaxLines(materialPath, 250, outFail)) return false;
    if (!expectMaxLines(scenePath, 400, outFail)) return false;

    return true;
}
