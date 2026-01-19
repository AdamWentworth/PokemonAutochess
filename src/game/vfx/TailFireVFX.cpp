// --- FILE: src/game/vfx/TailFireVFX.cpp ---
// src/game/vfx/TailFireVFX.cpp
#include "TailFireVFX.h"

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

static std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

void TailFireVFX::setNameFilterCaseInsensitive(const std::string& nameLowerOrAnyCase) {
    const std::string want = toLowerAscii(nameLowerOrAnyCase);
    setFilter([want](const PokemonInstance& inst) {
        return toLowerAscii(inst.name) == want;
    });
}

glm::mat4 TailFireVFX::computeInstanceTransform(const PokemonInstance& instance) const {
    float scaleFactor = 1.0f;
    if (instance.model) scaleFactor = instance.model->getScaleFactor();

    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
    glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), instance.position);

    return translation * rotationY * rotationX * rotationZ * scale;
}

void TailFireVFX::ensureConfigured() {
    if (configured) return;

    // Shader
    particles.setShaderPaths(cfg.vertShaderPath, cfg.fragShaderPath);

    // Flipbook (optional)
    particles.setUseFlipbook(cfg.useFlipbook);
    if (cfg.useFlipbook) {
        particles.setFlipbook(cfg.flipbookPath,
                              cfg.flipbookCols,
                              cfg.flipbookRows,
                              cfg.flipbookFrames,
                              cfg.flipbookFps);

        // Optional secondary atlas for extra per-particle variation
        if (cfg.useFlipbook2) {
            particles.setSecondaryFlipbook(cfg.flipbook2Path,
                                          cfg.flipbook2Cols,
                                          cfg.flipbook2Rows,
                                          cfg.flipbook2Frames,
                                          cfg.flipbook2Fps);
        } else {
            particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
        }
    } else {
        // Ensure both atlases are disabled when procedural-only
        particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
    }

    // Render state
    ParticleSystem::RenderSettings rs;
    rs.blend = cfg.blend;
    rs.depthTest = cfg.depthTest;
    rs.depthWrite = cfg.depthWrite;
    rs.programPointSize = true;
    particles.setRenderSettings(rs);

    // Simulation
    ParticleSystem::UpdateSettings us;
    us.acceleration = cfg.acceleration;
    us.dampingBase = cfg.dampingBase;
    particles.setUpdateSettings(us);

    // Point scale
    particles.setPointScale(cfg.pointScale);

    configured = true;
}

void TailFireVFX::update(float dt,
                         const std::vector<PokemonInstance>& boardUnits,
                         const std::vector<PokemonInstance>& benchUnits)
{
    ensureConfigured();

    particles.update(dt);
    emitForList(dt, boardUnits);
    emitForList(dt, benchUnits);
}

void TailFireVFX::emitForList(float dt, const std::vector<PokemonInstance>& list) {
    dt = std::clamp(dt, 0.0f, 0.05f);

    for (const auto& u : list) {
        if (!u.alive || !u.model) continue;
        if (filter && !filter(u)) continue;

        float& acc = emitAccumulator[u.id];
        acc += dt * cfg.emitRatePerSec;

        int spawnCount = (int)std::floor(acc);
        if (spawnCount <= 0) continue;
        acc -= (float)spawnCount;

        glm::mat4 instM = computeInstanceTransform(u);

        glm::mat4 tailNodeGlobal(1.0f);
        glm::vec3 tailWorld(0.0f);

        if (u.model->getNodeGlobalTransformByIndex(u.animTimeSec, kLoopAnimIndex, cfg.tailTipNodeIndex, tailNodeGlobal)) {
            tailWorld = glm::vec3(instM * tailNodeGlobal * glm::vec4(0, 0, 0, 1));
        } else {
            tailWorld = glm::vec3(instM * glm::vec4(0.0f, 0.78f, -0.38f, 1.0f));
        }

        tailWorld.y += cfg.tailWorldYOffset;

        float scaleFactor = u.model ? u.model->getScaleFactor() : 1.0f;

        uint32_t& serial = spawnSerial[u.id];

        for (int i = 0; i < spawnCount; ++i) {
            float base = (float)u.id * 100000.0f + (float)(serial++);

            // Narrower spawn: squeeze X/Z so it reads less wide
            float rx = hashSigned(base + 1.0f) * cfg.spawnRadius * 0.75f;
            float ry = hash01(base + 2.0f) * cfg.spawnRadius * 0.35f;
            float rz = hashSigned(base + 3.0f) * cfg.spawnRadius * 0.75f;

            ParticleSystem::Particle p;
            p.pos = tailWorld + glm::vec3(rx, ry, rz);

            // Taller: more upward velocity, slightly less backward push
            float up   = 0.055f + hash01(base + 5.0f) * 0.095f;
            float back = 0.050f + hash01(base + 6.0f) * 0.050f;

            p.vel = glm::vec3(0.0f, up, 0.0f) + cfg.backDir * back;

            // Taller: live longer so the plume can extend upward
            p.maxLifeSec = 0.14f + hash01(base + 7.0f) * 0.10f;
            p.lifeSec = p.maxLifeSec;

            float sizeBase = 0.22f * scaleFactor;
            float sizeJit  = 0.10f * scaleFactor;
            p.sizePx = sizeBase + hash01(base + 8.0f) * sizeJit;

            p.seed = hash01(base + 9.0f);

            particles.emit(p);
        }
    }
}

void TailFireVFX::render(const Camera3D& camera) {
    particles.render(camera);
}
