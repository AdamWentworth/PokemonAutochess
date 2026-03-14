#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool fileContainsToken(const std::filesystem::path& path, const std::string& token) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.find(token) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

bool test_projected_mesh_renderer_hot_path_contract(std::string& outFail) {
    const std::filesystem::path rendererPath =
        "src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp";
    const std::vector<std::filesystem::path> forbiddenPaths = {
        "src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshIndexedPath.cpp",
        "src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshIndexedPath.h",
    };
    const std::vector<std::string> requiredTokens = {
        "support::gpuSkinBatchStateEntries()",
        "resolveRigidSkinningModelMatrix(",
        "handledFastTexturedPath = true;",
        "bool fastPathHasGeometry = false;",
        "worldIndexedBatches.push_back(std::move(batch));",
    };

    if (!std::filesystem::exists(rendererPath)) {
        outFail = "missing projected mesh renderer source: " + rendererPath.string();
        return false;
    }

    for (const auto& path : forbiddenPaths) {
        if (std::filesystem::exists(path)) {
            outFail = "indexed projected mesh hot path was split out again: " + path.string() +
                      "; keep this branch inline in SharedProjectedUnitBackendMeshRenderer.cpp";
            return false;
        }
    }

    for (const auto& token : requiredTokens) {
        if (fileContainsToken(rendererPath, token)) continue;
        outFail = "projected mesh hot-path contract failed: missing token '" + token +
                  "' in " + rendererPath.string();
        return false;
    }

    return true;
}
