#include <cmath>
#include <string>
#include <vector>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlInterop.h"
#include "vfx/preview/growl/GrowlSharedRenderer.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool nearlyEqual(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool compareMeshUvs(const std::string& modelPath, std::string& outFail) {
    vfx::runtime::growl_batches::MeshData previewMesh;
    std::string previewError;
    if (!vfx::preview::growl::detail::loadMeshForPreview(modelPath, previewMesh, &previewError)) {
        outFail = "Preview Growl mesh loader failed for " + modelPath + ": " + previewError;
        return false;
    }

    game::runtime::render_model::MeshData cachedMesh;
    std::string cacheError;
    if (!game::runtime::render_model::loadMeshFromCache(modelPath, cachedMesh, &cacheError)) {
        outFail = "Render model cache loader failed for " + modelPath + ": " + cacheError;
        return false;
    }

    const auto expectedMesh =
        game::runtime::shared_growl_interop::toReusableMeshData(cachedMesh);

    if (!expect(previewMesh.vertices.size() == expectedMesh.vertices.size(),
                "Preview Growl mesh loader should produce the same vertex count as the cached game path for " + modelPath,
                outFail)) {
        return false;
    }
    if (!expect(previewMesh.indices.size() == expectedMesh.indices.size(),
                "Preview Growl mesh loader should produce the same index count as the cached game path for " + modelPath,
                outFail)) {
        return false;
    }

    for (std::size_t i = 0; i < previewMesh.vertices.size(); ++i) {
        const auto& previewVertex = previewMesh.vertices[i];
        const auto& expectedVertex = expectedMesh.vertices[i];
        if (!nearlyEqual(previewVertex.uv.x, expectedVertex.uv.x) ||
            !nearlyEqual(previewVertex.uv.y, expectedVertex.uv.y)) {
            outFail = "Preview Growl mesh loader UV mismatch at vertex " + std::to_string(i) +
                      " for " + modelPath;
            return false;
        }
    }

    return true;
}

} // namespace

bool test_shared_growl_preview_mesh_loader_contract(std::string& outFail) {
    const std::vector<std::string> modelPaths = {
        "assets/meshes/growl_1076_mesh.glb",
        "assets/meshes/growl_1085_mesh.glb",
        "assets/meshes/growl_1092_mesh.glb",
        "assets/meshes/growl_1101_mesh.glb",
        "assets/meshes/growl_1108_mesh.glb",
        "assets/meshes/growl_1117_mesh.glb",
    };

    for (const auto& modelPath : modelPaths) {
        if (!compareMeshUvs(modelPath, outFail)) {
            return false;
        }
    }

    return true;
}
