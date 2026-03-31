// src/game/vfx/TailFireVFX.h
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "game/PokemonInstance.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSyntheticEmitter.h"
#include "game/vfx/TailFireVFXConfig.h"

class Camera3D;
class Model;

class TailFireVFX {
public:
    using Config = TailFireVFXConfig;

public:
    TailFireVFX() = default;

    void setFilter(std::function<bool(const PokemonInstance&)> f) { filter = std::move(f); }
    void setNameFilterCaseInsensitive(const std::string& nameLowerOrAnyCase);
    void setConfig(const Config& c);
    const Config& getConfig() const { return defaultEmitter.cfg; }

    void setUsePlaybackSpeciesConfigs(bool enabled);
    bool usesPlaybackSpeciesConfigs() const { return usePlaybackSpeciesConfigs; }
    std::size_t particleCount() const;
    bool buildRenderSnapshots(std::vector<ParticleSystem::RenderSnapshot>& out) const;

    void update(float dt,
                const std::vector<PokemonInstance>& boardUnits,
                const std::vector<PokemonInstance>& benchUnits);

    void render(const Camera3D& camera);

private:
    struct EmitterBucket {
        Config cfg{};
        game::runtime::shared_tail_fire_synth_emitter::SyntheticEmitterState synthEmitter;
        std::unordered_map<const Model*, int> tailNodeIndexCache;
        std::unordered_map<const Model*, int> fireAnchorBaseNodeIndexCache;
        std::unordered_map<const Model*, int> fireAnchorTipNodeIndexCache;
    };

    void resetBucket(EmitterBucket& bucket, const Config& c);
    void clearPlaybackSpeciesEmitters();
    static std::string normalizeSpeciesKey(std::string_view species);
    EmitterBucket& resolveEmitterForUnit(const PokemonInstance& unit);
    const EmitterBucket* findPlaybackEmitter(std::string_view species) const;
    void ensureConfigured(EmitterBucket& bucket);
    void updateEmitters(float dt);
    void emitForList(float dt, const std::vector<PokemonInstance>& list);
    glm::mat4 computeInstanceTransform(const PokemonInstance& instance,
                                       const Config& cfg) const;

    // Resolve name->index once per Model* (cached). Uses cfg.tailTipNodeIndex as fallback.
    int resolveTailTipNodeIndex(EmitterBucket& bucket, const Model& model);
    int resolveOptionalNodeIndex(const Model& model,
                                 const std::string& nodeName,
                                 std::unordered_map<const Model*, int>& cache);

private:
    std::function<bool(const PokemonInstance&)> filter;
    bool usePlaybackSpeciesConfigs = false;
    EmitterBucket defaultEmitter{};
    std::unordered_map<std::string, EmitterBucket> playbackSpeciesEmitters;
};
