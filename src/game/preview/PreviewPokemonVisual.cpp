#include "game/preview/PreviewPokemonVisual.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>

#include "engine/core/Paths.h"
#include "engine/render/Model.h"
#include "game/config/AnimSetLoader.h"
#include "game/preview/PreviewSceneUtils.h"
#include "game/world/MoveImpactMath.h"

namespace game::preview {

void PreviewPokemonVisual::ensureLoaded(PokemonConfigLoader& pokemonConfig) {
    if (attemptedLoad) return;
    attemptedLoad = true;

    const PokemonStats* speciesStats = pokemonConfig.getStats(speciesName);
    if (!speciesStats) {
        loadError = "No Pokemon config entry for '" + speciesName + "'";
        return;
    }
    stats = *speciesStats;

    modelPath = engine::paths::asset(std::string("models/") + speciesStats->model);

    try {
        model = std::make_shared<Model>(modelPath);
    } catch (const std::exception& ex) {
        loadError = ex.what();
        model.reset();
        return;
    }

    runtimeLikeUnit = PokemonInstance{};
    runtimeLikeUnit.name = speciesName;
    runtimeLikeUnit.id = PokemonInstance::getNextUnitID();
    runtimeLikeUnit.model = model;
    runtimeLikeUnit.backendModelPath = modelPath;
    runtimeLikeUnit.animIndexCacheSourceModelPath = modelPath;
    runtimeLikeUnit.backendAnimDurationsSourceModelPath = modelPath;
    runtimeLikeUnit.modelScaleCorrection =
        resolvePreviewModelScaleCorrection(
            model.get(), speciesStats->modelScaleMode, speciesStats->modelScaleAxis);
    runtimeLikeUnit.speciesScale = std::max(0.05f, speciesStats->visualScale);
    runtimeLikeUnit.movementSpeed = std::max(0.01f, speciesStats->movementSpeed);
    runtimeLikeUnit.visualScale = 1.0f;
    runtimeLikeUnit.captureScale = 1.0f;
    AnimSet::applyAnimSetOverrides(runtimeLikeUnit, modelPath, nullptr);

    idleAnimIndex = runtimeLikeUnit.animIdleIndex;
    if (idleAnimIndex < 0 && model->getAnimationCount() > 0) {
        idleAnimIndex = 0;
    }
    finalScale = std::max(0.05f, computeModelWorldScaleForMoveImpact(runtimeLikeUnit));
    valid = true;
}

int PreviewPokemonVisual::resolveClipAnimIndex(const std::string& clipName) const {
    if (!model || clipName.empty()) return -1;
    return AnimSet::resolveAnimIndex(model.get(), clipName);
}

float PreviewPokemonVisual::animationDurationSec(int animIndex) const {
    if (!model || animIndex < 0) return 0.0f;
    return model->getAnimationDurationSec(animIndex);
}

float PreviewPokemonVisual::animationFps() const {
    return (runtimeLikeUnit.animFps > 0.0f) ? runtimeLikeUnit.animFps : 24.0f;
}

bool PreviewPokemonVisual::previewAnimationActive() const {
    return previewAnimIndex >= 0;
}

int PreviewPokemonVisual::currentAnimIndex() const {
    return previewAnimIndex >= 0 ? previewAnimIndex : idleAnimIndex;
}

float PreviewPokemonVisual::currentAnimTimeSec() const {
    return previewAnimIndex >= 0 ? previewAnimTimeSec : animTimeSec;
}

std::string PreviewPokemonVisual::currentAnimName() const {
    if (!model) return {};
    return model->getAnimationName(currentAnimIndex());
}

void PreviewPokemonVisual::setPreviewAnimation(int animIndex,
                                               bool loop,
                                               bool restart,
                                               float startTimeSec,
                                               float playbackSpeed) {
    if (animIndex < 0) {
        clearPreviewAnimation();
        return;
    }
    if (restart || previewAnimIndex != animIndex || previewAnimLoop != loop) {
        previewAnimTimeSec = std::max(0.0f, startTimeSec);
    }
    previewAnimIndex = animIndex;
    previewAnimPlaybackSpeed = std::max(0.0f, playbackSpeed);
    previewAnimLoop = loop;
    previewAnimFinished = false;
}

void PreviewPokemonVisual::clearPreviewAnimation() {
    previewAnimIndex = -1;
    previewAnimTimeSec = 0.0f;
    previewAnimPlaybackSpeed = 1.0f;
    previewAnimLoop = false;
    previewAnimFinished = false;
}

void PreviewPokemonVisual::update(float dt) {
    if (!valid || !model) return;

    const int animIndex = previewAnimIndex >= 0 ? previewAnimIndex : idleAnimIndex;
    if (animIndex < 0) return;
    const float dur = model->getAnimationDurationSec(animIndex);
    if (dur <= 0.0001f) return;

    if (previewAnimIndex >= 0) {
        previewAnimTimeSec = std::max(
            0.0f,
            previewAnimTimeSec + std::max(0.0f, dt) * std::max(0.0f, previewAnimPlaybackSpeed));
        if (previewAnimLoop) {
            previewAnimTimeSec = std::fmod(previewAnimTimeSec, dur);
            if (previewAnimTimeSec < 0.0f) previewAnimTimeSec += dur;
        } else {
            const float endTime = std::max(0.0f, dur - 0.0001f);
            previewAnimTimeSec = std::min(previewAnimTimeSec, endTime);
            previewAnimFinished = previewAnimTimeSec >= endTime;
        }
        return;
    }

    animTimeSec = std::fmod(animTimeSec + std::max(0.0f, dt), dur);
    if (animTimeSec < 0.0f) animTimeSec += dur;
}

glm::vec3 makeProjectedAlignedPreviewPos(const PreviewPokemonVisual& visual,
                                         glm::vec3 worldPos,
                                         float boardSurfaceY) {
    if (!visual.valid || !visual.model || !visual.model->hasBounds()) {
        return worldPos;
    }

    const float minAllowedModelY = boardSurfaceY + 0.0025f;
    const float approxModelMinY =
        worldPos.y + visual.model->getBoundsMin().y * visual.finalScale;
    if (std::isfinite(approxModelMinY) && approxModelMinY < minAllowedModelY) {
        worldPos.y += (minAllowedModelY - approxModelMinY);
    }
    return worldPos;
}

PokemonInstance makePreviewRuntimeUnit(const PreviewPokemonVisual& visual,
                                       const glm::vec3& worldPos,
                                       float yawDeg,
                                       PokemonSide side) {
    PokemonInstance unit = visual.runtimeLikeUnit;
    unit.side = side;
    unit.alive = true;
    unit.fainting = false;
    unit.captureInProgress = false;
    unit.position = worldPos;
    unit.rotation = glm::vec3(0.0f, yawDeg, 0.0f);
    unit.visualYOffset = 0.0f;
    unit.activeAnimIndex = visual.currentAnimIndex();
    unit.currentAttackAnimIndex =
        visual.previewAnimationActive() ? visual.currentAnimIndex() : -1;
    unit.animTimeSec = visual.currentAnimTimeSec();
    unit.visualScale = 1.0f;
    unit.captureScale = 1.0f;
    return unit;
}

} // namespace game::preview
