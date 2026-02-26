#include "game/runtime/backend_model_cache/BackendModelCache.h"
#include "game/runtime/backend_model_cache/BackendModelCacheFormat.h"
#include "game/runtime/backend_model_cache/BackendModelCacheLoadOrRebuild.h"
#include "game/runtime/backend_model_cache/BackendModelCacheReadDecode.h"
#include "game/runtime/backend_model_cache/BackendModelCacheSourceBuild.h"
#include "game/runtime/backend_model_cache/BackendModelCacheWrite.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include "engine/core/Environment.h"

namespace {
namespace fs = std::filesystem;

std::uint64_t fnv1a64(const std::string& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::string hexHash64(std::uint64_t v) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << v;
    return out.str();
}

bool rebuildCacheFromSource(const std::string& modelPath, std::string* outError) {
    game::runtime::backend_model::detail::SourceCacheBuildData data;
    std::string err;
    if (!game::runtime::backend_model::detail::buildBackendCacheSourceData(modelPath, data, &err)) {
        if (outError) *outError = err;
        return false;
    }
    if (!game::runtime::backend_model::detail::writeBackendCacheFromSourceData(modelPath, data, &err)) {
        if (outError) *outError = err;
        return false;
    }
    return true;
}

} // namespace

namespace game::runtime::backend_model {

std::string cachePathForModel(const std::string& modelPath) {
    const fs::path out = fs::path("cache") / "models" / (hexHash64(fnv1a64(modelPath)) + ".pacmdl");
    return out.string();
}

bool loadMeshFromCache(const std::string& modelPath, MeshData& out, std::string* outError) {
    out = MeshData{};
    if (engine::env::truthyNonZero("PAC_DISABLE_MODELCACHE")) {
        if (outError) *outError = "model cache disabled by PAC_DISABLE_MODELCACHE";
        return false;
    }
    if (modelPath.empty()) {
        if (outError) *outError = "empty model path";
        return false;
    }

    std::ifstream in;
    detail::CacheHeader hdr{};
    if (!detail::openValidatedCacheStreamForModel(modelPath,
                                                  &cachePathForModel,
                                                  &rebuildCacheFromSource,
                                                  in,
                                                  hdr,
                                                  outError)) {
        return false;
    }

    return detail::decodeMeshFromValidatedCacheStream(in, hdr, out, outError);
}

} // namespace game::runtime::backend_model
