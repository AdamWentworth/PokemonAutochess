#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/PokemonInstance.h"
#include "game/config/PokemonConfigLoader.h"

class Model;

namespace game::preview {

struct PreviewPokemonVisual {
    std::string speciesName;
    std::shared_ptr<Model> model;
    std::string loadError;
    std::string modelPath;
    float finalScale = 1.0f;
    float animTimeSec = 0.0f;
    int idleAnimIndex = -1;
    int previewAnimIndex = -1;
    float previewAnimTimeSec = 0.0f;
    float previewAnimPlaybackSpeed = 1.0f;
    bool previewAnimLoop = false;
    bool previewAnimFinished = false;
    bool attemptedLoad = false;
    bool valid = false;
    PokemonInstance runtimeLikeUnit;
    PokemonStats stats{};
    std::vector<std::uint8_t> directDrawSkipSubmeshMask;
    bool directDrawSkipSubmeshMaskReady = false;

    void ensureLoaded(PokemonConfigLoader& pokemonConfig);
    int resolveClipAnimIndex(const std::string& clipName) const;
    float animationDurationSec(int animIndex) const;
    float animationFps() const;
    bool previewAnimationActive() const;
    int currentAnimIndex() const;
    float currentAnimTimeSec() const;
    std::string currentAnimName() const;
    void setPreviewAnimation(int animIndex,
                             bool loop,
                             bool restart,
                             float startTimeSec = 0.0f,
                             float playbackSpeed = 1.0f);
    void clearPreviewAnimation();
    void update(float dt);
};

glm::vec3 makeProjectedAlignedPreviewPos(const PreviewPokemonVisual& visual,
                                         glm::vec3 worldPos,
                                         float boardSurfaceY = 0.006f);

PokemonInstance makePreviewRuntimeUnit(const PreviewPokemonVisual& visual,
                                       const glm::vec3& worldPos,
                                       float yawDeg,
                                       PokemonSide side);

} // namespace game::preview
