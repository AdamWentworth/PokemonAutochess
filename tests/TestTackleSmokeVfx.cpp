#include <algorithm>
#include <filesystem>
#include <string>

#include <glm/glm.hpp>

#include "vfx/effects/tackle/TackleSmokeVFX.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_tackle_smoke_vfx_contract(std::string& outFail) {
    const TackleSmokeVFX::Config defaults = TackleSmokeVFX::makeDefaultConfig();
    if (!expect(defaults.drawManifestPath == "config/vfx/moves/tackle_draw_passes.json",
                "Tackle smoke should default to the authored tackle draw-pass manifest.",
                outFail)) {
        return false;
    }
    if (!expect(defaults.blendMode == 0u,
                "Tackle smoke should default to alpha blending rather than additive growl blending.",
                outFail)) {
        return false;
    }
    if (!expect(std::filesystem::exists("config/vfx/moves/tackle_draw_passes.json"),
                "Tackle smoke contract expects the authored tackle draw-pass manifest to exist in the repo.",
                outFail)) {
        return false;
    }

    TackleSmokeVFX effect;
    effect.setConfig(defaults);
    effect.emitAt(glm::vec3(0.0f, 0.25f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    TackleSmokeVFX::RenderSnapshot snapshot;
    if (!expect(effect.activeCloudCount() > 0u && effect.buildRenderSnapshot(snapshot),
                "Tackle smoke should emit at least one live cloud and build a render snapshot after emitAt.",
                outFail)) {
        return false;
    }
    if (!expect(snapshot.config.blendMode == 0u,
                "Tackle smoke snapshots should carry alpha blend mode for the shared batch/runtime path.",
                outFail)) {
        return false;
    }
    if (!expect(snapshot.drawPasses.size() == 12u,
                "Tackle smoke should currently author six clustered smoke passes, five captured impact bursts, and the aggregated 1253-1540 streak burst.",
                outFail)) {
        return false;
    }

    bool saw4154 = false;
    bool saw4155 = false;
    bool saw4156 = false;
    bool saw4157 = false;
    bool saw4158 = false;
    bool saw4159 = false;
    bool saw4160 = false;
    bool saw4156QuarterRing = false;
    bool saw4157QuarterRing = false;
    bool saw4158CenteredBurst = false;
    bool saw4159CenteredBurst = false;
    bool saw4160CenteredBurst = false;
    bool saw1253StreakBurst = false;
    bool sawSmokeFrame5Window = false;
    bool sawSmokeFrame6Window = false;
    bool sawSmokeLongTail = false;
    bool sawQuarterCoreWindow = false;
    bool sawSpiralWindow = false;
    bool sawSparkWindow = false;
    std::size_t totalBillboardDirections = 0u;
    for (const auto& pass : snapshot.drawPasses) {
        const bool is4156 = pass.texturePath.find("Texture4156.png") != std::string::npos;
        const bool is4157 = pass.texturePath.find("Texture4157.png") != std::string::npos;
        const bool is4158 = pass.texturePath.find("Texture4158.png") != std::string::npos;
        const bool is4159 = pass.texturePath.find("Texture4159.png") != std::string::npos;
        const bool is4160 = pass.texturePath.find("Texture4160.png") != std::string::npos;
        if (!expect(!pass.overrideBlendMode,
                    "Tackle smoke should inherit blend mode from the shared config rather than per-pass overrides for now.",
                    outFail)) {
            return false;
        }
        if (!expect(pass.fragShaderPath.find("tackle_smoke_shared.frag") != std::string::npos ||
                        pass.fragShaderPath.find("authored_line_shared.frag") != std::string::npos,
                    "Tackle authored passes should currently use either the shared tackle smoke shader or the shared authored line shader.",
                    outFail)) {
            return false;
        }
        saw4154 = saw4154 || pass.texturePath.find("Texture4154.png") != std::string::npos;
        saw4155 = saw4155 || pass.texturePath.find("Texture4155.png") != std::string::npos;
        saw4156 = saw4156 || pass.texturePath.find("Texture4156.png") != std::string::npos;
        saw4157 = saw4157 || pass.texturePath.find("Texture4157.png") != std::string::npos;
        saw4158 = saw4158 || is4158;
        saw4159 = saw4159 || is4159;
        saw4160 = saw4160 || is4160;
        saw4156QuarterRing =
            saw4156QuarterRing || (is4156 && pass.renderMode == "texture_quarter_ring" && pass.quarterCount == 4);
        saw4157QuarterRing =
            saw4157QuarterRing || (is4157 && pass.renderMode == "texture_quarter_ring" && pass.quarterCount == 4);
        saw4158CenteredBurst =
            saw4158CenteredBurst || (is4158 && pass.renderMode == "glow_billboard");
        saw4159CenteredBurst =
            saw4159CenteredBurst || (is4159 && pass.renderMode == "glow_billboard");
        saw4160CenteredBurst =
            saw4160CenteredBurst || (is4160 && pass.renderMode == "glow_billboard");
        saw1253StreakBurst =
            saw1253StreakBurst ||
            (pass.eid == 1253 &&
             pass.renderMode == "streak_quad" &&
             pass.authoredSegmentsPath.find("tackle_streak_segments.json") != std::string::npos &&
             pass.authoredSegmentsLocal.size() == 48u &&
             pass.authoredSegmentCenterOrigin &&
             std::abs(pass.authoredSegmentTravelMul - 10.0f) <= 0.0005f);
        sawSmokeFrame5Window =
            sawSmokeFrame5Window ||
            ((pass.eid == 921 || pass.eid == 926) &&
             std::abs(pass.timeStartSec - 0.167f) <= 0.0005f);
        sawSmokeFrame6Window =
            sawSmokeFrame6Window ||
            ((pass.eid == 965 || pass.eid == 1004 || pass.eid == 1107 || pass.eid == 1176) &&
             std::abs(pass.timeStartSec - 0.200f) <= 0.0005f);
        sawSmokeLongTail =
            sawSmokeLongTail ||
            ((pass.eid == 921 || pass.eid == 926 || pass.eid == 965 || pass.eid == 1004 ||
              pass.eid == 1107 || pass.eid == 1176) &&
             std::abs(pass.timeEndSec - (50.0f / 30.0f)) <= 0.0005f &&
             pass.timeFadeLocal &&
             std::abs(pass.timeFadeStart - 0.30f) <= 0.0005f);
        sawQuarterCoreWindow =
            sawQuarterCoreWindow ||
            ((pass.eid == 1196 || pass.eid == 1210) &&
             std::abs(pass.timeStartSec - 0.0f) <= 0.0005f &&
             std::abs(pass.timeEndSec - 0.60f) <= 0.0005f &&
             pass.timeFadeLocal &&
             std::abs(pass.timeFadeStart - 0.62f) <= 0.0005f &&
             std::abs(pass.localScaleStartMul - 0.87f) <= 0.0005f &&
             std::abs(pass.localScaleEndMul - 1.0f) <= 0.0005f);
        sawSpiralWindow =
            sawSpiralWindow ||
            ((pass.eid == 1225 || pass.eid == 1234 || pass.eid == 1243) &&
             std::abs(pass.timeStartSec - 0.0f) <= 0.0005f &&
             std::abs(pass.timeEndSec - 0.533f) <= 0.0005f &&
             pass.timeFadeLocal &&
             std::abs(pass.timeFadeStart - 0.50f) <= 0.0005f &&
             std::abs(pass.localScaleStartMul - 0.87f) <= 0.0005f &&
             std::abs(pass.localScaleEndMul - 1.0f) <= 0.0005f);
        sawSparkWindow =
            sawSparkWindow ||
            (pass.eid == 1253 &&
             std::abs(pass.timeStartSec - 0.0f) <= 0.0005f &&
             std::abs(pass.timeEndSec - 0.40f) <= 0.0005f &&
             pass.timeFadeLocal &&
             std::abs(pass.timeFadeStart - 0.55f) <= 0.0005f);
        totalBillboardDirections += std::max<std::size_t>(1u, pass.directionsLocal.size());
    }

    if (!expect(saw4154 && saw4155 && saw4156 && saw4157 && saw4158 && saw4159 && saw4160,
                "Tackle smoke should currently include Texture4154, Texture4155, Texture4156, Texture4157, Texture4158, Texture4159, and Texture4160 in the authored pass set.",
                outFail)) {
        return false;
    }
    if (!expect(saw4156QuarterRing,
                "Tackle eid 1196 should currently render as a 4-piece quarter ring rather than a single billboard card.",
                outFail)) {
        return false;
    }
    if (!expect(saw4157QuarterRing,
                "Tackle eid 1210 should currently render as a 4-piece quarter ring rather than a single billboard card.",
                outFail)) {
        return false;
    }
    if (!expect(saw4158CenteredBurst,
                "Tackle eid 1225 should currently render as a centered impact billboard rather than another quarter ring.",
                outFail)) {
        return false;
    }
    if (!expect(saw4159CenteredBurst,
                "Tackle eid 1234 should currently render as a centered impact billboard rather than another quarter ring.",
                outFail)) {
        return false;
    }
    if (!expect(saw4160CenteredBurst,
                "Tackle eid 1243 should currently render as a centered impact billboard rather than another quarter ring.",
                outFail)) {
        return false;
    }
    if (!expect(saw1253StreakBurst,
                "Tackle should currently use the 1253-1540 captured line draws as one center-origin authored streak burst pass with 48 capture-derived local segments and fast outward travel.",
                outFail)) {
        return false;
    }
    if (!expect(sawSmokeFrame5Window && sawSmokeFrame6Window,
                "Tackle smoke should currently begin in two explicit 30 fps windows around frames 5 and 6.",
                outFail)) {
        return false;
    }
    if (!expect(sawSmokeLongTail,
                "Tackle smoke should currently use a full ~50-frame lifetime tail with a local fade instead of cutting off abruptly.",
                outFail)) {
        return false;
    }
    if (!expect(sawQuarterCoreWindow,
                "Tackle quarter-core passes should currently use an explicit local timing window through frame 18.",
                outFail)) {
        return false;
    }
    if (!expect(sawSpiralWindow,
                "Tackle spiral impact cards should currently use an explicit local timing window through frame 16.",
                outFail)) {
        return false;
    }
    if (!expect(sawSparkWindow,
                "Tackle sparks should currently use an explicit local timing window through frame 12.",
                outFail)) {
        return false;
    }
    if (!expect(totalBillboardDirections >= 20u,
                "Tackle smoke should author a dense clustered billboard set rather than only one or two puffs.",
                outFail)) {
        return false;
    }

    return true;
}
