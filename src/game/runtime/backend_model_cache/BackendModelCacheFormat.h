#pragma once

#include <cstdint>

namespace game::runtime::backend_model::detail {

inline constexpr std::uint64_t kModelCacheMagic = 0x4C444D434150554FULL;
inline constexpr std::uint32_t kModelCacheVersion = 5;

#pragma pack(push, 1)
struct CacheHeader {
    std::uint64_t magic = kModelCacheMagic;
    std::uint32_t version = kModelCacheVersion;
    std::int64_t srcWriteTime = 0;
    std::uint64_t srcFileSize = 0;
    float modelScaleFactor = 1.0f;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t submeshCount = 0;
    std::uint32_t nodeCount = 0;
    std::uint32_t skinCount = 0;
    std::uint32_t animCount = 0;
};
#pragma pack(pop)

struct CacheTextureHeader {
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t wrapS = 0;
    std::int32_t wrapT = 0;
    std::int32_t minF = 0;
    std::int32_t magF = 0;
    std::uint32_t bytes = 0;
};

} // namespace game::runtime::backend_model::detail
