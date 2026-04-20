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

bool validateLeer1268CsvMesh(std::string& outFail) {
    vfx::runtime::authored_batches::MeshData previewMesh;
    std::string previewError;
    const std::string modelPath = "assets/meshes/leer_1268_mesh_vsin.csv";
    if (!vfx::preview::authored::detail::loadMeshForPreview(modelPath, previewMesh, &previewError)) {
        outFail = "Preview authored VFX mesh loader failed for " + modelPath + ": " + previewError;
        return false;
    }

    if (!expect(previewMesh.vertices.size() == 142u,
                "Leer EID 1268 CSV preview mesh should preserve the exported vertex count",
                outFail)) {
        return false;
    }
    if (!expect(previewMesh.indices.size() == 420u,
                "Leer EID 1268 CSV preview mesh should convert the triangle strips into 140 triangles",
                outFail)) {
        return false;
    }

    const auto& firstVertex = previewMesh.vertices.front();
    if (!expect(nearlyEqual(firstVertex.position.x, -2.93846f) &&
                    nearlyEqual(firstVertex.position.y, -1.73064f) &&
                    nearlyEqual(firstVertex.position.z, 0.60395f),
                "Leer EID 1268 CSV preview mesh should preserve the first captured vertex position",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.uv.x, 0.38718f) &&
                    nearlyEqual(firstVertex.uv.y, 0.78863f),
                "Leer EID 1268 CSV preview mesh should preserve the first captured UV",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.color.r, 0.69804f) &&
                    nearlyEqual(firstVertex.color.a, 1.0f),
                "Leer EID 1268 CSV preview mesh should preserve the captured color channel",
                outFail)) {
        return false;
    }

    return true;
}

bool validateLeer1284CsvMesh(std::string& outFail) {
    vfx::runtime::authored_batches::MeshData previewMesh;
    std::string previewError;
    const std::string modelPath = "assets/meshes/leer_1284_mesh_vsin.csv";
    if (!vfx::preview::authored::detail::loadMeshForPreview(modelPath, previewMesh, &previewError)) {
        outFail = "Preview authored VFX mesh loader failed for " + modelPath + ": " + previewError;
        return false;
    }

    if (!expect(previewMesh.vertices.size() == 51u,
                "Leer EID 1284 CSV preview mesh should preserve the exported vertex count",
                outFail)) {
        return false;
    }
    if (!expect(previewMesh.indices.size() == 117u,
                "Leer EID 1284 CSV preview mesh should convert the triangle strips into 39 triangles",
                outFail)) {
        return false;
    }

    const auto& firstVertex = previewMesh.vertices.front();
    if (!expect(nearlyEqual(firstVertex.position.x, 4.65387f) &&
                    nearlyEqual(firstVertex.position.y, -1.57675f) &&
                    nearlyEqual(firstVertex.position.z, 0.18154f),
                "Leer EID 1284 CSV preview mesh should preserve the first captured vertex position",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.uv.x, 0.90061f) &&
                    nearlyEqual(firstVertex.uv.y, 0.73402f),
                "Leer EID 1284 CSV preview mesh should preserve the first captured UV",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.color.r, 0.69804f) &&
                    nearlyEqual(firstVertex.color.a, 1.0f),
                "Leer EID 1284 CSV preview mesh should preserve the captured color channel",
                outFail)) {
        return false;
    }

    return true;
}

bool validateLeer1291CsvMesh(std::string& outFail) {
    vfx::runtime::authored_batches::MeshData previewMesh;
    std::string previewError;
    const std::string modelPath = "assets/meshes/leer_1291_mesh_vsin.csv";
    if (!vfx::preview::authored::detail::loadMeshForPreview(modelPath, previewMesh, &previewError)) {
        outFail = "Preview authored VFX mesh loader failed for " + modelPath + ": " + previewError;
        return false;
    }

    if (!expect(previewMesh.vertices.size() == 48u,
                "Leer EID 1291 CSV preview mesh should preserve the exported vertex count",
                outFail)) {
        return false;
    }
    if (!expect(previewMesh.indices.size() == 114u,
                "Leer EID 1291 CSV preview mesh should convert the triangle strips into 38 triangles",
                outFail)) {
        return false;
    }

    const auto& firstVertex = previewMesh.vertices.front();
    if (!expect(nearlyEqual(firstVertex.position.x, -5.15755f) &&
                    nearlyEqual(firstVertex.position.y, 2.25895f) &&
                    nearlyEqual(firstVertex.position.z, 1.39244f),
                "Leer EID 1291 CSV preview mesh should preserve the first captured vertex position",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.uv.x, 0.11079f) &&
                    nearlyEqual(firstVertex.uv.y, 0.40718f),
                "Leer EID 1291 CSV preview mesh should preserve the first captured UV",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.color.r, 0.69804f) &&
                    nearlyEqual(firstVertex.color.a, 1.0f),
                "Leer EID 1291 CSV preview mesh should preserve the captured color channel",
                outFail)) {
        return false;
    }

    return true;
}

