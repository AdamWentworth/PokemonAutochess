#pragma once

#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace game::runtime::lgpe_route1_projected_shadow {

inline constexpr int kNativeAtlasWidth = 2048;
inline constexpr int kNativeAtlasHeight = 2048;

struct BuildStats {
    std::uint64_t drawCount = 0u;
    std::uint64_t instanceCount = 0u;
    std::uint64_t submittedTriangleCount = 0u;
    std::uint64_t rasterizedTriangleCount = 0u;
    std::uint64_t writtenPixelCount = 0u;
};

// Exact orthographic basis recovered from the synchronous Route 1 guest
// capture. Translation is recentered without changing the captured light
// orientation, scale, or depth span.
std::array<float, 16> projectionForCenter(
    const std::array<float, 3>& sourceCenterCm);

class Atlas {
public:
    bool build(
        const std::vector<lgpe_world_scene::PreparedScene*>& scenes,
        const std::array<float, 3>& sourceCenterCm,
        std::string* outError = nullptr);

    bool build(
        const std::vector<lgpe_world_scene::PreparedScene*>& scenes,
        const std::array<float, 3>& sourceCenterCm,
        int atlasWidth,
        int atlasHeight,
        std::string* outError = nullptr);

    void attach(
        const std::vector<lgpe_world_scene::PreparedScene*>& scenes) const;

    const std::vector<unsigned char>& rgba() const noexcept { return rgba_; }
    const std::array<float, 16>& projection() const noexcept {
        return projection_;
    }
    const BuildStats& stats() const noexcept { return stats_; }
    const std::string& textureKey() const noexcept { return textureKey_; }

private:
    std::array<float, 16> projection_{};
    std::vector<float> depth_;
    std::vector<unsigned char> rgba_;
    BuildStats stats_{};
    std::string textureKey_;
    int width_ = kNativeAtlasWidth;
    int height_ = kNativeAtlasHeight;
};

} // namespace game::runtime::lgpe_route1_projected_shadow
