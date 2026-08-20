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
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.cpp";
    const std::filesystem::path indexedFinalizePath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshIndexedFinalize.cpp";
    const std::filesystem::path cpuRewritePath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCpuRewrite.cpp";
    const std::filesystem::path cachedIndexedPath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCachedIndexedBatches.cpp";
    const std::filesystem::path fastPathPath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastPath.cpp";
    const std::filesystem::path gpuSkinBatchStatePath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGpuSkinBatchState.cpp";
    const std::filesystem::path supportPath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.cpp";
    const std::filesystem::path graphicsQualityPath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGraphicsQuality.cpp";
    const std::filesystem::path triangleLoopPath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleLoop.cpp";
    const std::filesystem::path persistentItemsPath =
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.cpp";
    const std::vector<std::filesystem::path> forbiddenPaths = {
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshIndexedPath.cpp",
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshIndexedPath.h",
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTailFireOverride.cpp",
    };
    const std::vector<std::pair<std::filesystem::path, std::string>> requiredTokens = {
        {rendererPath, "cached_indexed_batches::buildCachedIndexedBatches("},
        {rendererPath, "handledFastTexturedPath = cachedIndexedResult.handled;"},
        {indexedFinalizePath, "worldIndexedBatches.push_back(std::move(batch));"},
        {cpuRewritePath, "resolveModelVertexSurface("},
        {cachedIndexedPath, "support::gpuSkinBatchStateEntries()"},
        {cachedIndexedPath, "buildOrReuseCpuRewriteVertices("},
        {cachedIndexedPath, "anyIndexedBatchHasGeometry("},
        {fastPathPath, "worldIndexedBatches.emplace_back();"},
        {gpuSkinBatchStatePath, "configureGpuClipSkinningBatch("},
        {graphicsQualityPath, "material.materialFlipbook1Frames = textureDetailLodBiasForGraphicsQuality("},
        {triangleLoopPath, "triangleSubmitter.pushTriangle("},
        {triangleLoopPath, "appendFastTexturedTriangle("},
        {persistentItemsPath, "syncProjectedRenderItemDynamicState("},
        {supportPath, "preparedMeshState()"},
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

    for (const auto& [path, token] : requiredTokens) {
        if (!std::filesystem::exists(path)) {
            outFail = "projected mesh hot-path contract failed: missing source " + path.string();
            return false;
        }
        if (fileContainsToken(path, token)) continue;
        outFail = "projected mesh hot-path contract failed: missing token '" + token +
                  "' in " + path.string();
        return false;
    }

    const std::vector<std::string> forbiddenSupportTokens = {
        "applyGraphicsQualityToBatchTemplate(",
        "applyGraphicsQualityToWorldSceneMaterial(",
    };
    for (const auto& token : forbiddenSupportTokens) {
        if (!fileContainsToken(supportPath, token)) continue;
        outFail = "projected mesh hot-path contract failed: unexpected token '" + token +
                  "' in " + supportPath.string();
        return false;
    }

    return true;
}