bool validateLeerLeftEyeCsvMesh(const std::string& modelPath,
                                std::size_t expectedVertices,
                                std::size_t expectedIndices,
                                float firstX,
                                float firstY,
                                float firstZ,
                                float firstU,
                                float firstV,
                                std::string& outFail) {
    vfx::runtime::authored_batches::MeshData previewMesh;
    std::string previewError;
    if (!vfx::preview::authored::detail::loadMeshForPreview(modelPath, previewMesh, &previewError)) {
        outFail = "Preview authored VFX mesh loader failed for " + modelPath + ": " + previewError;
        return false;
    }

    if (!expect(previewMesh.vertices.size() == expectedVertices,
                "Leer left-eye CSV preview mesh should preserve the exported vertex count for " + modelPath,
                outFail)) {
        return false;
    }
    if (!expect(previewMesh.indices.size() == expectedIndices,
                "Leer left-eye CSV preview mesh should preserve the exported triangle-strip expansion for " + modelPath,
                outFail)) {
        return false;
    }

    const auto& firstVertex = previewMesh.vertices.front();
    if (!expect(nearlyEqual(firstVertex.position.x, firstX) &&
                    nearlyEqual(firstVertex.position.y, firstY) &&
                    nearlyEqual(firstVertex.position.z, firstZ),
                "Leer left-eye CSV preview mesh should preserve the first captured vertex position for " + modelPath,
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.uv.x, firstU) &&
                    nearlyEqual(firstVertex.uv.y, firstV),
                "Leer left-eye CSV preview mesh should preserve the first captured UV for " + modelPath,
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(firstVertex.color.r, 0.69804f) &&
                    nearlyEqual(firstVertex.color.a, 1.0f),
                "Leer left-eye CSV preview mesh should preserve the captured color channel for " + modelPath,
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
    if (!validateLeer1268CsvMesh(outFail)) {
        return false;
    }
    if (!validateLeer1284CsvMesh(outFail)) {
        return false;
    }
    if (!validateLeer1291CsvMesh(outFail)) {
        return false;
    }
    if (!validateLeerLeftEyeCsvMesh("assets/meshes/leer_1308_mesh_vsin.csv",
                                    53u,
                                    123u,
                                    3.24903f,
                                    -2.28125f,
                                    -0.61033f,
                                    0.85008f,
                                    0.83986f,
                                    outFail)) {
        return false;
    }
    if (!validateLeerLeftEyeCsvMesh("assets/meshes/leer_1320_mesh_vsin.csv",
                                    143u,
                                    423u,
                                    -0.60359f,
                                    -2.22007f,
                                    -0.22109f,
                                    0.52396f,
                                    0.82919f,
                                    outFail)) {
        return false;
    }
    if (!validateLeerLeftEyeCsvMesh("assets/meshes/leer_1336_mesh_vsin.csv",
                                    48u,
                                    114u,
                                    -5.15755f,
                                    2.25895f,
                                    -1.39244f,
                                    0.11079f,
                                    0.40718f,
                                    outFail)) {
        return false;
    }
    if (!validateLeerLeftEyeCsvMesh("assets/meshes/leer_1343_mesh_vsin.csv",
                                    51u,
                                    117u,
                                    4.65387f,
                                    -1.57675f,
                                    -0.18154f,
                                    0.90061f,
                                    0.73402f,
                                    outFail)) {
        return false;
    }

    return true;
}
