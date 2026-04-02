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
    if (!expect(snapshot.drawPasses.size() == 6u,
                "Tackle smoke should currently author six clustered billboard passes for the first captured-shape shortcut.",
                outFail)) {
        return false;
    }

    bool saw4154 = false;
    bool saw4155 = false;
    std::size_t totalBillboardDirections = 0u;
    for (const auto& pass : snapshot.drawPasses) {
        if (!expect(pass.renderMode == "glow_billboard",
                    "Tackle smoke authored passes should render as glow_billboard quads.",
                    outFail)) {
            return false;
        }
        if (!expect(!pass.overrideBlendMode,
                    "Tackle smoke should inherit blend mode from the shared config rather than per-pass overrides for now.",
                    outFail)) {
            return false;
        }
        if (!expect(pass.fragShaderPath.find("tackle_smoke_shared.frag") != std::string::npos,
                    "Tackle smoke authored passes should use the shared tackle smoke shader.",
                    outFail)) {
            return false;
        }
        saw4154 = saw4154 || pass.texturePath.find("Texture4154.png") != std::string::npos;
        saw4155 = saw4155 || pass.texturePath.find("Texture4155.png") != std::string::npos;
        totalBillboardDirections += std::max<std::size_t>(1u, pass.directionsLocal.size());
    }

    if (!expect(saw4154 && saw4155,
                "Tackle smoke should currently alternate between Texture4154 and Texture4155 in the authored pass set.",
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
