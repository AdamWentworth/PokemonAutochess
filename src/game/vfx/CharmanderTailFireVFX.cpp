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

        for (int i = 0; i < spawnCount; ++i) {
            float base = (float)u.id * 1000.0f + (float)i * 17.0f;

            float rx = hashSigned(base + 1.0f) * spawnRadius;
            float ry = hash01(base + 2.0f) * spawnRadius * 0.35f;
            float rz = hashSigned(base + 3.0f) * spawnRadius;

            ParticleSystem::Particle p;
            p.pos = tailWorld + glm::vec3(rx, ry, rz);

            float side = 0.26f;
            p.vel = glm::vec3(
                hashSigned(base + 4.0f) * side,
                0.55f + hash01(base + 5.0f) * 0.80f,
                hashSigned(base + 6.0f) * side
            );

            p.maxLifeSec = 0.28f + hash01(base + 7.0f) * 0.24f;
            p.lifeSec = p.maxLifeSec;

            // ✅ Smaller again (was 3.5..7.0, then 2.0..4.0-ish)
            p.sizePx = 2.0f + hash01(base + 8.0f) * 2.0f; // 2.0..4.0

            p.seed = hash01(base + 9.0f);

            particles.emit(p);
        }
    }
}

void CharmanderTailFireVFX::render(const Camera3D& camera) {
    particles.render(camera);
}
