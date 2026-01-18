// src/game/vfx/CharmanderTailFireVFX.cpp
#include "CharmanderTailFireVFX.h"

#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

static constexpr int kLoopAnimIndex = 1;

// Small deterministic hash -> [0,1)
static float hash01(float x) {
    float s = std::sin(x * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

// Random in [-1,1]
static float hashSigned(float x) {
    return hash01(x) * 2.0f - 1.0f;
}

bool CharmanderTailFireVFX::isCharmanderName(const std::string& name) const {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return (n == "charmander");
}

glm::mat4 CharmanderTailFireVFX::computeInstanceTransform(const PokemonInstance& instance) const {
    float scaleFactor = 1.0f;
    if (instance.model) {
        scaleFactor = instance.model->getScaleFactor();
    }

    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
    glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), instance.position);

    return translation * rotationY * rotationX * rotationZ * scale;
}

void CharmanderTailFireVFX::update(float dt,
                                   const std::vector<PokemonInstance>& boardUnits,
                                   const std::vector<PokemonInstance>& benchUnits)
{
    // Make particles visually larger at typical camera distances.
    // If it becomes too big after shader clamp increase, lower this first.
    particles.setPointScale(1200.0f);

    particles.update(dt);
    emitForList(dt, boardUnits);
    emitForList(dt, benchUnits);
}

void CharmanderTailFireVFX::emitForList(float dt, const std::vector<PokemonInstance>& list) {
    dt = std::clamp(dt, 0.0f, 0.05f);

    for (const auto& u : list) {
        if (!u.alive || !u.model) continue;
        if (!isCharmanderName(u.name)) continue;

        float& acc = emitAccumulator[u.id];
        acc += dt * emitRatePerSec;

        int spawnCount = (int)std::floor(acc);
        if (spawnCount <= 0) continue;
        acc -= (float)spawnCount;

        glm::mat4 instM = computeInstanceTransform(u);

        // Animated tail node position (MODEL SPACE -> WORLD SPACE)
        glm::mat4 tailNodeGlobal(1.0f);
        glm::vec3 tailWorld(0.0f);

        if (u.model->getNodeGlobalTransformByIndex(u.animTimeSec, kLoopAnimIndex, tailTipNodeIndex, tailNodeGlobal)) {
            tailWorld = glm::vec3(instM * tailNodeGlobal * glm::vec4(0, 0, 0, 1));
        } else {
            // fallback (static approximation)
            tailWorld = glm::vec3(instM * glm::vec4(0.0f, 0.78f, -0.38f, 1.0f));
        }

        // push down/up to hug the tail tip visually
        tailWorld.y += tailWorldYOffset;

        float scaleFactor = u.model ? u.model->getScaleFactor() : 1.0f;

        for (int i = 0; i < spawnCount; ++i) {
            float base = (float)u.id * 1000.0f + (float)i * 17.0f;

            float rx = hashSigned(base + 1.0f) * spawnRadius;
            float ry = hash01(base + 2.0f) * spawnRadius * 0.35f;
            float rz = hashSigned(base + 3.0f) * spawnRadius;

            ParticleSystem::Particle p;
            p.pos = tailWorld + glm::vec3(rx, ry, rz);

            // Keep a single cohesive flame (very small sideways drift)
            float side = 0.03f;
            p.vel = glm::vec3(
                hashSigned(base + 4.0f) * side,
                0.02f + hash01(base + 5.0f) * 0.06f,
                hashSigned(base + 6.0f) * side
            );

            // short life so it doesn't travel far from the tail tip
            p.maxLifeSec = 0.10f + hash01(base + 7.0f) * 0.06f;
            p.lifeSec = p.maxLifeSec;

            // Bigger flame sprite:
            // Old: 0.085 / 0.035. This was tiny and also got capped by shader at 28px.
            float sizeBase = 0.22f * scaleFactor;
            float sizeJit  = 0.10f * scaleFactor;
            p.sizePx = sizeBase + hash01(base + 8.0f) * sizeJit;

            p.seed = hash01(base + 9.0f);

            particles.emit(p);
        }
    }
}

void CharmanderTailFireVFX::render(const Camera3D& camera) {
    particles.render(camera);
}
