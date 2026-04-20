#include <cmath>
#include <string>
#include <vector>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"
#include "vfx/preview/shared/SharedAuthoredVfxRenderer.h"

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
    vfx::runtime::authored_batches::MeshData previewMesh;
    std::string previewError;
    if (!vfx::preview::authored::detail::loadMeshForPreview(modelPath, previewMesh, &previewError)) {
        outFail = "Preview authored VFX mesh loader failed for " + modelPath + ": " + previewError;
        return false;
    }

    game::runtime::render_model::MeshData cachedMesh;
    std::string cacheError;
    if (!game::runtime::render_model::loadMeshFromCache(modelPath, cachedMesh, &cacheError)) {
        outFail = "Render model cache loader failed for " + modelPath + ": " + cacheError;
        return false;
    }

    const auto expectedMesh =
        game::runtime::shared_authored_vfx_interop::toReusableMeshData(cachedMesh);

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

bool validateLeerCsvMesh(std::string& outFail) {
    vfx::runtime::authored_batches::MeshData previewMesh;
    std::string previewError;
    const std::string modelPath = "assets/meshes/leer_1254_mesh_vsin.csv";
    if (!vfx::preview::authored::detail::loadMeshForPreview(modelPath, previewMesh, &previewError)) {
        outFail = "Preview authored VFX mesh loader failed for " + modelPath + ": " + previewError;
        return false;
    }

    if (!expect(previewMesh.vertices.size() == 51u,
                "Leer CSV preview mesh should preserve the exported vertex count",
                outFail)) {
        return false;
    }
    if (!expect(previewMesh.indices.size() == 117u,
                "Leer CSV preview mesh should convert the triangle strips into 39 triangles",
                outFail)) {
        return false;
    }

    const auto& firstVertex = previewMesh.vertices.front();
    if (!expect(nearlyEqual(firstVertex.position.x, 3.87671f) &&
                    nearlyEqual(firstVertex.position.y, -1.03910f) &&
                    nearlyEqual(firstVertex.position.z, 0.37006f),
                "Leer CSV preview mesh should preserve the first captured vertex position",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.uv.x, 0.90061f) &&
                    nearlyEqual(firstVertex.uv.y, 0.73402f),
                "Leer CSV preview mesh should preserve the first captured UV",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.color.a, 1.0f),
                "Leer CSV preview mesh should preserve the captured alpha channel",
                outFail)) {
        return false;
    }

    return true;
}

} // namespace

bool test_shared_authored_vfx_preview_mesh_loader_contract(std::string& outFail) {
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

    if (!validateLeerCsvMesh(outFail)) {
        return false;
    }

    return true;
}
