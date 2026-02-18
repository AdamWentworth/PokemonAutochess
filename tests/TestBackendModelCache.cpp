#include "game/runtime/BackendModelCache.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

bool test_backend_model_cache_contract(std::string& outFail) {
    using game::runtime::backend_model::MeshData;
    using game::runtime::backend_model::cachePathForModel;
    using game::runtime::backend_model::loadMeshFromCache;

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

    return true;
}
