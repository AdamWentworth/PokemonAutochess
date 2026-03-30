// src/game/vfx/TailFireVFX.cpp
#include "TailFireVFX.h"
#include "engine/render/Model.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

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

// Build a stable orthonormal basis from a mat4 (ignoring translation).
// This avoids "weird" particle rotations if the node matrix contains scale/shear.
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
    // Gram-Schmidt
    y = y - x * glm::dot(y, x);
    y = safeNorm(y);
    z = glm::cross(x, y);
    z = safeNorm(z);

    // Ensure right-handed basis
    if (glm::dot(glm::cross(x, y), z) < 0.0f) {
        z = -z;
    }

    // Columns basis
    return glm::mat3(x, y, z);
}

static glm::vec3 safeNormOr(glm::vec3 v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    if (len2 <= 1e-10f) return fallback;
    return v * (1.0f / std::sqrt(len2));
}

static glm::mat3 buildFireAnchorBasis(const glm::mat4& baseWorldM,
                                      const glm::vec3& basePosWorld,
                                      const glm::vec3& tipPosWorld) {
    const glm::vec3 upAxis =
        safeNormOr(tipPosWorld - basePosWorld,
                   safeNormOr(glm::vec3(baseWorldM[1]), glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 xHint = glm::vec3(baseWorldM[0]);
    xHint -= upAxis * glm::dot(xHint, upAxis);
    if (glm::dot(xHint, xHint) <= 1e-10f) {
        xHint = glm::vec3(baseWorldM[2]);
        xHint -= upAxis * glm::dot(xHint, upAxis);
    }

    glm::vec3 xFallback = (std::fabs(upAxis.y) < 0.95f)
        ? safeNormOr(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), upAxis), glm::vec3(1.0f, 0.0f, 0.0f))
        : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 xAxis = safeNormOr(xHint, xFallback);
    glm::vec3 zAxis = safeNormOr(glm::cross(xAxis, upAxis), glm::vec3(0.0f, 0.0f, 1.0f));
    xAxis = safeNormOr(glm::cross(upAxis, zAxis), xAxis);

    if (glm::dot(glm::cross(xAxis, upAxis), zAxis) < 0.0f) {
        zAxis = -zAxis;
    }
    return glm::mat3(xAxis, upAxis, zAxis);
}

int TailFireVFX::resolveOptionalNodeIndex(const Model& model,
                                          const std::string& nodeName,
                                          std::unordered_map<const Model*, int>& cache) const {
    auto it = cache.find(&model);
    if (it != cache.end()) return it->second;

    int idx = -1;
    if (!nodeName.empty()) {
        (void)model.getNodeIndexByName(nodeName, idx);
    }
    cache[&model] = idx;
    return idx;
}

int TailFireVFX::resolveTailTipNodeIndex(const Model& model) const {
    auto it = tailNodeIndexCache.find(&model);
    if (it != tailNodeIndexCache.end()) return it->second;

    const int idxByName = resolveOptionalNodeIndex(model, cfg.tailTipNodeName, tailNodeIndexCache);
    if (idxByName >= 0) {
        tailNodeIndexCache[&model] = idxByName;
        return idxByName;
    }

    const int idx = cfg.tailTipNodeIndex;
    tailNodeIndexCache[&model] = idx;
    return idx;
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
    if (cfg.useUnitScaleChain) {
        scaleFactor *= std::max(0.0f, instance.modelScaleCorrection);
        scaleFactor *= std::max(0.0f, instance.speciesScale);
        scaleFactor *= std::max(0.0f, instance.visualScale);
        scaleFactor *= std::max(0.0f, instance.captureScale);
    }

    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
    glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), instance.position);

    return translation * rotationY * rotationX * rotationZ * scale;
}

