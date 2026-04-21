#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

namespace {

bool expect(bool condition, const std::string &message, std::string &outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_shared_authored_vfx_helpers_contract(std::string &outFail) {
    using namespace vfx::runtime::authored;

    SharedAuthoredBatchVFX::Config config;
    SharedAuthoredBatchVFX::Config::DrawPass pass;

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
    pass.renderMode = "streak_quad";
    pass.fragShaderPath = "assets/shaders/vfx/shared/authored_line_shared.frag";
    if (!expect(isLinePass(config, pass) && isStreakQuadPass(pass),
                "Shared authored helpers should treat streak_quad passes as shared line-style passes.",
                outFail)) {
        return false;
    }
    pass.generatedDirectionCount = 4;
    pass.generatedDirectionStartDeg = 0.0f;
    pass.generatedDirectionArcDeg = 360.0f;
    pass.generatedDirectionForward = 0.0f;
    const auto generatedDirections = resolveGeneratedDirections(pass);
    if (!expect(generatedDirections.size() == 4u,
                "resolveGeneratedDirections should synthesize one circular direction per generated streak burst count.",
                outFail)) {
        return false;
    }
    pass.generatedDirectionMode = "sphere";
    pass.generatedDirectionCount = 6;
    pass.generatedDirectionStartDeg = 0.0f;
    pass.generatedDirectionArcDeg = 360.0f;
    pass.generatedDirectionForward = 0.0f;
    const auto sphereDirections = resolveGeneratedDirections(pass);
    if (!expect(sphereDirections.size() == 6u,
                "resolveGeneratedDirections should synthesize one spherical direction per generated streak burst count.",
                outFail)) {
        return false;
    }
    bool foundPositiveY = false;
    bool foundNegativeY = false;
    for (const glm::vec3 &dir : sphereDirections) {
        foundPositiveY = foundPositiveY || dir.y > 0.15f;
        foundNegativeY = foundNegativeY || dir.y < -0.15f;
    }
    if (!expect(foundPositiveY && foundNegativeY,
                "Spherical generated directions should fan streaks above and below the impact plane instead of staying flat.",
                outFail)) {
        return false;
    }
    pass.renderMode.clear();
    pass.fragShaderPath.clear();
    pass.generatedDirectionMode = "circle";
    pass.generatedDirectionCount = 0;

    config.fragShaderPath = "assets/shaders/vfx/moves/growl/growl_ring_shared.frag";
    pass.fragShaderPath = "assets/shaders/vfx/moves/growl/growl_quarter_ring_shared.frag";
    pass.textureQuarterRing = false;
    pass.renderMode.clear();
    if (!expect(usesQuarterTextureBake(config, pass),
                "usesQuarterTextureBake should detect quarter-style texture baking from pass frag shader path.",
                outFail)) {
        return false;
    }
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
    pass.renderMode = "sparkle_mesh";
    if (!expect(isSparkleMeshPass(pass),
                "isSparkleMeshPass should detect explicit sparkle mesh render mode.",
                outFail)) {
        return false;
    }
    pass.renderMode = "mesh_corner_billboards";
    if (!expect(isMeshCornerBillboardPass(pass),
                "isMeshCornerBillboardPass should detect mesh-driven corner billboard render mode.",
                outFail)) {
        return false;
    }
    pass.renderMode = "glow_billboard";
    if (!expect(isGlowBillboardPass(pass),
                "isGlowBillboardPass should detect explicit glow billboard render mode.",
                outFail)) {
        return false;
    }
    pass.renderMode = "mesh";
    if (!expect(!isSparkleMeshPass(pass),
                "isSparkleMeshPass should ignore non-sparkle render modes.",
                outFail)) {
        return false;
    }
    if (!expect(!isMeshCornerBillboardPass(pass),
                "isMeshCornerBillboardPass should ignore non-corner render modes.",
                outFail)) {
        return false;
    }
    if (!expect(!isGlowBillboardPass(pass),
                "isGlowBillboardPass should ignore non-glow render modes.",
                outFail)) {
        return false;
    }

    pass.id = "growl_eid_1076";
    pass.texturePath = "assets/textures/moves/growl/Texture3918.png";
    const std::string meshKey = makeBakedTextureKey(pass, false);
    const std::string quarterKey = makeBakedTextureKey(pass, true);
    if (!expect(meshKey == "__authored_vfx_baked:growl_eid_1076:m:assets/textures/moves/growl/Texture3918.png:bake=tev_lerp:alpha=texture",
                "makeBakedTextureKey should produce stable mesh-pass cache keys.",
                outFail)) {
        return false;
    }
    if (!expect(quarterKey == "__authored_vfx_baked:growl_eid_1076:q:assets/textures/moves/growl/Texture3918.png:bake=tev_lerp:alpha=texture",
                "makeBakedTextureKey should produce stable quarter-pass cache keys.",
                outFail)) {
        return false;
    }

    pass.fragShaderPath = "assets/shaders/vfx/moves/growl/growl_line_shared.frag";
    pass.texturePath.clear();
    if (!expect(makeTextureCacheKey(config, pass) == "__authored_vfx_white__",
                "makeTextureCacheKey should share a single white cache entry for line/white growl passes.",
                outFail)) {
        return false;
    }

    pass.fragShaderPath = "assets/shaders/vfx/moves/growl/growl_quarter_ring_shared.frag";
    pass.texturePath = "assets/textures/moves/growl/Texture3918.png";
    pass.renderMode = "mesh";
    pass.textureQuarterRing = false;
    if (!expect(makeTextureCacheKey(config, pass) == quarterKey,
                "makeTextureCacheKey should reuse quarter-style baked-texture cache keys even for mesh passes using the quarter-ring shader.",
                outFail)) {
        return false;
    }

    TevState tev{};
    tev.c0 = glm::vec3(0.9f, 0.8f, 0.7f);
    tev.c1 = glm::vec3(0.1f, 0.2f, 0.3f);
    tev.k0 = glm::vec3(0.6f, 0.5f, 0.4f);
    tev.c0a = 0.82f;
    tev.c1a = 0.18f;
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
        128u, 64u, 255u, 192u,
        16u, 240u, 32u, 128u};
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

    SharedAuthoredBatchVFX::Config::DrawPass modulatePass = pass;
    modulatePass.textureBakeMode = "modulate_c0";
    modulatePass.textureAlphaMode = "one";
    modulatePass.tintColor = glm::vec3(1.0f);
    TevState modulateTev{};
    modulateTev.c0 = glm::vec3(0.5f, 0.25f, 0.125f);
    std::vector<unsigned char> modulateOut;
    if (!expect(bakePassTextureRgba(modulatePass, modulateTev, false, rawRgba, modulateOut) &&
                    modulateOut.size() == rawRgba.size(),
                "Modulate-c0 texture bake should preserve source pixel count for mesh passes.",
                outFail)) {
        return false;
    }
    if (!expect(modulateOut[0] == 64u &&
                    modulateOut[1] == 16u &&
                    modulateOut[2] == 32u &&
                    modulateOut[3] == 255u,
                "Modulate-c0 texture bake should multiply texture RGB by c0 and let vertex alpha own opacity.",
                outFail)) {
        return false;
    }

    SharedAuthoredBatchVFX::Config::DrawPass capturePass = pass;
    capturePass.textureBakeMode = "capture_lerp";
    capturePass.textureAlphaMode = "tev";
    capturePass.tintColor = glm::vec3(1.0f);
    capturePass.useAlphaMaskForColor = false;
    TevState captureTev{};
    captureTev.c0 = glm::vec3(0.9f, 0.7f, 0.7f);
    captureTev.c1 = glm::vec3(0.5f, 0.0f, 0.0f);
    captureTev.c0a = 32.0f / 255.0f;
    captureTev.c1a = 0.0f;
    std::vector<unsigned char> captureOut;
    if (!expect(bakePassTextureRgba(capturePass, captureTev, false, rawRgba, captureOut) &&
                    captureOut.size() == rawRgba.size(),
                "Capture-lerp texture bake should preserve source pixel count for mesh passes.",
                outFail)) {
        return false;
    }
    if (!expect(captureOut[3] > 0u && captureOut[3] <= 32u &&
                    captureOut[3] < rawRgba[3],
                "Capture-lerp texture bake should preserve the TEV-computed alpha instead of falling back to raw texture alpha.",
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

    pass.renderMode = "sparkle_mesh";
    std::vector<unsigned char> sparkleOut;
    if (!expect(bakePassTextureRgba(pass, tev, true, rawRgba, sparkleOut) &&
                    sparkleOut.size() == rawRgba.size(),
                "Sparkle mesh bake should preserve source pixel count for shared quarter-shader passes.",
                outFail)) {
        return false;
    }
    if (!expect(sparkleOut[3] > quarterOut[3],
                "Sparkle mesh bake should keep fuller alpha than the generic quarter-ring bake.",
                outFail)) {
        return false;
    }

    pass.renderMode = "glow_billboard";
    std::vector<unsigned char> glowOut;
    if (!expect(bakePassTextureRgba(pass, tev, true, rawRgba, glowOut) &&
                    glowOut.size() == rawRgba.size(),
                "Glow billboard bake should preserve source pixel count for no-mesh quarter-shader passes.",
                outFail)) {
        return false;
    }
    if (!expect(glowOut[3] > quarterOut[3] && glowOut[3] <= rawRgba[3],
                "Glow billboard bake should preserve softer authored alpha than the generic quarter-ring bake without over-boosting beyond source alpha.",
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

    SharedAuthoredBatchVFX::Config::DrawPass scalePass = pass;
    scalePass.timeStartSec = 0.0f;
    scalePass.timeEndSec = 50.0f / 30.0f;
    scalePass.passScaleFps = 30.0f;
    scalePass.passScaleUseGlobalTime = false;
    SharedAuthoredBatchVFX::Config::PassScaleFrame scaleFrame0;
    scaleFrame0.frameIndex = 0;
    scaleFrame0.scaleMul = glm::vec3(1.0f, 0.03f, 1.0f);
    SharedAuthoredBatchVFX::Config::PassScaleFrame scaleFrame2;
    scaleFrame2.frameIndex = 2;
    scaleFrame2.scaleMul = glm::vec3(1.0f, 0.14f, 1.0f);
    SharedAuthoredBatchVFX::Config::PassScaleFrame scaleFrame5;
    scaleFrame5.frameIndex = 5;
    scaleFrame5.scaleMul = glm::vec3(1.0f, 1.0f, 1.0f);
    scalePass.passScaleFrames = {scaleFrame0, scaleFrame2, scaleFrame5};
    const glm::vec3 startScale = resolvePassAnimatedScaleMul(scalePass, 0.0f);
    if (!expect(nearf(startScale.x, 1.0f) && nearf(startScale.y, 0.03f) && nearf(startScale.z, 1.0f),
                "resolvePassAnimatedScaleMul should honor the first authored frame at effect start.",
                outFail)) {
        return false;
    }
    const glm::vec3 midwayScale = resolvePassAnimatedScaleMul(scalePass, 1.0f / 30.0f);
    if (!expect(nearf(midwayScale.y, 0.085f, 0.0002f),
                "resolvePassAnimatedScaleMul should linearly interpolate authored per-frame scale values.",
                outFail)) {
        return false;
    }
    const glm::vec3 capturedScale = resolvePassAnimatedScaleMul(scalePass, 5.0f / 30.0f);
    if (!expect(nearf(capturedScale.x, 1.0f) && nearf(capturedScale.y, 1.0f) && nearf(capturedScale.z, 1.0f),
                "resolvePassAnimatedScaleMul should preserve the captured frame-5 baseline when authored that way.",
                outFail)) {
        return false;
    }

    SharedAuthoredBatchVFX::Config::DrawPass offsetPass = pass;
    offsetPass.timeStartSec = 0.0f;
    offsetPass.timeEndSec = 51.0f / 30.0f;
    offsetPass.passPositionOffsetFps = 30.0f;
    offsetPass.passPositionOffsetUseGlobalTime = true;
    offsetPass.passMeshOffsetFps = 30.0f;
    offsetPass.passMeshOffsetUseGlobalTime = true;
    SharedAuthoredBatchVFX::Config::PassOffsetFrame offsetFrame0;
    offsetFrame0.frameIndex = 0;
    offsetFrame0.offset = glm::vec3(-0.2f, 0.5f, 0.0f);
    SharedAuthoredBatchVFX::Config::PassOffsetFrame offsetFrame10;
    offsetFrame10.frameIndex = 10;
    offsetFrame10.offset = glm::vec3(0.3f, -0.25f, 0.4f);
    offsetPass.passPositionOffsetFrames = {offsetFrame0, offsetFrame10};
    offsetPass.passMeshOffsetFrames = {offsetFrame0, offsetFrame10};
    const glm::vec3 startPositionOffset = resolvePassAnimatedPositionLocalOffset(offsetPass, 0.0f);
    if (!expect(nearf(startPositionOffset.x, -0.2f) && nearf(startPositionOffset.y, 0.5f) &&
                    nearf(startPositionOffset.z, 0.0f),
                "resolvePassAnimatedPositionLocalOffset should honor the first authored frame at effect start.",
                outFail)) {
        return false;
    }
    const glm::vec3 midPositionOffset =
        resolvePassAnimatedPositionLocalOffset(offsetPass, 5.0f / 30.0f);
    if (!expect(nearf(midPositionOffset.x, 0.05f, 0.0002f) &&
                    nearf(midPositionOffset.y, 0.125f, 0.0002f) &&
                    nearf(midPositionOffset.z, 0.2f, 0.0002f),
                "resolvePassAnimatedPositionLocalOffset should linearly interpolate authored per-frame offsets.",
                outFail)) {
        return false;
    }
    const glm::vec3 endMeshOffset = resolvePassAnimatedMeshLocalOffset(offsetPass, 10.0f / 30.0f);
    if (!expect(nearf(endMeshOffset.x, 0.3f) && nearf(endMeshOffset.y, -0.25f) &&
                    nearf(endMeshOffset.z, 0.4f),
                "resolvePassAnimatedMeshLocalOffset should preserve the last authored frame once the sampled frame reaches it.",
                outFail)) {
        return false;
    }

    SharedAuthoredBatchVFX::Config::DrawPass rotationPass = pass;
    rotationPass.passMeshRotationFps = 30.0f;
    rotationPass.passMeshRotationUseGlobalTime = true;
    SharedAuthoredBatchVFX::Config::PassRotationFrame rotationFrame0;
    rotationFrame0.frameIndex = 0;
    rotationFrame0.rotationQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    SharedAuthoredBatchVFX::Config::PassRotationFrame rotationFrame10;
    rotationFrame10.frameIndex = 10;
    rotationFrame10.rotationQuat =
        glm::normalize(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
    rotationPass.passMeshRotationFrames = {rotationFrame0, rotationFrame10};
    const glm::quat midRotation = resolvePassAnimatedMeshRotationQuat(rotationPass, 5.0f / 30.0f);
    const glm::vec3 rotatedX = midRotation * glm::vec3(1.0f, 0.0f, 0.0f);
    if (!expect(nearf(rotatedX.x, std::sqrt(0.5f), 0.0003f) &&
                    nearf(rotatedX.y, std::sqrt(0.5f), 0.0003f) &&
                    nearf(rotatedX.z, 0.0f, 0.0003f),
                "resolvePassAnimatedMeshRotationQuat should slerp authored mesh rotations across sampled frames.",
                outFail)) {
        return false;
    }

    SharedAuthoredBatchVFX::Config::DrawPass uvPass = pass;
    uvPass.timeStartSec = 0.0f;
    uvPass.timeEndSec = 50.0f / 30.0f;
    uvPass.passUvScaleFps = 30.0f;
    uvPass.passUvScaleUseGlobalTime = true;
    uvPass.passUvOffsetFps = 30.0f;
    uvPass.passUvOffsetUseGlobalTime = true;
    SharedAuthoredBatchVFX::Config::PassUvScaleFrame uvScaleFrame5;
    uvScaleFrame5.frameIndex = 5;
    uvScaleFrame5.uvScale = glm::vec2(1.826087f, 1.826087f);
    SharedAuthoredBatchVFX::Config::PassUvScaleFrame uvScaleFrame14;
    uvScaleFrame14.frameIndex = 14;
    uvScaleFrame14.uvScale = glm::vec2(1.615385f, 1.615385f);
    SharedAuthoredBatchVFX::Config::PassUvOffsetFrame uvOffsetFrame5;
    uvOffsetFrame5.frameIndex = 5;
    uvOffsetFrame5.uvOffset = glm::vec2(-0.913043f, -1.278227f);
    SharedAuthoredBatchVFX::Config::PassUvOffsetFrame uvOffsetFrame14;
    uvOffsetFrame14.frameIndex = 14;
    uvOffsetFrame14.uvOffset = glm::vec2(-0.807692f, -1.130740f);
    uvPass.passUvScaleFrames = {uvScaleFrame5, uvScaleFrame14};
    uvPass.passUvOffsetFrames = {uvOffsetFrame5, uvOffsetFrame14};
    const glm::vec2 startUvScale = resolvePassAnimatedUvScale(uvPass, 5.0f / 30.0f);
    if (!expect(nearf(startUvScale.x, 1.826087f, 0.0002f) &&
                    nearf(startUvScale.y, 1.826087f, 0.0002f),
                "resolvePassAnimatedUvScale should honor the source-backed Leer frame-5 UV scale key.",
                outFail)) {
        return false;
    }
    const glm::vec2 midUvScale = resolvePassAnimatedUvScale(uvPass, 9.5f / 30.0f);
    if (!expect(nearf(midUvScale.x, (1.826087f + 1.615385f) * 0.5f, 0.0003f),
                "resolvePassAnimatedUvScale should linearly interpolate source-backed per-frame UV scale values.",
                outFail)) {
        return false;
    }
    const glm::vec2 endUvOffset = resolvePassAnimatedUvOffset(uvPass, 14.0f / 30.0f);
    if (!expect(nearf(endUvOffset.x, -0.807692f, 0.0002f) &&
                    nearf(endUvOffset.y, -1.130740f, 0.0002f),
                "resolvePassAnimatedUvOffset should preserve the last authored UV drift key once the sampled frame reaches it.",
                outFail)) {
        return false;
    }

    SharedAuthoredBatchVFX::Config::DrawPass alphaPass = pass;
    alphaPass.passAlphaFps = 30.0f;
    alphaPass.passAlphaUseGlobalTime = true;
    SharedAuthoredBatchVFX::Config::PassAlphaFrame alphaFrame0;
    alphaFrame0.frameIndex = 0;
    alphaFrame0.alphaMul = 1.0f;
    SharedAuthoredBatchVFX::Config::PassAlphaFrame alphaFrame40;
    alphaFrame40.frameIndex = 40;
    alphaFrame40.alphaMul = 1.0f;
    SharedAuthoredBatchVFX::Config::PassAlphaFrame alphaFrame50;
    alphaFrame50.frameIndex = 50;
    alphaFrame50.alphaMul = 0.5f;
    SharedAuthoredBatchVFX::Config::PassAlphaFrame alphaFrame51;
    alphaFrame51.frameIndex = 51;
    alphaFrame51.alphaMul = 0.0f;
    alphaPass.passAlphaFrames = {alphaFrame0, alphaFrame40, alphaFrame50, alphaFrame51};
    if (!expect(nearf(resolvePassAnimatedAlphaMul(alphaPass, 40.0f / 30.0f), 1.0f),
                "resolvePassAnimatedAlphaMul should hold full opacity through frame 40 for Leer's tail fade.",
                outFail)) {
        return false;
    }
    if (!expect(nearf(resolvePassAnimatedAlphaMul(alphaPass, 45.0f / 30.0f), 0.75f, 0.0002f),
                "resolvePassAnimatedAlphaMul should linearly fade Leer toward half opacity between frames 40 and 50.",
                outFail)) {
        return false;
    }
    if (!expect(nearf(resolvePassAnimatedAlphaMul(alphaPass, 50.0f / 30.0f), 0.5f),
                "resolvePassAnimatedAlphaMul should leave frame 50 at half opacity for Leer's final visible sample.",
                outFail)) {
        return false;
    }
    if (!expect(nearf(resolvePassAnimatedAlphaMul(alphaPass, 51.0f / 30.0f), 0.0f),
                "resolvePassAnimatedAlphaMul should drop Leer fully invisible on cleanup frame 51.",
                outFail)) {
        return false;
    }

    return true;
}
