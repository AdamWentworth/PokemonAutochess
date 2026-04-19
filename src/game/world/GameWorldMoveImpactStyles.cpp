#include "game/world/GameWorld.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

#include "game/config/GameDataDb.h"
#include "game/logging/ScratchPerfTrace.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/world/MoveImpactMath.h"

namespace {

std::string resolveBackendModelPathLocal(const PokemonInstance& unit, const GameDataDb* data) {
    if (!unit.backendModelPath.empty()) return unit.backendModelPath;
    if (!unit.animIndexCacheSourceModelPath.empty()) return unit.animIndexCacheSourceModelPath;
    if (!unit.backendAnimDurationsSourceModelPath.empty()) {
        return unit.backendAnimDurationsSourceModelPath;
    }
    if (!data) return {};

    const PokemonStats* stats = data->pokemon.getStats(unit.name);
    if (!stats || stats->model.empty()) return {};
    return "assets/models/" + stats->model;
}

const game::runtime::render_model::MeshData* tryResolveImpactMeshLocal(const PokemonInstance& unit,
                                                                       const GameDataDb* data) {
    const std::string modelPath = resolveBackendModelPathLocal(unit, data);
    if (modelPath.empty()) return nullptr;

    struct CacheEntry {
        bool attemptedLoad = false;
        game::runtime::render_model::MeshData mesh;
    };

    static std::unordered_map<std::string, CacheEntry> cache;
    auto& entry = cache[modelPath];
    if (!entry.attemptedLoad) {
        entry.attemptedLoad = true;
        std::string err;
        if (!game::runtime::render_model::loadMeshFromCache(modelPath, entry.mesh, &err)) {
            entry.mesh = game::runtime::render_model::MeshData{};
        }
    }

    if (entry.mesh.vertices.empty() || entry.mesh.indices.size() < 3u) {
        return nullptr;
    }
    return &entry.mesh;
}

} // namespace

void GameWorld::emitClawSwipeImpact(const PokemonInstance& target,
                                    const PokemonInstance* attacker,
                                    const glm::vec3& forward,
                                    bool metallic) {
    const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);

    if (!metallic) {
        using Clock = std::chrono::steady_clock;
        const bool traceScratch = game::scratch_trace::isTerminalModeEnabled(engineServices);
        const auto traceStart = Clock::now();
        double targetMeshMs = 0.0;
        double attackerMeshMs = 0.0;
        double surfacePointMs = 0.0;
        double emitMs = 0.0;

        if (!scratchGlowVfxInitialized) {
            ScratchGlowVFX::Config defaultConfig = ScratchGlowVFX::makeGameplayConfig();
            scratchGlowVfx.setConfig(defaultConfig);
            scratchGlowVfxInitialized = true;
        }

        const auto targetMeshStart = traceScratch ? Clock::now() : Clock::time_point{};
        const auto* targetMesh = tryResolveImpactMeshLocal(target, data);
        if (traceScratch) {
            targetMeshMs =
                std::chrono::duration<double, std::milli>(Clock::now() - targetMeshStart).count();
        }
        const auto attackerMeshStart = traceScratch ? Clock::now() : Clock::time_point{};
        const auto* attackerMesh =
            attacker ? tryResolveImpactMeshLocal(*attacker, data) : nullptr;
        if (traceScratch) {
            attackerMeshMs = std::chrono::duration<double, std::milli>(
                                 Clock::now() - attackerMeshStart)
                                 .count();
        }
        const auto surfacePointStart = traceScratch ? Clock::now() : Clock::time_point{};
        const MoveImpactSurfacePoint impact =
            computeTargetSurfaceImpactPoint(target, attacker, targetMesh, attackerMesh);
        if (traceScratch) {
            surfacePointMs = std::chrono::duration<double, std::milli>(
                                 Clock::now() - surfacePointStart)
                                 .count();
        }
        const auto emitStart = traceScratch ? Clock::now() : Clock::time_point{};
        scratchGlowVfx.emitAt(
            impact.position,
            impact.forward,
            hasLastViewMatrix ? &lastViewMatrix : nullptr);
        if (traceScratch) {
            emitMs =
                std::chrono::duration<double, std::milli>(Clock::now() - emitStart).count();
            std::ostringstream trace;
            trace << std::fixed << std::setprecision(2)
                  << "attacker=" << (attacker ? attacker->id : -1)
                  << " target=" << target.id
                  << " target_mesh=" << (targetMesh ? 1 : 0)
                  << " attacker_mesh=" << (attackerMesh ? 1 : 0)
                  << " surface_mode=" << (impact.usedMeshSurface ? "mesh_surface" : "fallback_bounds")
                  << " target_mesh_resolve=" << targetMeshMs << "ms"
                  << " attacker_mesh_resolve=" << attackerMeshMs << "ms"
                  << " surface_point=" << surfacePointMs << "ms"
                  << " emit=" << emitMs << "ms"
                  << " total=" <<
                         std::chrono::duration<double, std::milli>(Clock::now() - traceStart).count()
                  << "ms";
            game::scratch_trace::emit(log, "world_scratch_impact", trace.str());
        }
        return;
    }

    if (!clawSwipeVfxInitialized) {
        ClawSwipeVFX::Config defaultConfig;
        clawSwipeVfx.setConfig(defaultConfig);
        clawSwipeVfxInitialized = true;
    }

    clawSwipeVfx.emitAt(base, forward, true);
}

void GameWorld::emitAquaSwooshImpact(const PokemonInstance& target,
                                     const PokemonInstance* attacker,
                                     const glm::vec3& forward,
                                     AquaSwooshVFX::Style style,
                                     bool originFromAttacker) {
    if (!aquaSwooshVfxInitialized) {
        AquaSwooshVFX::Config defaultConfig;
        aquaSwooshVfx.setConfig(defaultConfig);
        aquaSwooshVfxInitialized = true;
    }

    const glm::vec3 base =
        (originFromAttacker && attacker)
            ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
            : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));
    aquaSwooshVfx.emitAt(base, forward, style);
}
