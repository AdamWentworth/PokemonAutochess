#include "engine/render/Model.h"
#include "engine/core/Paths.h"
#include "vfx/preview/scratch/ScratchPreviewController.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace vfx::preview::scratch {

namespace {

constexpr const char *kScratchManifestRel = "config/vfx/moves/scratch_draw_passes.json";
constexpr const char *kScratchShapeOverridesRel = "config/vfx/moves/scratch_shape_overrides.json";
struct ScratchPairPassIds {
    std::string_view redGlow;
    std::string_view goldGlow;
    std::string_view pointGlow;
    std::array<std::string_view, 3> claws;
};

constexpr std::array<ScratchPairPassIds, 5> kScratchPairs{{
    {
        "scratch_eid_1192_flash",
        "scratch_eid_1196_texture7567_gold_glow",
        "scratch_eid_1232_texture40750_point",
        {{
            "scratch_eid_1231_mini_marks_base",
            "scratch_eid_1233_mini_marks_highlight",
            "scratch_eid_1235_mini_marks_core",
        }},
    },
    {
        "scratch_eid_1200_flash",
        "scratch_eid_1204_texture7567_gold_glow",
        "",
        {{
            "scratch_eid_1240_claw_bundle",
            "",
            "",
        }},
    },
    {
        "scratch_eid_1208_texture40749_flash",
        "scratch_eid_1212_texture7567_gold_glow",
        "",
        {{
            "scratch_eid_1244_followup_claws",
            "",
            "",
        }},
    },
    {
        "scratch_eid_1216_texture40749_flash",
        "scratch_eid_1220_texture7567_gold_glow",
        "",
        {{
            "scratch_eid_1252_pair_claws",
            "scratch_eid_1260_single_claw",
            "",
        }},
    },
    {
        "scratch_eid_1224_followup_flash",
        "scratch_eid_1228_texture7567_gold_glow",
        "",
        {{
            "scratch_eid_1268_top_triple",
            "",
            "",
        }},
    },
}};

constexpr std::array<std::string_view, 5> kScratchPrimaryClawPassIds{{
    "scratch_eid_1231_mini_marks_base",
    "scratch_eid_1240_claw_bundle",
    "scratch_eid_1244_followup_claws",
    "scratch_eid_1252_pair_claws",
    "scratch_eid_1268_top_triple",
}};

constexpr std::string_view kScratchFirstClawEid1032PassId = "scratch_eid_1231_mini_marks_base";

enum class ScratchPassRole {
    None,
    RedGlow,
    GoldGlow,
    PointGlow,
    Claw,
};

struct ScratchPassInfo {
    int pairIndex = -1;
    int pairOrder = 1000;
    ScratchPassRole role = ScratchPassRole::None;
    bool primaryClaw = false;

    bool inSequence() const {
        return pairIndex >= 0;
    }

    bool isGlow() const {
        return role == ScratchPassRole::RedGlow || role == ScratchPassRole::GoldGlow;
    }

    bool isClaw() const {
        return role == ScratchPassRole::Claw;
    }
};

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

struct ScratchBillboardShapeOverride {
    float positionScale = 1.0f;
    std::vector<SharedAuthoredBatchVFX::Config::AuthoredBillboardInstance> billboards;

