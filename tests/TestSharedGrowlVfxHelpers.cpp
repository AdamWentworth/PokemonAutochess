#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"

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

bool test_shared_growl_vfx_helpers_contract(std::string& outFail) {
    using namespace game::runtime::shared_growl;

    GrowlWaveVFX::Config config;
    GrowlWaveVFX::Config::DrawPass pass;

    config.tevC0 = glm::vec3(1.3f, -0.2f, 0.5f);
    config.tevC1 = glm::vec3(-0.1f, 0.7f, 2.1f);
    config.tevK0 = glm::vec3(0.3f, 1.6f, 0.8f);
    config.tevK1A = 1.7f;
    pass.overrideTev = false;
    {
        const TevState tev = resolveTevState(config, pass);
        if (!expect(nearf(tev.c0.r, 1.0f) && nearf(tev.c0.g, 0.0f) && nearf(tev.c0.b, 0.5f),
                    "resolveTevState should clamp config tevC0 when pass does not override.",
                    outFail)) {
            return false;
        }
        if (!expect(nearf(tev.c1.r, 0.0f) && nearf(tev.c1.g, 0.7f) && nearf(tev.c1.b, 1.0f),
                    "resolveTevState should clamp config tevC1 into [0,1].",
                    outFail)) {
            return false;
        }
        if (!expect(nearf(tev.k0.r, 0.3f) && nearf(tev.k0.g, 1.0f) && nearf(tev.k0.b, 0.8f) &&
                        nearf(tev.k1a, 1.0f),
                    "resolveTevState should clamp config tevK0/tevK1A into [0,1].",
                    outFail)) {
            return false;
        }
    }

    pass.overrideTev = true;
    pass.tevC0 = glm::vec3(-1.0f, 0.25f, 2.0f);
    pass.tevC1 = glm::vec3(0.2f, -1.0f, 0.4f);
    pass.tevK0 = glm::vec3(1.2f, 0.4f, -0.2f);
    pass.tevK1A = -0.5f;
    {
        const TevState tev = resolveTevState(config, pass);
        if (!expect(nearf(tev.c0.r, 0.0f) && nearf(tev.c0.g, 0.25f) && nearf(tev.c0.b, 1.0f),
                    "resolveTevState should prefer pass TEV overrides and clamp them.",
                    outFail)) {
            return false;
        }
        if (!expect(nearf(tev.c1.r, 0.2f) && nearf(tev.c1.g, 0.0f) && nearf(tev.c1.b, 0.4f) &&
                        nearf(tev.k0.r, 1.0f) && nearf(tev.k0.g, 0.4f) && nearf(tev.k0.b, 0.0f) &&
                        nearf(tev.k1a, 0.0f),
                    "resolveTevState should clamp overridden tevC1/tevK0/tevK1A.",
                    outFail)) {
            return false;
        }
    }

    config.fragShaderPath = "assets/shaders/VFX/moves/growl/GROWL_LINE_SHARED.frag";
    pass.fragShaderPath.clear();
    pass.textureQuarterRing = false;
    if (!expect(isLinePass(config, pass),
                "isLinePass should detect line passes from config frag shader path (case-insensitive).",
                outFail)) {
        return false;
    }

    config.fragShaderPath = "assets/shaders/vfx/moves/growl/growl_ring_shared.frag";
    pass.fragShaderPath = "assets/shaders/vfx/moves/growl/growl_quarter_ring_shared.frag";
    if (!expect(isQuarterRingPass(config, pass),
                "isQuarterRingPass should detect quarter-ring passes from pass frag shader path.",
                outFail)) {
        return false;
    }
    pass.fragShaderPath.clear();
    pass.textureQuarterRing = true;
    if (!expect(isQuarterRingPass(config, pass),
                "isQuarterRingPass should respect explicit textureQuarterRing pass flag.",
                outFail)) {
        return false;
    }

    pass.id = "growl_eid_1076";
    pass.texturePath = "assets/textures/moves/growl/Texture3918.png";
    const std::string meshKey = makeBakedTextureKey(pass, false);
    const std::string quarterKey = makeBakedTextureKey(pass, true);
    if (!expect(meshKey == "__growl_baked:growl_eid_1076:m:assets/textures/moves/growl/Texture3918.png",
                "makeBakedTextureKey should produce stable mesh-pass cache keys.",
                outFail)) {
        return false;
    }
    if (!expect(quarterKey == "__growl_baked:growl_eid_1076:q:assets/textures/moves/growl/Texture3918.png",
                "makeBakedTextureKey should produce stable quarter-pass cache keys.",
                outFail)) {
        return false;
    }

    TevState tev{};
    tev.c0 = glm::vec3(0.9f, 0.8f, 0.7f);
    tev.c1 = glm::vec3(0.1f, 0.2f, 0.3f);
    tev.k0 = glm::vec3(0.6f, 0.5f, 0.4f);
    tev.k1a = 0.5f;
    pass.tintColor = glm::vec3(0.8f, 0.7f, 0.6f);
    pass.useAlphaMaskForColor = true;

    std::vector<unsigned char> outRgba;
    if (!expect(!bakePassTextureRgba(pass, tev, false, {1u, 2u, 3u}, outRgba),
                "bakePassTextureRgba should fail cleanly on non-RGBA-aligned source data.",
                outFail)) {
        return false;
    }

    const std::vector<unsigned char> rawRgba = {
        128u,  64u, 255u, 192u,
         16u, 240u,  32u, 128u
    };
    if (!expect(bakePassTextureRgba(pass, tev, false, rawRgba, outRgba) &&
                    outRgba.size() == rawRgba.size(),
                "bakePassTextureRgba should preserve source pixel count for mesh passes.",
                outFail)) {
        return false;
    }
    if (!expect(outRgba != rawRgba,
                "bakePassTextureRgba should transform pixel colors/alpha for mesh passes.",
                outFail)) {
        return false;
    }

    std::vector<unsigned char> quarterOut;
    if (!expect(bakePassTextureRgba(pass, tev, true, rawRgba, quarterOut) &&
                    quarterOut.size() == rawRgba.size(),
                "bakePassTextureRgba should preserve source pixel count for quarter-ring passes.",
                outFail)) {
        return false;
    }
    if (!expect(quarterOut[3] != rawRgba[3],
                "Quarter-ring pass bake should quantize/adjust alpha relative to source alpha.",
                outFail)) {
        return false;
    }

    if (!expect(nearf(quantizeLineVertexAlpha(0.0f, 1.0f, 1.0f), 0.0f) &&
                    nearf(quantizeLineVertexAlpha(1.0f, 1.0f, 1.0f), 1.0f),
                "quantizeLineVertexAlpha should clamp to [0,1] endpoints for canonical inputs.",
                outFail)) {
        return false;
    }
    const float q = quantizeLineVertexAlpha(0.45f, 0.75f, 0.8f);
    if (!expect(q >= 0.0f && q <= 1.0f && nearf(q, quantizeLineVertexAlpha(0.45f, 0.75f, 0.8f)),
                "quantizeLineVertexAlpha should be deterministic and clamped for mid-range inputs.",
                outFail)) {
        return false;
    }

    return true;
}
