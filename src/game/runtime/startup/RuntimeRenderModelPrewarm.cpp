#include "game/runtime/startup/RuntimeRenderModelPrewarm.h"

#include <algorithm>

namespace game::runtime::render_model_prewarm {

namespace {

void requestQuitIfNeeded(const Callbacks& callbacks) {
    if (callbacks.requestQuit) {
        callbacks.requestQuit();
    }
}

bool pumpPreloadEventsOrQuit(const Callbacks& callbacks) {
    if (!callbacks.pumpPreloadEvents) {
        return true;
    }
    if (callbacks.pumpPreloadEvents()) {
        return true;
    }
    requestQuitIfNeeded(callbacks);
    return false;
}

float progressForIndex(std::size_t index, std::size_t totalModels) {
    if (totalModels == 0u) {
        return 1.0f;
    }
    return static_cast<float>(index + 1u) / static_cast<float>(totalModels);
}

void maybeRenderBootProgress(const Callbacks& callbacks, float progress) {
    if (callbacks.renderBootLoading) {
        callbacks.renderBootLoading(progress);
    }
}

} // namespace

Summary run(const std::vector<std::string>& modelPathsToPreload,
            const Options& options,
            const Callbacks& callbacks,
            const engine::log::Sink& log) {
    Summary summary;
    summary.failedSamples.reserve(options.maxFailureSamples);

    if (!callbacks.loadModel) {
        return summary;
    }

    if (callbacks.setTitle) {
        callbacks.setTitle("PokemonAutochess - Loading.");
    }
    maybeRenderBootProgress(callbacks, 0.0f);
    if (!pumpPreloadEventsOrQuit(callbacks)) {
        summary.preloadInterrupted = true;
    }

    const std::size_t totalModels = modelPathsToPreload.size();
    for (std::size_t i = 0; i < totalModels && !summary.preloadInterrupted; ++i) {
        const std::string& modelPath = modelPathsToPreload[i];
        if (callbacks.setTitle) {
            callbacks.setTitle(
                std::string("PokemonAutochess - Loading ") +
                std::to_string(i + 1u) + "/" + std::to_string(totalModels) + "  " + modelPath);
        }
        if (!pumpPreloadEventsOrQuit(callbacks)) {
            summary.preloadInterrupted = true;
            break;
        }

        const ModelLoadResult load = callbacks.loadModel(modelPath);
        if (!load.error.empty()) {
            if (load.loadedFresh) {
                ++summary.failed;
                if (summary.failedSamples.size() < options.maxFailureSamples) {
                    summary.failedSamples.push_back(modelPath + " (" + load.error + ")");
                }
                if (options.verboseModelCacheLog) {
                    log.info("[Init][ModelCache][MISS] " + modelPath +
                             " reason=" + load.error);
                }
            }
            maybeRenderBootProgress(callbacks, progressForIndex(i, totalModels));
            continue;
        }

        if (load.loadedFresh) {
            ++summary.loaded;
        }

        if (load.mesh) {
            if (options.prewarmAnimRoles && callbacks.prewarmAnimRoles &&
                callbacks.prewarmAnimRoles(modelPath, *load.mesh)) {
                ++summary.animRolesWarmed;
            }
            if (options.prewarmModelTextures && callbacks.prewarmTextures) {
                summary.modelTextureMaterialsWarmed +=
                    callbacks.prewarmTextures(modelPath, *load.mesh);
            }
            if (options.prewarmModelGeometry && callbacks.prewarmGeometry) {
                summary.modelGeometryBatchesWarmed += callbacks.prewarmGeometry(*load.mesh);
            }
            if (load.loadedFresh && options.verboseModelCacheLog) {
                log.info("[Init][ModelCache][OK] " + modelPath +
                         " vtx=" + std::to_string(load.mesh->vertices.size()) +
                         " idx=" + std::to_string(load.mesh->indices.size()) +
                         " submesh=" + std::to_string(load.mesh->submeshBaseTextures.size()));
            }
        }

        maybeRenderBootProgress(callbacks, progressForIndex(i, totalModels));
    }

    log.info("[Init] Render model cache preload complete: loaded=" +
             std::to_string(summary.loaded) +
             " failed=" + std::to_string(summary.failed));
    if (options.prewarmAnimRoles) {
        log.info("[Init] Backend anim role prewarm complete: warmed=" +
                 std::to_string(summary.animRolesWarmed));
    }
    if (options.prewarmModelTextures) {
        log.info("[Init] Backend model texture prewarm complete: materials=" +
                 std::to_string(summary.modelTextureMaterialsWarmed));
    }
    if (options.prewarmModelGeometry) {
        log.info("[Init] Backend model geometry prewarm complete: cached_batches=" +
                 std::to_string(summary.modelGeometryBatchesWarmed));
    }
    if (summary.failed > 0u && !summary.failedSamples.empty() && !options.verboseModelCacheLog) {
        log.info("[Init][ModelCache] Sample failures:");
        for (const std::string& item : summary.failedSamples) {
            log.info("  - " + item);
        }
        log.info("[Init][ModelCache] Set PAC_BACKEND_MODEL_VERBOSE=1 for full per-model cache logs.");
    }
    if (summary.preloadInterrupted) {
        log.info("[Init][ModelCache] Preload interrupted by window close or quit request.");
    }

    if (callbacks.setTitle) {
        callbacks.setTitle("Pokemon Autochess");
    }
    if (callbacks.pumpPreloadEvents) {
        callbacks.pumpPreloadEvents();
    }

    return summary;
}

} // namespace game::runtime::render_model_prewarm