    bool hasBillboards() const {
        return !billboards.empty();
    }
};

struct ScratchShapeOverrides {
    ScratchBillboardShapeOverride primaryClawVsout;
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

ScratchPassInfo describeScratchPass(std::string_view id) {
    ScratchPassInfo info{};
    info.primaryClaw = isPassInIdList(id, kScratchPrimaryClawPassIds);

    for (std::size_t pairIndex = 0; pairIndex < kScratchPairs.size(); ++pairIndex) {
        const ScratchPairPassIds &pair = kScratchPairs[pairIndex];
        if (idEquals(id, pair.redGlow)) {
            info.pairIndex = static_cast<int>(pairIndex);
            info.pairOrder = 0;
            info.role = ScratchPassRole::RedGlow;
            return info;
        }
        if (idEquals(id, pair.goldGlow)) {
            info.pairIndex = static_cast<int>(pairIndex);
            info.pairOrder = 1;
            info.role = ScratchPassRole::GoldGlow;
            return info;
        }
        if (!pair.pointGlow.empty() && idEquals(id, pair.pointGlow)) {
            info.pairIndex = static_cast<int>(pairIndex);
            info.pairOrder = 2;
            info.role = ScratchPassRole::PointGlow;
            return info;
        }
        for (std::size_t clawIndex = 0; clawIndex < pair.claws.size(); ++clawIndex) {
            if (!pair.claws[clawIndex].empty() && idEquals(id, pair.claws[clawIndex])) {
                info.pairIndex = static_cast<int>(pairIndex);
                info.pairOrder = static_cast<int>(3 + clawIndex);
                info.role = ScratchPassRole::Claw;
                return info;
            }
        }
    }
    return info;
}

float jsonFloat(const nlohmann::json &object, const char *key, float fallback) {
    if (!object.contains(key) || !object[key].is_number()) return fallback;
    return object[key].get<float>();
}

bool jsonBool(const nlohmann::json &object, const char *key, bool fallback) {
    if (!object.contains(key) || !object[key].is_boolean()) return fallback;
    return object[key].get<bool>();
}

bool parseVec3Array(const nlohmann::json &value, glm::vec3 &out) {
    if (!value.is_array() || value.size() < 3u) return false;
    if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number()) return false;
    out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    return true;
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
        static_cast<int>(kScratchPairs.size()));
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

ScratchBillboardShapeOverride loadScratchBillboardShapeOverride(
    const nlohmann::json &root,
    const char *key) {
    ScratchBillboardShapeOverride shape;
    if (!root.contains(key) || !root[key].is_object()) return shape;

    const auto &object = root[key];
    shape.positionScale = std::max(0.0f, jsonFloat(object, "position_scale", shape.positionScale));
    if (!object.contains("billboards") || !object["billboards"].is_array()) return shape;

    const auto &billboards = object["billboards"];
    shape.billboards.reserve(billboards.size());
    for (const auto &billboardJson : billboards) {
        if (!billboardJson.is_object()) continue;
        glm::vec3 positionLocal(0.0f);
        if (!billboardJson.contains("position_local") ||
            !parseVec3Array(billboardJson["position_local"], positionLocal)) {
            continue;
        }

        shape.billboards.push_back(makeClawBillboard(
            positionLocal,
            jsonFloat(billboardJson, "scale_mul", 1.0f),
            jsonFloat(billboardJson, "scale_x_mul", 1.0f),
            jsonFloat(billboardJson, "scale_y_mul", 1.0f),
            jsonFloat(billboardJson, "alpha_mul", 1.0f),
            jsonFloat(billboardJson, "spin_deg", 0.0f)));
    }

    return shape;
}

ScratchShapeOverrides loadScratchShapeOverrides(const std::string &manifestPath) {
    ScratchShapeOverrides overrides;
    std::ifstream in(manifestPath);
    if (!in.is_open()) return overrides;

    try {
        nlohmann::json root;
        in >> root;
        if (!root.is_object()) return overrides;

        overrides.primaryClawVsout =
            loadScratchBillboardShapeOverride(root, "primary_claw_vsout");
    } catch (const std::exception &) {
        return ScratchShapeOverrides{};
    }

    return overrides;
}

void applyBillboardShape(SharedAuthoredBatchVFX::Config::DrawPass &pass,
                         const ScratchBillboardShapeOverride &shapeOverride) {
    if (pass.renderMode != "glow_billboard") return;
    if (!shapeOverride.hasBillboards()) return;

    pass.authoredBillboardsLocal.clear();
    pass.authoredBillboardsLocal.reserve(shapeOverride.billboards.size());
    for (const auto &billboard : shapeOverride.billboards) {
        pass.authoredBillboardsLocal.push_back(billboard);
    }
    pass.authoredBillboardPositionScale = shapeOverride.positionScale;
}

void applyPrimaryClawVsoutShape(SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                const ScratchShapeOverrides &shapeOverrides) {
    applyBillboardShape(pass, shapeOverrides.primaryClawVsout);
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

void configureScratchPassEnablement(SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                    const ScratchSequenceTuning &tuning,
                                    const ScratchPassInfo &info) {
    pass.cameraFacing = true;
    pass.enabled = info.pairIndex < tuning.pairCount;
    if (tuning.soloFirstClawEid1032 && info.isClaw() &&
        !idEquals(pass.id, kScratchFirstClawEid1032PassId)) {
        pass.enabled = false;
    }
    pass.sequenceCount = 1;
    pass.sequenceIndex = -1;
    pass.sequenceStep = 0.0f;
    pass.sequenceLife = 1.0f;
    pass.sequenceFadeLocal = false;
}

void configureScratchPassTiming(SharedAuthoredBatchVFX::Config::DrawPass &pass,
                                const ScratchSequenceTuning &tuning,
                                const ScratchPassInfo &info) {
    const float pairStartSec = static_cast<float>(info.pairIndex) * tuning.pairStepSec;
    const float leadSec = info.isGlow() ? tuning.glowLeadSec : tuning.clawLeadSec;
    const float durationSec = info.isGlow() ? tuning.glowDurationSec : tuning.clawDurationSec;
    pass.timeStartSec = pairStartSec + leadSec;
    pass.timeEndSec = pass.timeStartSec + durationSec;
    pass.timeFadeLocal = true;
    pass.timeFadeStart = info.isGlow() ? tuning.glowFadeStart : tuning.clawFadeStart;
}

void applyScratchGlowScale(SharedAuthoredBatchVFX::Config::DrawPass &pass,
                           const ScratchSequenceTuning &tuning,
                           const ScratchPassInfo &info) {
    if (info.role == ScratchPassRole::RedGlow) {
        pass.alphaMul *= tuning.redGlowAlphaScale;
        pass.scaleMul *= tuning.redGlowScaleMul;
    } else if (info.role == ScratchPassRole::GoldGlow) {
        pass.alphaMul *= tuning.goldGlowAlphaScale;
        pass.scaleMul *= tuning.goldGlowScaleMul;
    }
}

void applyScratchAngleTuning(SharedAuthoredBatchVFX::Config::DrawPass &pass,
                             const ScratchSequenceTuning &tuning,
                             const ScratchPassInfo &info) {
    const float pairSpinDeg =
        tuning.clawBillboardRollDeg +
        tuning.pairAnglesDeg[static_cast<std::size_t>(info.pairIndex)];
    if (info.isClaw()) {
        pass.billboardSpinStartDeg = pairSpinDeg;
        pass.directionSpacingJitterDeg =
            tuning.angleJitterDeg *
            (info.primaryClaw ? tuning.primaryClawJitterScale : 1.0f);
    } else if (info.role == ScratchPassRole::PointGlow) {
        pass.billboardSpinStartDeg = pairSpinDeg;
        pass.directionSpacingJitterDeg = tuning.angleJitterDeg * 0.5f;
    }
}

void applyScratchShapeOverrides(SharedAuthoredBatchVFX::Config &config,
                                const ScratchSequenceTuning &tuning,
                                const ScratchShapeOverrides &shapeOverrides) {
    for (auto &pass : config.drawPasses) {
        const ScratchPassInfo info = describeScratchPass(pass.id);
        if (!info.inSequence()) continue;

        if (tuning.primaryClawVsoutShape && info.primaryClaw) {
            applyPrimaryClawVsoutShape(pass, shapeOverrides);
        }

        if (tuning.centerAtImpact) {
            recenterAuthoredBillboards(pass);
        }
    }
}

void sortScratchPasses(std::vector<SharedAuthoredBatchVFX::Config::DrawPass> &passes) {
    std::stable_sort(passes.begin(),
                     passes.end(),
                     [](const auto &lhs, const auto &rhs) {
                         const ScratchPassInfo lhsInfo = describeScratchPass(lhs.id);
                         const ScratchPassInfo rhsInfo = describeScratchPass(rhs.id);
                         if (lhsInfo.pairIndex != rhsInfo.pairIndex) {
                             if (lhsInfo.pairIndex < 0) return false;
                             if (rhsInfo.pairIndex < 0) return true;
                             return lhsInfo.pairIndex < rhsInfo.pairIndex;
                         }
                         return lhsInfo.pairOrder < rhsInfo.pairOrder;
                     });
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
        const ScratchPassInfo info = describeScratchPass(pass.id);
        if (!info.inSequence()) continue;

        configureScratchPassEnablement(pass, tuning, info);
        configureScratchPassTiming(pass, tuning, info);
        applyScratchGlowScale(pass, tuning, info);
        applyScratchAngleTuning(pass, tuning, info);
    }
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
    const ScratchSequenceTuning tuning =
        loadScratchSequenceTuning(engine::paths::data(kScratchManifestRel));
    const ScratchShapeOverrides shapeOverrides =
        loadScratchShapeOverrides(engine::paths::data(kScratchShapeOverridesRel));
    applyScratchSequenceTuning(resolvedConfig, tuning);
    applyScratchShapeOverrides(resolvedConfig, tuning, shapeOverrides);
    sortScratchPasses(resolvedConfig.drawPasses);
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
