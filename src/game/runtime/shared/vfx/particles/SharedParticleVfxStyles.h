#pragma once

#include <string>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"

namespace game::runtime::shared_particle_vfx_styles {

struct ParticleVisualStyle {
    std::string texturePath;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
};

ParticleVisualStyle resolveStyle(const ParticleSystem::RenderSnapshot& snapshot,
                                 const ParticleSystem::Particle& particle,
                                 float age01);

} // namespace game::runtime::shared_particle_vfx_styles
