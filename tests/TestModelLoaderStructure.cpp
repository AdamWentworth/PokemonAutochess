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
    const std::filesystem::path loaderPath = "src/engine/render/ModelFastGltfLoader.cpp";
    const std::filesystem::path helperPath = "src/engine/render/ModelFastGltfLoaderHelpers.cpp";

    if (!std::filesystem::exists(loaderPath)) {
        outFail = "missing loader source file: " + loaderPath.string();
        return false;
    }
    if (!std::filesystem::exists(helperPath)) {
        outFail = "missing helper source file: " + helperPath.string();
        return false;
    }

    if (!expectMaxLines(loaderPath, 900, outFail)) return false;
    if (!expectMaxLines(helperPath, 700, outFail)) return false;

    return true;
}
