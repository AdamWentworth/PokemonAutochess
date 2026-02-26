#include "game/runtime/SharedGrowlVfxHelpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace game::runtime::shared_growl {
namespace {

std::string toLowerCopyLocal(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

std::uint8_t u8From01(float v) {
    return static_cast<std::uint8_t>(std::clamp<int>(
        static_cast<int>(std::lround(clamp01(v) * 255.0f)), 0, 255));
}

float tevMixU8Scalar(float a, float b, float t) {
    const float a8 = std::floor(clamp01(a) * 255.0f + 0.5f);
    const float b8 = std::floor(clamp01(b) * 255.0f + 0.5f);
    const float t8 = std::floor(clamp01(t) * 255.0f + 0.5f);
    const float tc = t8 + std::floor(t8 / 128.0f);
    const float out8 = std::floor((a8 * 256.0f + (b8 - a8) * tc + 128.0f) / 256.0f);
    return clamp01(out8 / 255.0f);
}

float alpha6bit(float a) {
    return clamp01(std::floor(clamp01(a) * 63.0f + 0.5f) / 63.0f);
}

std::string effectiveFragPath(const GrowlWaveVFX::Config& config,
                              const GrowlWaveVFX::Config::DrawPass& pass) {
    return toLowerCopyLocal(pass.fragShaderPath.empty() ? config.fragShaderPath : pass.fragShaderPath);
}

} // namespace

TevState resolveTevState(const GrowlWaveVFX::Config& config,
                         const GrowlWaveVFX::Config::DrawPass& pass) {
    TevState tev;
    tev.c0 = pass.overrideTev ? pass.tevC0 : config.tevC0;
    tev.c1 = pass.overrideTev ? pass.tevC1 : config.tevC1;
    tev.k0 = pass.overrideTev ? pass.tevK0 : config.tevK0;
    tev.k1a = pass.overrideTev ? pass.tevK1A : config.tevK1A;
    tev.c0 = glm::clamp(tev.c0, glm::vec3(0.0f), glm::vec3(1.0f));
    tev.c1 = glm::clamp(tev.c1, glm::vec3(0.0f), glm::vec3(1.0f));
    tev.k0 = glm::clamp(tev.k0, glm::vec3(0.0f), glm::vec3(1.0f));
    tev.k1a = clamp01(tev.k1a);
    return tev;
}

bool isLinePass(const GrowlWaveVFX::Config& config,
                const GrowlWaveVFX::Config::DrawPass& pass) {
    return effectiveFragPath(config, pass).find("growl_line_shared") != std::string::npos;
}

bool isQuarterRingPass(const GrowlWaveVFX::Config& config,
                       const GrowlWaveVFX::Config::DrawPass& pass) {
    if (pass.textureQuarterRing) return true;
    return effectiveFragPath(config, pass).find("growl_quarter_ring_shared") != std::string::npos;
}

std::string makeBakedTextureKey(const GrowlWaveVFX::Config::DrawPass& pass, bool quarterPass) {
    return std::string("__growl_baked:") + pass.id + ":" +
           (quarterPass ? "q:" : "m:") +
           (pass.texturePath.empty() ? std::string("__white__") : pass.texturePath);
}

bool bakePassTextureRgba(const GrowlWaveVFX::Config::DrawPass& pass,
                        const TevState& tev,
                        bool quarterPass,
                        const std::vector<unsigned char>& rawRgba,
                        std::vector<unsigned char>& outRgba) {
    if (rawRgba.empty() || (rawRgba.size() % 4u) != 0u) {
        outRgba.clear();
        return false;
    }
    outRgba.clear();
    outRgba.resize(rawRgba.size(), 0u);

    const glm::vec3 tint = glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f));
    for (std::size_t i = 0; i + 3u < rawRgba.size(); i += 4u) {
        const float tr = static_cast<float>(rawRgba[i + 0u]) / 255.0f;
        const float tg = static_cast<float>(rawRgba[i + 1u]) / 255.0f;
        const float tb = static_cast<float>(rawRgba[i + 2u]) / 255.0f;
        const float ta = static_cast<float>(rawRgba[i + 3u]) / 255.0f;

        glm::vec3 rgb(1.0f);
        float alpha = ta;
        if (quarterPass) {
            rgb = glm::vec3(
                tevMixU8Scalar(tev.c1.r, tev.c0.r, tr),
                tevMixU8Scalar(tev.c1.g, tev.c0.g, tg),
                tevMixU8Scalar(tev.c1.b, tev.c0.b, tb));
            rgb *= tint;
            alpha = alpha6bit(ta * tev.k1a);
        } else {
            const glm::vec3 tevInput = pass.useAlphaMaskForColor
                ? glm::vec3(ta, ta, ta)
                : glm::vec3(tr, tg, tb);
            const glm::vec3 stage1 = glm::mix(tev.c1, tev.k0, tevInput);
            rgb = tint * (tev.c0 * stage1);
            alpha = ta;
        }

        rgb = glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f));
        outRgba[i + 0u] = u8From01(rgb.r);
        outRgba[i + 1u] = u8From01(rgb.g);
        outRgba[i + 2u] = u8From01(rgb.b);
        outRgba[i + 3u] = u8From01(alpha);
    }

    return true;
}

float quantizeLineVertexAlpha(float srcAlpha, float lineTevK1A, float colorAlpha) {
    const float alpha255 =
        std::clamp(clamp01(srcAlpha) * clamp01(lineTevK1A) * 255.0f, 0.0f, 255.0f);
    const float quantized = std::floor(alpha255 * 0.25f) / 63.0f;
    return std::clamp(clamp01(colorAlpha) * quantized, 0.0f, 1.0f);
}

} // namespace game::runtime::shared_growl
