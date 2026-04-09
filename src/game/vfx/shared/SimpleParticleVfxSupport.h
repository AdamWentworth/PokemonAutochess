#pragma once

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>

#include "engine/core/Random.h"
#include "engine/vfx/ParticleSystem.h"

class Camera3D;

namespace game::particle_vfx::shared {

template <typename Config>
inline void applySimpleParticleConfig(ParticleSystem& particles,
                                      const Config& cfg,
                                      bool useFlipbook = false) {
    particles.setShaderPaths(cfg.vertShaderPath, cfg.fragShaderPath);
    particles.setUseFlipbook(useFlipbook);

    ParticleSystem::RenderSettings renderSettings;
    renderSettings.blend = cfg.blend;
    renderSettings.depthTest = cfg.depthTest;
    renderSettings.depthWrite = cfg.depthWrite;
    renderSettings.programPointSize = true;
    particles.setRenderSettings(renderSettings);

    ParticleSystem::UpdateSettings updateSettings;
    updateSettings.acceleration = cfg.acceleration;
    updateSettings.dampingBase = cfg.dampingBase;
    particles.setUpdateSettings(updateSettings);

    particles.setPointScale(cfg.pointScale);
}

class SimpleParticleVfxSupport {
public:
    explicit SimpleParticleVfxSupport(std::uint32_t seed)
        : rng_(seed) {}

    void markDirty() {
        configured_ = false;
    }

    template <typename Config>
    void ensureConfigured(const Config& cfg) {
        if (configured_) return;
        applySimpleParticleConfig(particles_, cfg);
        configured_ = true;
    }

    void update(float dt) {
        particles_.update(dt);
    }

    void render(const Camera3D& camera) {
        particles_.render(camera);
    }

    ParticleSystem& particles() { return particles_; }
    const ParticleSystem& particles() const { return particles_; }

    float rand01() {
        return engine::random::nextFloat01(rng_);
    }

    float randRange(float a, float b) {
        if (b < a) std::swap(a, b);
        return a + (b - a) * rand01();
    }

    int randInclusive(int minValue, int maxValue) {
        return engine::random::rangeInclusive(rng_, minValue, maxValue);
    }

    static glm::vec3 safeForwardXZ(const glm::vec3& value) {
        glm::vec3 forward(value.x, 0.0f, value.z);
        const float len = glm::length(forward);
        if (len <= 0.0001f) {
            return glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return forward / len;
    }

private:
    ParticleSystem particles_;
    bool configured_ = false;
    engine::XorShift32 rng_;
};

} // namespace game::particle_vfx::shared
