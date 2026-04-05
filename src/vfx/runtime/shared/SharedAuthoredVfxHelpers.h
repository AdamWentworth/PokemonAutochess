#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "vfx/effects/shared/SharedAuthoredBatchVFX.h"

namespace vfx::runtime::authored {

struct TevState {
    glm::vec3 c0{1.0f, 1.0f, 1.0f};
    glm::vec3 c1{0.0f, 0.0f, 0.0f};
    glm::vec3 k0{1.0f, 1.0f, 1.0f};
    float k1a = 1.0f;
};

struct PassTimingPlan {
    bool explicitTimeWindow = false;
    int rawSequenceCount = 1;
    int delayedSequenceIndex = -1;
    bool delayedSinglePass = false;
    bool repeatedSequencePass = false;
    int sequenceLoopCount = 1;
};

struct PassTimingState {
    float globalAge01 = 0.0f;
    float localAge01 = 0.0f;
    float fade = 1.0f;
};

TevState resolveTevState(const SharedAuthoredBatchVFX::Config& config,
                         const SharedAuthoredBatchVFX::Config::DrawPass& pass);
std::uint8_t resolveBlendMode(const SharedAuthoredBatchVFX::Config& config,
                              const SharedAuthoredBatchVFX::Config::DrawPass& pass);

bool isLinePass(const SharedAuthoredBatchVFX::Config& config,
                const SharedAuthoredBatchVFX::Config::DrawPass& pass);
bool isStreakQuadPass(const SharedAuthoredBatchVFX::Config::DrawPass& pass);

bool usesQuarterTextureBake(const SharedAuthoredBatchVFX::Config& config,
                            const SharedAuthoredBatchVFX::Config::DrawPass& pass);

bool isSparkleMeshPass(const SharedAuthoredBatchVFX::Config::DrawPass& pass);
bool isGlowBillboardPass(const SharedAuthoredBatchVFX::Config::DrawPass& pass);

bool isQuarterRingPass(const SharedAuthoredBatchVFX::Config& config,
                       const SharedAuthoredBatchVFX::Config::DrawPass& pass);

std::string makeBakedTextureKey(const SharedAuthoredBatchVFX::Config::DrawPass& pass, bool quarterPass);
std::string makeTextureCacheKey(const SharedAuthoredBatchVFX::Config& config,
                                const SharedAuthoredBatchVFX::Config::DrawPass& pass);

bool bakePassTextureRgba(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                         const TevState& tev,
                         bool quarterPass,
                         const std::vector<unsigned char>& rawRgba,
                         std::vector<unsigned char>& outRgba);

std::vector<glm::vec3> resolveGeneratedDirections(
    const SharedAuthoredBatchVFX::Config::DrawPass& pass);

float quantizeLineVertexAlpha(float srcAlpha, float lineTevK1A, float colorAlpha);

PassTimingPlan planPassTiming(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                              bool allowRepeatedSequence);
bool evaluatePassTiming(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                        float ageSec,
                        float lifeSec,
                        float fadeStart,
                        const PassTimingPlan& plan,
                        int sequenceOrdinal,
                        PassTimingState& outState);

} // namespace vfx::runtime::authored

