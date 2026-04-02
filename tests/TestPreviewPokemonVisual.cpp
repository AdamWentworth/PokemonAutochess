#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "game/preview/PreviewPokemonVisual.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_preview_runtime_unit_contract(std::string& outFail) {
    game::preview::PreviewPokemonVisual visual{};
    visual.runtimeLikeUnit.attackDurationSec = 0.8f;
    visual.idleAnimIndex = 2;
    visual.previewAnimIndex = 5;
    visual.previewAnimTimeSec = 0.2f;
    visual.previewAnimPlaybackSpeed = 2.0f;
    visual.previewAttackMoveName = "tackle";

    const PokemonInstance attackUnit = game::preview::makePreviewRuntimeUnit(
        visual,
        glm::vec3(1.0f, 0.0f, 2.0f),
        30.0f,
        PokemonSide::Player);
    if (!expect(attackUnit.activeAnimIndex == 5 &&
                    attackUnit.currentAttackAnimIndex == 5,
                "Preview runtime unit should surface the active one-shot clip as the current attack animation.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(attackUnit.attackDurationSec - 0.8f) < 0.0001f,
                "Preview runtime unit should preserve the preview attack window duration when no model clip duration is available.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(attackUnit.attackTimerSec - 0.7f) < 0.0001f,
                "Preview runtime unit should derive a live attack timer from preview animation time and playback speed.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(attackUnit.attackAnimSpeed - 2.0f) < 0.0001f,
                "Preview runtime unit should mirror preview playback speed into attackAnimSpeed.",
                outFail)) {
        return false;
    }
    if (!expect(attackUnit.activeAttackMoveName == "tackle",
                "Preview runtime unit should mirror the active preview move name for move-specific presentation policy.",
                outFail)) {
        return false;
    }

    visual.previewAnimIndex = -1;
    visual.previewAnimTimeSec = 0.0f;
    visual.previewAnimPlaybackSpeed = 1.0f;
    const PokemonInstance idleUnit = game::preview::makePreviewRuntimeUnit(
        visual,
        glm::vec3(0.0f),
        0.0f,
        PokemonSide::Enemy);
    if (!expect(idleUnit.activeAnimIndex == 2 &&
                    idleUnit.currentAttackAnimIndex == -1,
                "Preview runtime unit should fall back to the idle clip when no preview attack animation is active.",
                outFail)) {
        return false;
    }
    if (!expect(idleUnit.attackTimerSec == 0.0f,
                "Preview runtime unit should clear the attack timer when no preview attack animation is active.",
                outFail)) {
        return false;
    }
    if (!expect(idleUnit.activeAttackMoveName.empty(),
                "Preview runtime unit should clear the active attack move name when no preview attack animation is active.",
                outFail)) {
        return false;
    }

    return true;
}
