// src/engine/vfx/ParticleSystem.h
#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>

class Shader;
class Camera3D;

class ParticleSystem {
public:
    struct Particle {
        glm::vec3 pos{0.0f};
        glm::vec3 vel{0.0f};

        float lifeSec = 0.0f;
        float maxLifeSec = 0.0f;

        float sizePx = 24.0f;
        float seed = 0.0f;
    };

public:
    ParticleSystem() = default;
    ~ParticleSystem();

    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    void init();
    void shutdown();

    void update(float dt);
    void render(const Camera3D& camera);

    void emit(const Particle& p);

    void setPointScale(float s) { pointScale = s; }

private:
    struct GPUParticle {
        glm::vec3 pos;
        float age01;
        float sizePx;
        float seed;
    };

private:
    bool initialized = false;

    std::shared_ptr<Shader> shader;

    unsigned int vao = 0;
    unsigned int vbo = 0;

    std::vector<Particle> particles;
    std::vector<GPUParticle> gpuBuffer;

    float timeSec = 0.0f;

    // MUCH smaller than before
    float pointScale = 40.0f;
};