void TailFireVFX::ensureConfigured() {
    if (configured) return;

    particles.setShaderPaths(cfg.vertShaderPath, cfg.fragShaderPath);

    particles.setUseFlipbook(cfg.useFlipbook);
    if (cfg.useFlipbook) {
        particles.setFlipbook(cfg.flipbookPath,
                              cfg.flipbookCols,
                              cfg.flipbookRows,
                              cfg.flipbookFrames,
                              cfg.flipbookFps);

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
        particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
    }

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

        const bool calmerSingleFlipbook = !cfg.useFlipbook2;
        const float singleFlipbookEmitScale = calmerSingleFlipbook ? 0.45f : 1.0f;
        float& acc = emitAccumulator[u.id];
        acc += dt * cfg.emitRatePerSec * singleFlipbookEmitScale;

        int emitCount = (int)std::floor(acc);
        if (emitCount <= 0) continue;
        acc -= (float)emitCount;

        // Pick the animation index that is actually being rendered.
        int animIdx = u.activeAnimIndex;
        if (animIdx < 0) animIdx = u.animIdleIndex;

        // If animation changed since last frame, reset history so we don't get a fake velocity spike.
        {
            int& prevIdx = prevAnimIndex[u.id];
            if (prevIdx != animIdx) {
                prevIdx = animIdx;
                prevTailWorld.erase(u.id);
                smoothedTailWorld.erase(u.id);
                prevAnimTimeSec.erase(u.id);
                filteredTailVel.erase(u.id);
            }
        }

        // Detect time wrap for looping clips (t goes backwards).
        bool timeWrapped = false;
        {
            auto itT = prevAnimTimeSec.find(u.id);
            if (itT == prevAnimTimeSec.end()) {
                prevAnimTimeSec[u.id] = u.animTimeSec;
            } else {
                float prevT = itT->second;
                if (u.animTimeSec + 1e-4f < prevT) timeWrapped = true;
                itT->second = u.animTimeSec;
            }
        }
        if (timeWrapped) {
            prevTailWorld.erase(u.id);
            smoothedTailWorld.erase(u.id);
            filteredTailVel.erase(u.id);
        }

        auto getNodeGlobal = [&](int nodeIndex, glm::mat4& outNodeGlobal) -> bool {
            if (nodeIndex < 0) return false;
            if (u.model->getNodeGlobalTransformByIndex(u.animTimeSec, animIdx, nodeIndex, outNodeGlobal)) {
                return true;
            }
            return u.model->getNodeGlobalTransformByIndex(
                u.animTimeSec, u.animIdleIndex, nodeIndex, outNodeGlobal);
        };

        const int tailNodeIndex = resolveTailTipNodeIndex(*u.model);
        const int fireAnchorBaseNodeIndex =
            resolveOptionalNodeIndex(*u.model, cfg.fireAnchorBaseNodeName, fireAnchorBaseNodeIndexCache);
        const int fireAnchorTipNodeIndex =
            resolveOptionalNodeIndex(*u.model, cfg.fireAnchorTipNodeName, fireAnchorTipNodeIndexCache);

        // Model -> world
        const glm::mat4 instM = computeInstanceTransform(u);

        glm::vec3 tailPosWorld(0.0f);
        glm::mat3 tailBasis(1.0f);
        glm::mat4 baseNodeGlobal(1.0f);
        glm::mat4 tipNodeGlobal(1.0f);
        if (fireAnchorBaseNodeIndex >= 0 &&
            fireAnchorTipNodeIndex >= 0 &&
            getNodeGlobal(fireAnchorBaseNodeIndex, baseNodeGlobal) &&
            getNodeGlobal(fireAnchorTipNodeIndex, tipNodeGlobal)) {
            const glm::mat4 baseWorldM = instM * baseNodeGlobal;
            const glm::mat4 tipWorldM = instM * tipNodeGlobal;
            const glm::vec3 basePosWorld = glm::vec3(baseWorldM[3]);
            const glm::vec3 tipPosWorld = glm::vec3(tipWorldM[3]);
            tailBasis = buildFireAnchorBasis(baseWorldM, basePosWorld, tipPosWorld);
            const float fireAxisLength = glm::length(tipPosWorld - basePosWorld);
            const glm::vec3 fireUp =
                safeNormOr(glm::vec3(tailBasis[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            tailPosWorld = basePosWorld - fireUp * (fireAxisLength * 0.06f);
        } else {
            glm::mat4 tailNodeGlobal(1.0f);
            if (!getNodeGlobal(tailNodeIndex, tailNodeGlobal)) {
                continue;
            }

            const glm::mat4 tailWorldM = instM * tailNodeGlobal;
            tailPosWorld = glm::vec3(tailWorldM[3]);
            tailPosWorld.y += cfg.tailWorldYOffset;
            tailBasis = orthonormalBasis(tailWorldM);
        }

        // Rotate the "back direction" with the tail
        glm::vec3 backDirWorld = tailBasis * cfg.backDir;
        float bd2 = glm::dot(backDirWorld, backDirWorld);
        if (bd2 > 1e-10f) backDirWorld *= (1.0f / std::sqrt(bd2));
        else backDirWorld = glm::vec3(0, 0, 1);

        // Anchor smoothing (optional)
        glm::vec3 anchor = tailPosWorld;
        if (cfg.followSmoothing > 0.0f) {
            glm::vec3& s = smoothedTailWorld[u.id];
            if (smoothedTailWorld.find(u.id) == smoothedTailWorld.end()) {
                s = tailPosWorld;
            }
            float a = 1.0f - std::exp(-cfg.followSmoothing * dt);
            s = (1.0f - a) * s + a * tailPosWorld;
            anchor = s;
        }

        // Tail tip velocity (robust): discontinuity guard + low-pass filter + cap
        glm::vec3 tailVel(0.0f);
        {
            auto itPrev = prevTailWorld.find(u.id);
            if (itPrev != prevTailWorld.end()) {
                glm::vec3 prev = itPrev->second;
                glm::vec3 delta = (tailPosWorld - prev);

                // tighter threshold now; timeWrapped handles loop resets
                const float maxDeltaPerFrame = 0.20f;
                bool discontinuity = (glm::dot(delta, delta) > maxDeltaPerFrame * maxDeltaPerFrame);

                if (!discontinuity) {
                    float invDt = (dt > 1e-6f) ? (1.0f / dt) : 0.0f;
                    glm::vec3 rawVel = delta * invDt;

                    // cap raw vel before filtering
                    const float maxTailVel = 4.0f;
                    float sp2 = glm::dot(rawVel, rawVel);
                    if (sp2 > maxTailVel * maxTailVel) {
                        rawVel *= (maxTailVel / std::sqrt(sp2));
                    }

                    // low-pass filter
                    glm::vec3& vFilt = filteredTailVel[u.id];
                    if (filteredTailVel.find(u.id) == filteredTailVel.end()) vFilt = rawVel;

                    const float k = 25.0f; // higher follows more; lower smooths more
                    float a = 1.0f - std::exp(-k * dt);
                    vFilt = (1.0f - a) * vFilt + a * rawVel;

                    tailVel = vFilt;
                } else {
                    filteredTailVel.erase(u.id);
                    tailVel = glm::vec3(0.0f);
                }
            }
            prevTailWorld[u.id] = tailPosWorld;
        }

        // Emit particles
        uint32_t& serial = spawnSerial[u.id];

        const float unitSeed = hash01(static_cast<float>(u.id) * 13.137f + 0.417f);

        for (int k = 0; k < emitCount; ++k) {
            const float base = (float)(serial++);

            const float jitterScale = calmerSingleFlipbook ? 0.14f : 1.0f;
            float rx = hashSigned(base + 1.0f) * cfg.spawnRadius * jitterScale;
            float ry = hashSigned(base + 2.0f) * cfg.spawnRadius * jitterScale;
            float rz = hashSigned(base + 3.0f) * cfg.spawnRadius * jitterScale;

            glm::vec3 localJitter(rx, ry, rz);
            glm::vec3 worldJitter = tailBasis * localJitter;

            ParticleSystem::Particle p;
            p.pos = anchor + worldJitter;

            float up =
                (calmerSingleFlipbook ? 0.006f : 0.055f) +
                hash01(base + 5.0f) * (calmerSingleFlipbook ? 0.010f : 0.095f);
            float back =
                (calmerSingleFlipbook ? 0.002f : 0.050f) +
                hash01(base + 6.0f) * (calmerSingleFlipbook ? 0.006f : 0.050f);

            // base motion + rotate back-dir with tail
            p.vel = glm::vec3(0.0f, up, 0.0f) + backDirWorld * back;

            // inherit tail tip motion, but clamp the inherited component
            const float inheritVelocity = calmerSingleFlipbook ? 0.0f : cfg.inheritVelocity;
            if (inheritVelocity != 0.0f) {
                glm::vec3 inh = tailVel * inheritVelocity;

                const float maxInherit = 2.5f;
                float inh2 = glm::dot(inh, inh);
                if (inh2 > maxInherit * maxInherit) {
                    inh *= (maxInherit / std::sqrt(inh2));
                }

                p.vel += inh;
            }

            p.maxLifeSec =
                (calmerSingleFlipbook ? 0.045f : 0.14f) +
                hash01(base + 7.0f) * (calmerSingleFlipbook ? 0.020f : 0.10f);
            p.lifeSec = p.maxLifeSec;

            float scaleFactor = u.model ? u.model->getScaleFactor() : 1.0f;
            float sizeBase = (calmerSingleFlipbook ? 0.30f : 0.22f) * scaleFactor;
            float sizeJit  = (calmerSingleFlipbook ? 0.015f : 0.10f) * scaleFactor;
            p.sizePx = sizeBase + hash01(base + 8.0f) * sizeJit;

            p.seed = calmerSingleFlipbook ? unitSeed : hash01(base + 9.0f);

            particles.emit(p);
        }
    }
}

void TailFireVFX::render(const Camera3D& camera) {
    particles.render(camera);
}
