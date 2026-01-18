// src/game/vfx/CharmanderTailFireVFX.cpp
#include "CharmanderTailFireVFX.h"

#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

static constexpr int kLoopAnimIndex = 1;

static float hash01(float x) {
    float s = std::sin(x * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

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
    if (instance.model) scaleFactor = instance.model->getScaleFactor();

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

        glm::mat4 tailNodeGlobal(1.0f);
        glm::vec3 tailWorld(0.0f);

        if (u.model->getNodeGlobalTransformByIndex(u.animTimeSec, kLoopAnimIndex, tailTipNodeIndex, tailNodeGlobal)) {
            tailWorld = glm::vec3(instM * tailNodeGlobal * glm::vec4(0, 0, 0, 1));
        } else {
            tailWorld = glm::vec3(instM * glm::vec4(0.0f, 0.78f, -0.38f, 1.0f));
        }

        tailWorld.y += tailWorldYOffset; // keep your tuned value

        float scaleFactor = u.model ? u.model->getScaleFactor() : 1.0f;

        uint32_t& serial = spawnSerial[u.id];

        // Back-lean direction: +Z assumed toward the camera when you're behind the unit.
        // If it leans away from you, change to (0,0,-1).
        const glm::vec3 backDir = glm::vec3(0.0f, 0.0f, 1.0f);

        for (int i = 0; i < spawnCount; ++i) {
            float base = (float)u.id * 100000.0f + (float)(serial++);

            float rx = hashSigned(base + 1.0f) * spawnRadius;
            float ry = hash01(base + 2.0f) * spawnRadius * 0.35f;
            float rz = hashSigned(base + 3.0f) * spawnRadius;

            ParticleSystem::Particle p;
            p.pos = tailWorld + glm::vec3(rx, ry, rz);

            // UPRIGHT fire:
            // - no left/right drift (x=0)
            // - minimal sideways (z) jitter; all “lean” comes from backDir
            float up = 0.03f + hash01(base + 5.0f) * 0.06f;
            float back = 0.07f + hash01(base + 6.0f) * 0.05f; // “lean back” amount

            p.vel = glm::vec3(0.0f, up, 0.0f) + backDir * back;

            p.maxLifeSec = 0.10f + hash01(base + 7.0f) * 0.06f;
            p.lifeSec = p.maxLifeSec;

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
