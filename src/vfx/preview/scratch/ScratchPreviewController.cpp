#include "engine/render/Model.h"
#include "engine/core/Paths.h"
#include "vfx/preview/scratch/ScratchPreviewController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace vfx::preview::scratch {

namespace {

constexpr const char *kScratchManifestRel = "config/vfx/moves/scratch_draw_passes.json";
constexpr std::array<std::array<std::string_view, 12>, 5> kScratchPairPassIds{{
    {{
        "scratch_eid_1192_flash",
        "scratch_eid_1231_mini_marks_base",
        "scratch_frame9740_eid1353_claw_mesh",
        "scratch_frame9740_eid1362_claw_mesh",
        "scratch_frame9740_eid1371_claw_mesh",
        "scratch_eid_1196_texture7567_gold_glow",
        "scratch_frame9740_eid1391_claw_mesh",
        "scratch_frame9740_eid1400_claw_mesh",
        "scratch_eid_1232_texture40750_point",
        "scratch_eid_1233_mini_marks_highlight",
        "scratch_eid_1235_mini_marks_core",
        "",
    }},
    {{
        "scratch_eid_1200_flash",
        "scratch_eid_1204_texture7567_gold_glow",
        "scratch_eid_1240_claw_bundle",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
    }},
    {{
        "scratch_eid_1208_texture40749_flash",
        "scratch_eid_1212_texture7567_gold_glow",
        "scratch_eid_1244_followup_claws",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
    }},
    {{
        "scratch_eid_1216_texture40749_flash",
        "scratch_eid_1220_texture7567_gold_glow",
        "scratch_eid_1252_pair_claws",
        "scratch_eid_1260_single_claw",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
    }},
    {{
        "scratch_eid_1224_followup_flash",
        "scratch_eid_1228_texture7567_gold_glow",
        "scratch_eid_1268_top_triple",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
    }},
}};

constexpr std::array<std::string_view, 5> kScratchRedGlowPassIds{{
    "scratch_eid_1192_flash",
    "scratch_eid_1200_flash",
    "scratch_eid_1208_texture40749_flash",
    "scratch_eid_1216_texture40749_flash",
    "scratch_eid_1224_followup_flash",
}};

constexpr std::array<std::string_view, 5> kScratchGoldGlowPassIds{{
    "scratch_eid_1196_texture7567_gold_glow",
    "scratch_eid_1204_texture7567_gold_glow",
    "scratch_eid_1212_texture7567_gold_glow",
    "scratch_eid_1220_texture7567_gold_glow",
    "scratch_eid_1228_texture7567_gold_glow",
}};

constexpr std::array<std::string_view, 5> kScratchPrimaryClawPassIds{{
    "scratch_eid_1231_mini_marks_base",
    "scratch_eid_1240_claw_bundle",
    "scratch_eid_1244_followup_claws",
    "scratch_eid_1252_pair_claws",
    "scratch_eid_1268_top_triple",
}};

constexpr std::string_view kScratchPointGlowPassId = "scratch_eid_1232_texture40750_point";
constexpr std::string_view kScratchPrimaryFirstClawPassId = "scratch_eid_1231_mini_marks_base";
constexpr std::array<std::string_view, 4> kScratchFrame9740FirstFrameClawPassIds{{
    "scratch_eid_1231_mini_marks_base",
    "scratch_frame9740_eid1353_claw_mesh",
    "scratch_frame9740_eid1362_claw_mesh",
    "scratch_frame9740_eid1371_claw_mesh",
}};

struct ScratchSequenceTuning {
    int pairCount = 1;
    float pairStepSec = 0.055f;
    float glowLeadSec = 0.0f;
    float glowDurationSec = 0.105f;
    float clawLeadSec = 0.010f;
    float clawDurationSec = 0.135f;
    float glowFadeStart = 0.52f;
    float clawFadeStart = 0.70f;
    bool centerAtImpact = true;
    bool soloFirstClawEid1032 = true;
    float redGlowAlphaScale = 0.45f;
    float redGlowScaleMul = 0.86f;
    float goldGlowAlphaScale = 1.0f;
    float goldGlowScaleMul = 1.0f;
    bool primaryClawVsoutShape = true;
    float primaryClawJitterScale = 0.25f;
    float clawBillboardRollDeg = 0.0f;
    float angleJitterDeg = 0.0f;
    std::array<float, 5> pairAnglesDeg{0.0f, 0.0f, 18.0f, -16.0f, 24.0f};
};

SharedAuthoredBatchVFX::Config makeScratchPreviewConfig() {
    SharedAuthoredBatchVFX::Config config{};
    config.spawnForwardOffset = 0.0f;
    config.spawnHeightOffset = 0.0f;
    config.ringForwardOffset = 0.0f;
    config.ringMinSpeed = 0.0f;
    config.ringMaxSpeed = 0.0f;
    config.ringMinLifeSec = 0.45f;
    config.ringMaxLifeSec = 0.45f;
    config.ringMinSize = 1.0f;
    config.ringMaxSize = 1.0f;
    config.ringTrailCount = 0;
    config.ringScaleGrowth = 1.0f;
    config.fadeStart = 0.6f;
    config.vertShaderPath = "assets/shaders/vfx/moves/scratch/scratch_flash_shared.vert";
    config.fragShaderPath = "assets/shaders/vfx/moves/scratch/scratch_flash_shared.frag";
    config.tevC0 = glm::vec3(1.0f);
    config.tevC1 = glm::vec3(0.0f);
    config.tevK0 = glm::vec3(1.0f);
    config.tevC0A = 1.0f;
    config.tevC1A = 0.0f;
    config.tevK1A = 1.0f;
    config.blendMode = 1u;
    config.drawManifestPath = kScratchManifestRel;
    config.drawPasses.clear();
    config.depthTest = true;
    config.depthWrite = false;
    return config;
}

bool idEquals(std::string_view a, std::string_view b) {
    return a == b;
}

bool isPassInIdList(std::string_view id, const auto &list) {
    for (std::string_view candidate : list) {
        if (!candidate.empty() && idEquals(id, candidate)) return true;
    }
    return false;
}

int scratchPairIndexForPass(std::string_view id) {
    for (std::size_t pairIndex = 0; pairIndex < kScratchPairPassIds.size(); ++pairIndex) {
        if (isPassInIdList(id, kScratchPairPassIds[pairIndex])) {
            return static_cast<int>(pairIndex);
        }
    }
    return -1;
}

int scratchPairOrderForPass(std::string_view id) {
    for (const auto &pair : kScratchPairPassIds) {
        for (std::size_t order = 0; order < pair.size(); ++order) {
            if (!pair[order].empty() && idEquals(id, pair[order])) {
                return static_cast<int>(order);
            }
        }
    }
    return 1000;
}

bool isScratchRedGlowPass(std::string_view id) {
    return isPassInIdList(id, kScratchRedGlowPassIds);
}

bool isScratchGoldGlowPass(std::string_view id) {
    return isPassInIdList(id, kScratchGoldGlowPassIds);
}

bool isScratchPrimaryClawPass(std::string_view id) {
    return isPassInIdList(id, kScratchPrimaryClawPassIds);
}

bool isScratchPointGlowPass(std::string_view id) {
    return idEquals(id, kScratchPointGlowPassId);
}

bool isScratchFrame9740FirstFrameClawPass(std::string_view id) {
    return isPassInIdList(id, kScratchFrame9740FirstFrameClawPassIds);
}

bool isScratchClawMarkPass(std::string_view id) {
    const int pairIndex = scratchPairIndexForPass(id);
    return pairIndex >= 0 && !isScratchRedGlowPass(id) && !isScratchGoldGlowPass(id) &&
           !isScratchPointGlowPass(id);
}

float jsonFloat(const nlohmann::json &object, const char *key, float fallback) {
    if (!object.contains(key) || !object[key].is_number()) return fallback;
    return object[key].get<float>();
}

bool jsonBool(const nlohmann::json &object, const char *key, bool fallback) {
    if (!object.contains(key) || !object[key].is_boolean()) return fallback;
    return object[key].get<bool>();
}

ScratchSequenceTuning loadScratchSequenceTuning(const std::string &manifestPath) {
    ScratchSequenceTuning tuning{};
    std::ifstream in(manifestPath);
    if (!in.is_open()) return tuning;

    try {
        nlohmann::json root;
        in >> root;
        if (!root.is_object() || !root.contains("scratch_sequence") ||
            !root["scratch_sequence"].is_object()) {
            return tuning;
        }

        const auto &sequence = root["scratch_sequence"];
        if (sequence.contains("pair_count") && sequence["pair_count"].is_number_integer()) {
            tuning.pairCount = sequence["pair_count"].get<int>();
        }
        tuning.pairStepSec = jsonFloat(sequence, "pair_step_sec", tuning.pairStepSec);
        tuning.glowLeadSec = jsonFloat(sequence, "glow_lead_sec", tuning.glowLeadSec);
        tuning.glowDurationSec = jsonFloat(sequence, "glow_duration_sec", tuning.glowDurationSec);
        tuning.clawLeadSec = jsonFloat(sequence, "claw_lead_sec", tuning.clawLeadSec);
        tuning.clawDurationSec = jsonFloat(sequence, "claw_duration_sec", tuning.clawDurationSec);
        tuning.glowFadeStart = jsonFloat(sequence, "glow_fade_start", tuning.glowFadeStart);
        tuning.clawFadeStart = jsonFloat(sequence, "claw_fade_start", tuning.clawFadeStart);
        tuning.redGlowAlphaScale =
            jsonFloat(sequence, "red_glow_alpha_scale", tuning.redGlowAlphaScale);
        tuning.redGlowScaleMul = jsonFloat(sequence, "red_glow_scale_mul", tuning.redGlowScaleMul);
        tuning.goldGlowAlphaScale =
            jsonFloat(sequence, "gold_glow_alpha_scale", tuning.goldGlowAlphaScale);
        tuning.goldGlowScaleMul =
            jsonFloat(sequence, "gold_glow_scale_mul", tuning.goldGlowScaleMul);
        tuning.primaryClawVsoutShape =
            jsonBool(sequence, "primary_claw_vsout_shape", tuning.primaryClawVsoutShape);
        tuning.primaryClawJitterScale =
            jsonFloat(sequence, "primary_claw_jitter_scale", tuning.primaryClawJitterScale);
        tuning.clawBillboardRollDeg =
            jsonFloat(sequence, "claw_billboard_roll_deg", tuning.clawBillboardRollDeg);
        tuning.angleJitterDeg = jsonFloat(sequence, "angle_jitter_deg", tuning.angleJitterDeg);
        tuning.centerAtImpact = jsonBool(sequence, "center_at_impact", tuning.centerAtImpact);
        tuning.soloFirstClawEid1032 =
            jsonBool(sequence, "solo_first_claw_eid1032", tuning.soloFirstClawEid1032);
        if (sequence.contains("pair_angle_deg") && sequence["pair_angle_deg"].is_array()) {
            const auto &angles = sequence["pair_angle_deg"];
            for (std::size_t i = 0; i < tuning.pairAnglesDeg.size() && i < angles.size(); ++i) {
                if (angles[i].is_number()) {
                    tuning.pairAnglesDeg[i] = angles[i].get<float>();
                }
            }
        }
    } catch (const std::exception &) {
        return ScratchSequenceTuning{};
    }

    tuning.pairCount = std::clamp(
        tuning.pairCount,
        0,
        static_cast<int>(kScratchPairPassIds.size()));
    tuning.pairStepSec = std::max(0.0f, tuning.pairStepSec);
    tuning.glowLeadSec = std::max(0.0f, tuning.glowLeadSec);
    tuning.glowDurationSec = std::max(0.01f, tuning.glowDurationSec);
    tuning.clawLeadSec = std::max(0.0f, tuning.clawLeadSec);
    tuning.clawDurationSec = std::max(0.01f, tuning.clawDurationSec);
    tuning.glowFadeStart = std::clamp(tuning.glowFadeStart, 0.0f, 0.98f);
    tuning.clawFadeStart = std::clamp(tuning.clawFadeStart, 0.0f, 0.98f);
    tuning.redGlowAlphaScale = std::clamp(tuning.redGlowAlphaScale, 0.0f, 2.0f);
    tuning.redGlowScaleMul = std::max(0.0f, tuning.redGlowScaleMul);
    tuning.goldGlowAlphaScale = std::clamp(tuning.goldGlowAlphaScale, 0.0f, 2.0f);
    tuning.goldGlowScaleMul = std::max(0.0f, tuning.goldGlowScaleMul);
    tuning.primaryClawJitterScale = std::clamp(tuning.primaryClawJitterScale, 0.0f, 1.0f);
    tuning.angleJitterDeg = std::max(0.0f, tuning.angleJitterDeg);
    return tuning;
}

SharedAuthoredBatchVFX::Config::AuthoredBillboardInstance makeClawBillboard(
    const glm::vec3 &position,
    float scale,
    float scaleX,
    float scaleY,
    float alpha,
    float spinDeg) {
    SharedAuthoredBatchVFX::Config::AuthoredBillboardInstance billboard{};
    billboard.positionLocal = position;
    billboard.scaleMul = scale;
    billboard.scaleXMul = scaleX;
    billboard.scaleYMul = scaleY;
    billboard.alphaMul = alpha;
    billboard.spinDeg = spinDeg;
    return billboard;
}

void applyPrimaryClawVsoutShape(SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    if (pass.renderMode != "glow_billboard") return;

    pass.authoredBillboardsLocal = {
        makeClawBillboard(glm::vec3(-0.044f, -0.010f, 0.020f), 0.76f, 0.34f, 1.48f, 0.56f, -2.0f),
        makeClawBillboard(glm::vec3(-0.022f, 0.012f, 0.026f), 1.02f, 0.30f, 1.72f, 0.88f, -0.8f),
        makeClawBillboard(glm::vec3(0.000f, 0.024f, 0.030f), 1.16f, 0.28f, 1.88f, 1.00f, 0.0f),
        makeClawBillboard(glm::vec3(0.023f, 0.010f, 0.026f), 1.00f, 0.30f, 1.70f, 0.84f, 1.0f),
        makeClawBillboard(glm::vec3(0.045f, -0.012f, 0.020f), 0.72f, 0.34f, 1.46f, 0.52f, 2.0f),
    };
    pass.authoredBillboardPositionScale = 1.0f;
}

void recenterAuthoredBillboards(SharedAuthoredBatchVFX::Config::DrawPass &pass) {
    if (pass.authoredBillboardsLocal.empty()) return;

    glm::vec3 center(0.0f);
    for (const auto &billboard : pass.authoredBillboardsLocal) {
        center += billboard.positionLocal;
    }
    center /= static_cast<float>(pass.authoredBillboardsLocal.size());

    for (auto &billboard : pass.authoredBillboardsLocal) {
        billboard.positionLocal -= center;
    }
}

void applyScratchSequenceTuning(SharedAuthoredBatchVFX::Config &config,
                                const ScratchSequenceTuning &tuning) {
    float totalLifeSec = 0.0f;
    if (tuning.pairCount > 0) {
        totalLifeSec =
            static_cast<float>(tuning.pairCount - 1) * tuning.pairStepSec +
            std::max(tuning.glowLeadSec + tuning.glowDurationSec,
                     tuning.clawLeadSec + tuning.clawDurationSec) +
            0.08f;
    }
    config.ringMinLifeSec = std::max(config.ringMinLifeSec, totalLifeSec);
    config.ringMaxLifeSec = std::max(config.ringMaxLifeSec, config.ringMinLifeSec);

    for (auto &pass : config.drawPasses) {
        const int pairIndex = scratchPairIndexForPass(pass.id);
        if (pairIndex < 0) continue;

        pass.cameraFacing = true;
        const bool enabledByPairCount = pairIndex < tuning.pairCount;
        pass.enabled = enabledByPairCount;
        if (tuning.soloFirstClawEid1032 && isScratchClawMarkPass(pass.id) &&
            !isScratchFrame9740FirstFrameClawPass(pass.id)) {
            pass.enabled = false;
        }
        if (tuning.soloFirstClawEid1032 && isScratchPointGlowPass(pass.id)) {
            pass.enabled = false;
        }
        pass.sequenceCount = 1;
        pass.sequenceIndex = -1;
        pass.sequenceStep = 0.0f;
        pass.sequenceLife = 1.0f;
        pass.sequenceFadeLocal = false;

        const bool redGlow = isScratchRedGlowPass(pass.id);
        const bool goldGlow = isScratchGoldGlowPass(pass.id);
        const bool pointGlow = isScratchPointGlowPass(pass.id);
        const float pairStartSec = static_cast<float>(pairIndex) * tuning.pairStepSec;
        const bool glowTimedPass = redGlow || goldGlow;
        const float leadSec = glowTimedPass ? tuning.glowLeadSec : tuning.clawLeadSec;
        const float durationSec = glowTimedPass ? tuning.glowDurationSec : tuning.clawDurationSec;
        pass.timeStartSec = pairStartSec + leadSec;
        pass.timeEndSec = pass.timeStartSec + durationSec;
        pass.timeFadeLocal = true;
        pass.timeFadeStart = glowTimedPass ? tuning.glowFadeStart : tuning.clawFadeStart;

        if (redGlow) {
            pass.alphaMul *= tuning.redGlowAlphaScale;
            pass.scaleMul *= tuning.redGlowScaleMul;
        } else if (goldGlow) {
            pass.alphaMul *= tuning.goldGlowAlphaScale;
            pass.scaleMul *= tuning.goldGlowScaleMul;
        }

        if (tuning.primaryClawVsoutShape && isScratchPrimaryClawPass(pass.id)) {
            applyPrimaryClawVsoutShape(pass);
        }

        if (tuning.centerAtImpact) {
            recenterAuthoredBillboards(pass);
        }

        const float pairSpinDeg =
            tuning.clawBillboardRollDeg +
            tuning.pairAnglesDeg[static_cast<std::size_t>(pairIndex)];
        if (isScratchClawMarkPass(pass.id)) {
            pass.billboardSpinStartDeg = pairSpinDeg;
            pass.directionSpacingJitterDeg =
                tuning.angleJitterDeg *
                (isScratchPrimaryClawPass(pass.id) ? tuning.primaryClawJitterScale : 1.0f);
        } else if (pointGlow) {
            pass.billboardSpinStartDeg = pairSpinDeg;
            pass.directionSpacingJitterDeg = tuning.angleJitterDeg * 0.5f;
        }
    }

    std::stable_sort(config.drawPasses.begin(),
                     config.drawPasses.end(),
                     [](const auto &lhs, const auto &rhs) {
                         const int lhsPair = scratchPairIndexForPass(lhs.id);
                         const int rhsPair = scratchPairIndexForPass(rhs.id);
                         if (lhsPair != rhsPair) {
                             if (lhsPair < 0) return false;
                             if (rhsPair < 0) return true;
                             return lhsPair < rhsPair;
                         }
                         return scratchPairOrderForPass(lhs.id) < scratchPairOrderForPass(rhs.id);
                     });
}

} // namespace

