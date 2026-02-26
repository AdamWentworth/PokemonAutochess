#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAtlasHelpers.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_shared_tail_fire_atlas_helpers_contract(std::string& outFail) {
    using namespace game::runtime::shared_tail_fire_atlas;

    {
        const std::vector<unsigned char> src = {
            255u, 128u,   0u, 128u,
             10u,  20u,  30u, 255u
        };
        RgbaTextureOwned out;
        if (!expect(buildPremultipliedAtlas({src.data(), 2, 1}, out),
                    "buildPremultipliedAtlas should succeed on valid RGBA data.",
                    outFail)) {
            return false;
        }
        if (!expect(out.width == 2 && out.height == 1 && out.rgba.size() == src.size(),
                    "buildPremultipliedAtlas should preserve dimensions and pixel count.",
                    outFail)) {
            return false;
        }
        if (!expect(out.rgba[0] == 128u && out.rgba[1] == 64u && out.rgba[2] == 0u && out.rgba[3] == 128u,
                    "buildPremultipliedAtlas should premultiply RGB by alpha and preserve alpha (with rounding).",
                    outFail)) {
            return false;
        }
        if (!expect(out.rgba[4] == 10u && out.rgba[5] == 20u && out.rgba[6] == 30u && out.rgba[7] == 255u,
                    "buildPremultipliedAtlas should keep fully opaque pixels unchanged.",
                    outFail)) {
            return false;
        }
    }

    {
        const std::vector<unsigned char> primary = {
            10u, 20u, 30u, 255u,   40u, 50u, 60u, 255u
        }; // 2x1
        const std::vector<unsigned char> secondary = {
            70u, 80u, 90u, 255u,
            15u, 25u, 35u, 255u
        }; // 1x2

        RgbaTextureOwned atlas;
        CombinedAtlasInfo info;
        const RgbaTextureView secondaryView{secondary.data(), 1, 2};
        if (!expect(buildCombinedAtlas({primary.data(), 2, 1},
                                       &secondaryView,
                                       atlas,
                                       info),
                    "buildCombinedAtlas should succeed with primary + secondary atlases.",
                    outFail)) {
            return false;
        }
        if (!expect(atlas.width == 5 && atlas.height == 2,
                    "buildCombinedAtlas should pack primary + gutter + secondary into a combined atlas.",
                    outFail)) {
            return false;
        }
        if (!expect(info.hasSecondary,
                    "buildCombinedAtlas should report secondary atlas presence when provided.",
                    outFail)) {
            return false;
        }
        if (!expect(nearf(info.rect0.x, 0.0f) && nearf(info.rect0.y, 0.0f) &&
                        nearf(info.rect0.z, 2.0f / 5.0f) && nearf(info.rect0.w, 1.0f / 2.0f),
                    "buildCombinedAtlas should compute primary atlas rect in normalized coordinates.",
                    outFail)) {
            return false;
        }
        if (!expect(nearf(info.rect1.x, 4.0f / 5.0f) && nearf(info.rect1.y, 0.0f) &&
                        nearf(info.rect1.z, 1.0f / 5.0f) && nearf(info.rect1.w, 1.0f),
                    "buildCombinedAtlas should compute secondary atlas rect after the 2px gutter.",
                    outFail)) {
            return false;
        }

        const auto pixelAt = [&](int x, int y, int channel) -> unsigned char {
            const std::size_t idx =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(atlas.width) +
                 static_cast<std::size_t>(x)) * 4u +
                static_cast<std::size_t>(channel);
            return atlas.rgba[idx];
        };
        if (!expect(pixelAt(0, 0, 0) == 10u && pixelAt(1, 0, 0) == 40u,
                    "buildCombinedAtlas should blit primary pixels at atlas origin.",
                    outFail)) {
            return false;
        }
        if (!expect(pixelAt(4, 0, 0) == 70u && pixelAt(4, 1, 0) == 15u,
                    "buildCombinedAtlas should blit secondary pixels after the gutter.",
                    outFail)) {
            return false;
        }
        if (!expect(pixelAt(2, 0, 0) == 0u && pixelAt(3, 0, 0) == 0u,
                    "buildCombinedAtlas should leave the gutter pixels empty.",
                    outFail)) {
            return false;
        }
    }

    {
        RgbaTextureOwned out;
        if (!expect(!buildPremultipliedAtlas({}, out),
                    "buildPremultipliedAtlas should fail on empty/invalid texture view.",
                    outFail)) {
            return false;
        }
        CombinedAtlasInfo info;
        if (!expect(!buildCombinedAtlas({}, nullptr, out, info),
                    "buildCombinedAtlas should fail on invalid primary atlas.",
                    outFail)) {
            return false;
        }
    }

    return true;
}
