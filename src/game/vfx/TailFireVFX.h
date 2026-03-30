// src/game/vfx/TailFireVFX.h
#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
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

    void setConfig(const Config& c) {
        cfg = c;
        game::runtime::shared_tail_fire_synth_emitter::resetState(synthEmitter);

        // important if node name changes
        tailNodeIndexCache.clear();
        fireAnchorBaseNodeIndexCache.clear();
        fireAnchorTipNodeIndexCache.clear();
    }
    const Config& getConfig() const { return cfg; }

    ParticleSystem& getParticles() { return synthEmitter.particles; }
    const ParticleSystem& getParticles() const { return synthEmitter.particles; }

    void update(float dt,
                const std::vector<PokemonInstance>& boardUnits,
                const std::vector<PokemonInstance>& benchUnits);

    void render(const Camera3D& camera);

private:
    void ensureConfigured();
    void emitForList(float dt, const std::vector<PokemonInstance>& list);
    glm::mat4 computeInstanceTransform(const PokemonInstance& instance) const;

    // Resolve name->index once per Model* (cached). Uses cfg.tailTipNodeIndex as fallback.
    int resolveTailTipNodeIndex(const Model& model) const;
    int resolveOptionalNodeIndex(const Model& model,
                                 const std::string& nodeName,
                                 std::unordered_map<const Model*, int>& cache) const;

private:
    std::function<bool(const PokemonInstance&)> filter;
    game::runtime::shared_tail_fire_synth_emitter::SyntheticEmitterState synthEmitter;

    Config cfg{};

    mutable std::unordered_map<const Model*, int> tailNodeIndexCache;
    mutable std::unordered_map<const Model*, int> fireAnchorBaseNodeIndexCache;
    mutable std::unordered_map<const Model*, int> fireAnchorTipNodeIndexCache;
};
