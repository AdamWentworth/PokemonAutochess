// src/game/vfx/LeechSeedProjectileVFX.cpp
#include "LeechSeedProjectileVFX.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"

static float hash01(float x) {
    float s = std::sin(x * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

static glm::mat3 orthonormalBasis(const glm::mat4& m) {
    glm::vec3 x = glm::vec3(m[0]);
    glm::vec3 y = glm::vec3(m[1]);
    glm::vec3 z = glm::vec3(m[2]);

    auto safeNorm = [](glm::vec3 v) -> glm::vec3 {
        float len2 = glm::dot(v, v);
        if (len2 < 1e-10f) return glm::vec3(1, 0, 0);
        return v * (1.0f / std::sqrt(len2));
    };

    x = safeNorm(x);
    y = y - x * glm::dot(y, x);
    y = safeNorm(y);
    z = glm::cross(x, y);
    z = safeNorm(z);

    if (glm::dot(glm::cross(x, y), z) < 0.0f) {
        z = -z;
    }

    return glm::mat3(x, y, z);
}

void LeechSeedProjectileVFX::ensureConfigured() {
    if (configured) return;

    particles.setShaderPaths(cfg.vertShaderPath, cfg.fragShaderPath);
    particles.setUseFlipbook(false);

    ParticleSystem::RenderSettings rs;
    rs.blend = cfg.blend;
    rs.depthTest = cfg.depthTest;
    rs.depthWrite = cfg.depthWrite;
    rs.programPointSize = true;
    particles.setRenderSettings(rs);

    ParticleSystem::UpdateSettings us;
    us.acceleration = cfg.acceleration;
    us.dampingBase = cfg.dampingBase;
    particles.setUpdateSettings(us);

    particles.setPointScale(cfg.pointScale);

    configured = true;
}

float LeechSeedProjectileVFX::rand01() {
    return engine::random::nextFloat01(rng);
}

float LeechSeedProjectileVFX::randRange(float a, float b) {
    if (b < a) std::swap(a, b);
    return a + (b - a) * rand01();
}

void LeechSeedProjectileVFX::update(float dt) {
    ensureConfigured();
    particles.update(dt);
}

void LeechSeedProjectileVFX::render(const Camera3D& camera) {
    ensureConfigured();
    particles.render(camera);
}

int LeechSeedProjectileVFX::resolveOriginNodeIndex(const Model& model) const {
    auto it = originNodeIndexCache.find(&model);
    if (it != originNodeIndexCache.end()) return it->second;

    int idx = -1;
    if (!cfg.originNodeName.empty()) {
        if (model.getNodeIndexByName(cfg.originNodeName, idx)) {
            originNodeIndexCache[&model] = idx;
            return idx;
        }
    }

    idx = cfg.originNodeIndex;
    originNodeIndexCache[&model] = idx;
    return idx;
}

glm::mat4 LeechSeedProjectileVFX::computeInstanceTransform(const PokemonInstance& instance) const {
    float scaleFactor = 1.0f;
    if (instance.model) scaleFactor = instance.model->getScaleFactor();

    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
    glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));

    glm::vec3 renderPos = instance.position + glm::vec3(0.0f, instance.visualYOffset, 0.0f);
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);

    return translation * rotationY * rotationX * rotationZ * scale;
}

glm::vec3 LeechSeedProjectileVFX::computeOriginWorld(const PokemonInstance& attacker) const {
    const float yaw = glm::radians(attacker.rotation.y);
    const glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
    const glm::vec3 back = -forward;

    if (!attacker.model) {
        return attacker.position
            + glm::vec3(0.0f, attacker.visualYOffset + cfg.worldUpOffset, 0.0f)
            + back * cfg.worldBackOffset;
    }

    const int nodeIdx = resolveOriginNodeIndex(*attacker.model);

    int animIdx = attacker.activeAnimIndex;
    if (animIdx < 0) animIdx = attacker.animIdleIndex;

    glm::mat4 nodeGlobal(1.0f);
    if (!attacker.model->getNodeGlobalTransformByIndex(attacker.animTimeSec, animIdx, nodeIdx, nodeGlobal)) {
        if (!attacker.model->getNodeGlobalTransformByIndex(attacker.animTimeSec, attacker.animIdleIndex, nodeIdx, nodeGlobal)) {
            return attacker.position
                + glm::vec3(0.0f, attacker.visualYOffset + cfg.worldUpOffset, 0.0f)
                + back * cfg.worldBackOffset;
        }
    }

    glm::mat4 instM = computeInstanceTransform(attacker);
    glm::mat4 nodeWorld = instM * nodeGlobal;

    glm::vec3 nodePosWorld = glm::vec3(nodeWorld[3]);
    glm::mat3 basis = orthonormalBasis(nodeWorld);

    glm::vec3 offsetWorld = basis * cfg.localOffset;

    nodePosWorld += offsetWorld;
    nodePosWorld.y += cfg.originWorldYOffset;
    nodePosWorld += back * cfg.worldBackOffset;
    nodePosWorld.y += cfg.worldUpOffset;

    return nodePosWorld;
}

void LeechSeedProjectileVFX::emit(const PokemonInstance& attacker,
                                  const PokemonInstance& target,
                                  float travelSec) {
    ensureConfigured();

    if (travelSec <= 0.0001f) return;

    glm::vec3 start = computeOriginWorld(attacker);

    glm::vec3 end = target.position + glm::vec3(0.0f, target.visualYOffset + cfg.impactYOffset, 0.0f);

    glm::vec3 delta = end - start;
    float dist2 = glm::dot(delta, delta);
    if (dist2 < 1e-6f) delta = glm::vec3(0.0f, 0.0f, 1.0f);

    const float T = std::max(0.01f, travelSec);

    glm::vec3 vel = delta * (1.0f / T);
    glm::vec3 accel(0.0f);

    // Arc with apex at mid-flight.
    if (cfg.arcHeight > 0.0001f) {
        const float y0 = start.y;
        const float y1 = end.y;
        const float yA = std::max(y0, y1) + cfg.arcHeight;

        const float invT = 1.0f / T;
        const float invT2 = invT * invT;

        const float ay = 4.0f * (y0 + y1 - 2.0f * yA) * invT2;
        const float v0y = (4.0f * yA - 3.0f * y0 - y1) * invT;

        vel.y = v0y;
        accel.y = ay;
    }

    glm::vec3 jitter(
        randRange(-cfg.spawnRadius, cfg.spawnRadius),
        randRange(-cfg.spawnRadius, cfg.spawnRadius),
        randRange(-cfg.spawnRadius, cfg.spawnRadius));

    ParticleSystem::Particle p;
    p.pos = start + jitter;
    p.vel = vel;
    p.accel = accel;
    p.maxLifeSec = travelSec;
    p.lifeSec = travelSec;
    p.sizePx = randRange(cfg.minSize, cfg.maxSize);
    p.seed = hash01(rand01() * 97.31f);

    particles.emit(p);
}
