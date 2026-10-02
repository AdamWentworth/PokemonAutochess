#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include <glm/glm.hpp>

#include "game/vfx/effects/scratch/ScratchGlowVFX.h"
#include "game/vfx/effects/tackle/TackleSmokeVFX.h"
#include "engine/vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

namespace {

bool require(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

} // namespace

int main() {
    bool passed = true;

    const auto scratch = ScratchGlowVFX::makeDefaultConfig();
    passed &= require(!scratch.drawManifestPath.empty(),
                      "Scratch must expose an authored manifest dependency.");
    passed &= require(!scratch.depthWrite,
                      "Scratch must remain a transparent, non-depth-writing effect.");

    const auto tackleDefault = TackleSmokeVFX::makeDefaultConfig();
    const auto tackleGameplay = TackleSmokeVFX::makeGameplayConfig();
    passed &= require(!tackleDefault.drawManifestPath.empty(),
                      "Tackle must expose an authored manifest dependency.");
    passed &= require(
        std::abs(tackleGameplay.ringMinSize - tackleDefault.ringMinSize * 0.5f) <
            0.0005f,
        "The gameplay Tackle profile must retain its qualified spatial scale.");

    SharedAuthoredBatchVFX::Config::DrawPass generated;
    generated.generatedDirectionMode = "circle";
    generated.generatedDirectionCount = 8;
    const auto directions =
        vfx::runtime::authored::resolveGeneratedDirections(generated);
    passed &= require(directions.size() == 8u,
                      "Authored direction generation must remain deterministic.");
    for (const glm::vec3& direction : directions) {
        passed &= require(
            std::abs(glm::length(direction) - 1.0f) < 0.0005f,
            "Generated effect directions must be normalized.");
    }

    if (!passed) {
        return EXIT_FAILURE;
    }

    std::cout << "Phlosion VFX contracts passed.\n";
    return EXIT_SUCCESS;
}
