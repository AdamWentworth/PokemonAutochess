#include "game/runtime/render_model_cache/RenderModelCacheLoadOrRebuild.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {
namespace fs = std::filesystem;

using game::runtime::render_model::detail::CacheHeader;
constexpr std::uint64_t kModelCacheMagic = game::runtime::render_model::detail::kModelCacheMagic;
constexpr std::uint32_t kModelCacheVersion = game::runtime::render_model::detail::kModelCacheVersion;

template <typename T>
bool readPod(std::istream& in, T& out) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&out), sizeof(T)));
}

bool sourceMetadataForModel(const std::string& modelPath,
                            std::uint64_t& outFileSize,
                            std::int64_t& outWriteTime) {
    std::error_code ec;
    if (!fs::exists(modelPath, ec) || ec) return false;
    outFileSize = static_cast<std::uint64_t>(fs::file_size(modelPath, ec));
    if (ec) return false;
    outWriteTime = static_cast<std::int64_t>(fs::last_write_time(modelPath, ec).time_since_epoch().count());
    return !ec;
}

bool cacheHeaderMatchesSource(const std::string& modelPath, const CacheHeader& hdr) {
    std::uint64_t srcFileSize = 0u;
    std::int64_t srcWriteTime = 0;
    if (!sourceMetadataForModel(modelPath, srcFileSize, srcWriteTime)) return false;
    return hdr.srcFileSize == srcFileSize && hdr.srcWriteTime == srcWriteTime;
}

} // namespace

namespace game::runtime::render_model::detail {

bool openValidatedCacheStreamForModel(const std::string& modelPath,
                                      CachePathForModelFn cachePathForModelFn,
                                      RebuildCacheFromSourceFn rebuildCacheFromSourceFn,
                                      std::ifstream& outStream,
                                      CacheHeader& outHeader,
                                      std::string* outError) {
    outHeader = CacheHeader{};
    outStream = std::ifstream{};

    bool attemptedRebuild = false;
retry_read:
    const std::string cachePath = cachePathForModelFn(modelPath);
    std::ifstream in(cachePath, std::ios::binary);
    if (!in.is_open()) {
        if (!attemptedRebuild) {
            std::string rebuildErr;
            if (rebuildCacheFromSourceFn(modelPath, &rebuildErr)) {
                attemptedRebuild = true;
                goto retry_read;
            }
            if (outError) *outError = "cache file not found: " + cachePath + " (rebuild failed: " + rebuildErr + ")";
            return false;
        }
        if (outError) *outError = "cache file not found: " + cachePath;
        return false;
    }

    CacheHeader hdr{};
    if (!readPod(in, hdr)) {
        if (!attemptedRebuild) {
            std::string rebuildErr;
            if (rebuildCacheFromSourceFn(modelPath, &rebuildErr)) {
                attemptedRebuild = true;
                goto retry_read;
            }
            if (outError) *outError = "failed to read cache header (rebuild failed: " + rebuildErr + ")";
            return false;
        }
        if (outError) *outError = "failed to read cache header";
        return false;
    }
    if (hdr.magic != kModelCacheMagic || hdr.version != kModelCacheVersion) {
        if (!attemptedRebuild) {
            std::string rebuildErr;
            if (rebuildCacheFromSourceFn(modelPath, &rebuildErr)) {
                attemptedRebuild = true;
                goto retry_read;
            }
            if (outError) *outError = "cache format mismatch (rebuild failed: " + rebuildErr + ")";
            return false;
        }
        if (outError) *outError = "cache format mismatch";
        return false;
    }
    if (!cacheHeaderMatchesSource(modelPath, hdr)) {
        if (!attemptedRebuild) {
            std::string rebuildErr;
            if (rebuildCacheFromSourceFn(modelPath, &rebuildErr)) {
                attemptedRebuild = true;
                goto retry_read;
            }
            if (outError) *outError = "cache stale and rebuild failed: " + rebuildErr;
            return false;
        }
        if (outError) *outError = "cache stale after rebuild";
        return false;
    }

    outHeader = hdr;
    outStream = std::move(in);
    return true;
}

} // namespace game::runtime::render_model::detail
