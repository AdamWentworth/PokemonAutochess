#pragma once

#include "engine/vfx/ParticleSystem.h"
#include "game/PokemonInstance.h"
#include "game/vfx/TailFireVFX.h"

#include <functional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_tail_fire_fallback {

struct Anchor {
    bool valid = false;
    bool exactFireAnchor = false;
    bool meshCarrierActive = false;
    glm::vec3 pos{0.0f};
    glm::vec3 tipPos{0.0f};
    glm::mat3 basis{1.0f};
    glm::vec3 backDir{0.0f, 1.0f, 0.0f};
    float particleSizeScale = 1.0f;
};

struct Args {
    float worldCellSize = 1.0f;
    double simNowSec = 0.0;
    const TailFireVFX::Config* cfg = nullptr;
    const std::unordered_map<int, Anchor>* anchors = nullptr;
    const std::vector<PokemonInstance>* pokemons = nullptr;
    const std::vector<PokemonInstance>* benchPokemons = nullptr;
    std::function<bool(const char*, const ParticleSystem::RenderSnapshot&)> appendSnapshot;
};

bool appendSyntheticTailFire(const Args& args);

} // namespace game::runtime::shared_tail_fire_fallback

