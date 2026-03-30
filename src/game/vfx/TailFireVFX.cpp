#include "TailFireVFX.h"

#include "engine/render/Model.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAnchorMath.h"

#include <algorithm>
#include <cctype>

#include <glm/gtc/matrix_transform.hpp>

namespace anchor_math = game::runtime::shared_tail_fire_anchor_math;
namespace synth_emitter = game::runtime::shared_tail_fire_synth_emitter;

namespace {

std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

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

    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
    const glm::mat4 rotationX =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    const glm::mat4 rotationZ =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), instance.position);

    return translation * rotationY * rotationX * rotationZ * scale;
}

void TailFireVFX::ensureConfigured() {
    synth_emitter::ensureConfigured(synthEmitter, cfg);
}

void TailFireVFX::update(float dt,
                         const std::vector<PokemonInstance>& boardUnits,
                         const std::vector<PokemonInstance>& benchUnits) {
    ensureConfigured();

    synthEmitter.particles.update(dt);
    emitForList(dt, boardUnits);
    emitForList(dt, benchUnits);
}

void TailFireVFX::emitForList(float dt, const std::vector<PokemonInstance>& list) {
    dt = synth_emitter::clampStepDt(dt);
    if (dt <= 0.0f) return;

    for (const auto& unit : list) {
        if (!unit.alive || !unit.model) continue;
        if (filter && !filter(unit)) continue;

        int animIdx = unit.activeAnimIndex;
        if (animIdx < 0) animIdx = unit.animIdleIndex;

        const int emitCount = synth_emitter::beginUnitEmission(
            synthEmitter,
            cfg,
            unit.id,
            dt,
            animIdx,
            unit.animTimeSec);
        if (emitCount <= 0) {
            continue;
        }

        const auto getNodeGlobal =
            [&](int nodeIndex, glm::mat4& outNodeGlobal) -> bool {
            if (nodeIndex < 0) return false;
            if (unit.model->getNodeGlobalTransformByIndex(
                    unit.animTimeSec,
                    animIdx,
                    nodeIndex,
                    outNodeGlobal)) {
                return true;
            }
            return unit.model->getNodeGlobalTransformByIndex(
                unit.animTimeSec,
                unit.animIdleIndex,
                nodeIndex,
                outNodeGlobal);
        };

        const int tailNodeIndex = resolveTailTipNodeIndex(*unit.model);
        const int fireAnchorBaseNodeIndex =
            resolveOptionalNodeIndex(*unit.model, cfg.fireAnchorBaseNodeName, fireAnchorBaseNodeIndexCache);
        const int fireAnchorTipNodeIndex =
            resolveOptionalNodeIndex(*unit.model, cfg.fireAnchorTipNodeName, fireAnchorTipNodeIndexCache);

        const glm::mat4 instanceTransform = computeInstanceTransform(unit);
        glm::vec3 tailPosWorld(0.0f);
        glm::mat3 tailBasis(1.0f);
        glm::vec3 backDirWorld(0.0f, 1.0f, 0.0f);

        glm::mat4 baseNodeGlobal(1.0f);
        glm::mat4 tipNodeGlobal(1.0f);
        if (fireAnchorBaseNodeIndex >= 0 &&
            fireAnchorTipNodeIndex >= 0 &&
            getNodeGlobal(fireAnchorBaseNodeIndex, baseNodeGlobal) &&
            getNodeGlobal(fireAnchorTipNodeIndex, tipNodeGlobal)) {
            const auto frame = anchor_math::buildExactFireAnchorFrame(
                instanceTransform * baseNodeGlobal,
                instanceTransform * tipNodeGlobal,
                cfg.backDir);
            const float fireAxisLength = glm::length(frame.tipPosWorld - frame.posWorld);
            const glm::vec3 fireUp =
                anchor_math::safeNormOr(glm::vec3(frame.basis[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            tailPosWorld = frame.posWorld - fireUp * (fireAxisLength * 0.06f);
            tailBasis = frame.basis;
            backDirWorld = frame.backDirWorld;
        } else {
            glm::mat4 tailNodeGlobal(1.0f);
            if (!getNodeGlobal(tailNodeIndex, tailNodeGlobal)) {
                continue;
            }

            const auto frame = anchor_math::buildTailTipAnchorFrame(
                instanceTransform * tailNodeGlobal,
                cfg.backDir);
            tailPosWorld = frame.posWorld;
            tailPosWorld.y += cfg.tailWorldYOffset;
            tailBasis = frame.basis;
            backDirWorld = frame.backDirWorld;
        }

        const glm::vec3 anchorWorld =
            synth_emitter::resolveSmoothedAnchor(synthEmitter, cfg, unit.id, tailPosWorld, dt);
        const glm::vec3 tailVelocity =
            synth_emitter::resolveFilteredTailVelocity(synthEmitter, unit.id, tailPosWorld, dt);

        float particleScale = unit.model ? unit.model->getScaleFactor() : 1.0f;
        if (cfg.useUnitScaleChain) {
            particleScale *= std::max(0.0f, unit.modelScaleCorrection);
            particleScale *= std::max(0.0f, unit.speciesScale);
            particleScale *= std::max(0.0f, unit.visualScale);
            particleScale *= std::max(0.0f, unit.captureScale);
        }

        synth_emitter::emitParticles(
            synthEmitter,
            cfg,
            {
                .unitId = unit.id,
                .anchorWorld = anchorWorld,
                .tailBasis = tailBasis,
                .backDirWorld = backDirWorld,
                .tailVelocity = tailVelocity,
                .particleScale = particleScale,
            },
            emitCount);
    }
}

void TailFireVFX::render(const Camera3D& camera) {
    synthEmitter.particles.render(camera);
}
