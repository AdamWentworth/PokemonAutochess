#include "game/runtime/render_model_cache/RenderModelCache.h"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <string>

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

bool test_render_model_cache_contract(std::string& outFail) {
    using game::runtime::render_model::MeshData;
    using game::runtime::render_model::cachePathForModel;
    using game::runtime::render_model::loadMeshFromCache;

    const std::string pathA = cachePathForModel("assets/models/0004_Charmander.glb");
    const std::string pathB = cachePathForModel("assets/models/0004_Charmander.glb");
    const std::string pathC = cachePathForModel("assets/models/0007_Squirtle.glb");

    if (pathA != pathB) {
        outFail = "cachePathForModel should be deterministic for identical inputs";
        return false;
    }
    if (pathA == pathC) {
        outFail = "cachePathForModel should differ for different model paths";
        return false;
    }
    if (!contains(pathA, "cache") || !contains(pathA, ".pacmdl")) {
        outFail = "cachePathForModel should target cache/models/*.pacmdl";
        return false;
    }

    {
        MeshData mesh;
        std::string err;
        if (loadMeshFromCache("", mesh, &err)) {
            outFail = "loadMeshFromCache should fail for empty model path";
            return false;
        }
        if (err.empty()) {
            outFail = "loadMeshFromCache should provide an error for empty model path";
            return false;
        }
        if (!mesh.vertices.empty() ||
            !mesh.indices.empty() ||
            !mesh.triangleBaseColors.empty() ||
            !mesh.triangleOpacity.empty() ||
            !mesh.triangleDoubleSided.empty() ||
            !mesh.vertexBaseColors.empty() ||
            !mesh.submeshMeshIndex.empty() ||
            !mesh.submeshBaseTextures.empty() ||
            !mesh.submeshAlphaMode.empty() ||
            !mesh.submeshAlphaCutoff.empty() ||
            !mesh.meshIndexToNode.empty() ||
            !mesh.triangleNodeIndex.empty() ||
            !mesh.triangleSkinIndex.empty() ||
            !mesh.nodesDefault.empty() ||
            !mesh.bindNodeGlobals.empty() ||
            !mesh.animations.empty()) {
            outFail = "loadMeshFromCache should clear output mesh on failure";
            return false;
        }
    }

    {
        MeshData mesh;
        std::string err;
        if (loadMeshFromCache("assets/models/not_a_real_model.glb", mesh, &err)) {
            outFail = "loadMeshFromCache should fail when cache file is missing";
            return false;
        }
        if (err.empty()) {
            outFail = "loadMeshFromCache should provide an error when cache file is missing";
            return false;
        }
        if (!mesh.vertices.empty() ||
            !mesh.indices.empty() ||
            !mesh.triangleBaseColors.empty() ||
            !mesh.triangleOpacity.empty() ||
            !mesh.triangleDoubleSided.empty() ||
            !mesh.vertexBaseColors.empty() ||
            !mesh.submeshMeshIndex.empty() ||
            !mesh.submeshBaseTextures.empty() ||
            !mesh.submeshAlphaMode.empty() ||
            !mesh.submeshAlphaCutoff.empty() ||
            !mesh.meshIndexToNode.empty() ||
            !mesh.triangleNodeIndex.empty() ||
            !mesh.triangleSkinIndex.empty() ||
            !mesh.nodesDefault.empty() ||
            !mesh.bindNodeGlobals.empty() ||
            !mesh.animations.empty()) {
            outFail = "missing-cache load failure should leave mesh output empty";
            return false;
        }
    }

    {
        namespace fs = std::filesystem;
        const std::string fakeModel = "assets/models/_unit_test_corrupt_cache_probe.glb";
        const fs::path corruptPath = cachePathForModel(fakeModel);
        std::error_code ec;
        fs::create_directories(corruptPath.parent_path(), ec);
        if (ec) {
            outFail = "failed to create cache directory for corrupt-cache probe";
            return false;
        }

        {
            std::ofstream out(corruptPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                outFail = "failed to write corrupt cache probe file";
                return false;
            }
            const char payload[] = {'B', 'A', 'D', '!'};
            out.write(payload, sizeof(payload));
        }

        MeshData mesh;
        std::string err;
        const bool ok = loadMeshFromCache(fakeModel, mesh, &err);
        fs::remove(corruptPath, ec);
        if (ok) {
            outFail = "loadMeshFromCache should reject corrupt cache files";
            return false;
        }
        if (err.empty()) {
            outFail = "loadMeshFromCache should surface an error for corrupt cache files";
            return false;
        }
    }

    {
        MeshData mesh;
        std::string err;
        const std::string growlMeshPath = "assets/meshes/growl_1255_mesh.glb";
        if (!loadMeshFromCache(growlMeshPath, mesh, &err)) {
            outFail = "loadMeshFromCache should load the Growl 1255 sparkle mesh: " + err;
            return false;
        }
        if (mesh.vertices.size() < 4u) {
            outFail = "Growl 1255 sparkle mesh should decode at least one textured quad";
            return false;
        }
        auto approx = [](float a, float b) { return std::fabs(a - b) <= 0.001f; };
        const auto& v0 = mesh.vertices[0].uv;
        const auto& v1 = mesh.vertices[1].uv;
        const auto& v2 = mesh.vertices[2].uv;
        const auto& v3 = mesh.vertices[3].uv;
        const bool hasExpectedQuadUvs =
            approx(v0.x, 0.0f) && approx(v0.y, 0.0f) &&
            approx(v1.x, 1.0f) && approx(v1.y, 0.0f) &&
            approx(v2.x, 0.0f) && approx(v2.y, 1.0f) &&
            approx(v3.x, 1.0f) && approx(v3.y, 1.0f);
        if (!hasExpectedQuadUvs) {
            outFail =
                "Growl 1255 sparkle mesh should preserve RenderDoc rawtex0 UVs instead of falling back to generated planar UVs";
            return false;
        }
    }

    return true;
}