ScratchPreviewController::ScratchPreviewController(std::string_view logPrefix)
    : SharedPreviewControllerBase(engine::paths::data(kScratchManifestRel),
                                  "ScratchPreview",
                                  "Scratch",
                                  logPrefix) {
    config_ = makeScratchPreviewConfig();
}

ScratchPreviewController::~ScratchPreviewController() = default;

void ScratchPreviewController::configureEffect() {
    effect_.setConfig(config_);
    SharedAuthoredBatchVFX::Config resolvedConfig = effect_.getConfig();
    applyScratchSequenceTuning(
        resolvedConfig,
        loadScratchSequenceTuning(engine::paths::data(kScratchManifestRel)));
    resolvedConfig.drawManifestPath.clear();
    effect_.setConfig(resolvedConfig);
}

void ScratchPreviewController::emitScene(
    const engine::tools::vfx_preview::PreviewSceneState &scene) {
    const glm::vec3 impactPos = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    effect_.emitFrom(impactPos,
                     vfx::preview::shared::safeForwardXZ(impactPos - scene.emitter),
                     nullptr);
}

void ScratchPreviewController::advanceEffect(float dt) {
    effect_.update(dt);
}

void ScratchPreviewController::renderPreview(
    vfx::preview::authored::SharedAuthoredVfxRenderer &renderer,
    const engine::tools::vfx_preview::PreviewFrameContext &frame) {
    renderer.render(effect_, frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

std::uint32_t ScratchPreviewController::activeEffectCount() const {
    return effect_.activeRingCount();
}

} // namespace vfx::preview::scratch
