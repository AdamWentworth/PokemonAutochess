#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "vfx/effects/growl/GrowlWaveVFX.h"

namespace vfx::runtime::growl {

struct TevState {
    glm::vec3 c0{1.0f, 1.0f, 1.0f};
    glm::vec3 c1{0.0f, 0.0f, 0.0f};
    glm::vec3 k0{1.0f, 1.0f, 1.0f};
    float k1a = 1.0f;
};

TevState resolveTevState(const GrowlWaveVFX::Config& config,
                         const GrowlWaveVFX::Config::DrawPass& pass);
std::uint8_t resolveBlendMode(const GrowlWaveVFX::Config& config,
                              const GrowlWaveVFX::Config::DrawPass& pass);

bool isLinePass(const GrowlWaveVFX::Config& config,
                const GrowlWaveVFX::Config::DrawPass& pass);

bool usesQuarterTextureBake(const GrowlWaveVFX::Config& config,
                            const GrowlWaveVFX::Config::DrawPass& pass);

bool isSparkleMeshPass(const GrowlWaveVFX::Config::DrawPass& pass);
bool isGlowBillboardPass(const GrowlWaveVFX::Config::DrawPass& pass);

bool isQuarterRingPass(const GrowlWaveVFX::Config& config,
                       const GrowlWaveVFX::Config::DrawPass& pass);

std::string makeBakedTextureKey(const GrowlWaveVFX::Config::DrawPass& pass, bool quarterPass);
std::string makeTextureCacheKey(const GrowlWaveVFX::Config& config,
                                const GrowlWaveVFX::Config::DrawPass& pass);

bool bakePassTextureRgba(const GrowlWaveVFX::Config::DrawPass& pass,
                        const TevState& tev,
                        bool quarterPass,
                        const std::vector<unsigned char>& rawRgba,
                        std::vector<unsigned char>& outRgba);

float quantizeLineVertexAlpha(float srcAlpha, float lineTevK1A, float colorAlpha);

} // namespace vfx::runtime::growl

