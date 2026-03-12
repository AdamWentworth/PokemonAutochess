#include "game/runtime/startup/RuntimeBackendModelPrewarm.h"

#include <algorithm>
#include <ostream>

namespace game::runtime::backend_model_prewarm {

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
            std::ostream& out) {
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
                    out << "[Init][ModelCache][MISS] " << modelPath
                        << " reason=" << load.error << "\n";
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
                out << "[Init][ModelCache][OK] " << modelPath
                    << " vtx=" << load.mesh->vertices.size()
                    << " idx=" << load.mesh->indices.size()
                    << " submesh=" << load.mesh->submeshBaseTextures.size()
                    << "\n";
            }
        }

        maybeRenderBootProgress(callbacks, progressForIndex(i, totalModels));
    }

    out << "[Init] Backend model cache preload complete: loaded=" << summary.loaded
        << " failed=" << summary.failed << "\n";
    if (options.prewarmAnimRoles) {
        out << "[Init] Backend anim role prewarm complete: warmed="
            << summary.animRolesWarmed << "\n";
    }
    if (options.prewarmModelTextures) {
        out << "[Init] Backend model texture prewarm complete: materials="
            << summary.modelTextureMaterialsWarmed << "\n";
    }
    if (options.prewarmModelGeometry) {
        out << "[Init] Backend model geometry prewarm complete: cached_batches="
            << summary.modelGeometryBatchesWarmed << "\n";
    }
    if (summary.failed > 0u && !summary.failedSamples.empty() && !options.verboseModelCacheLog) {
        out << "[Init][ModelCache] Sample failures:\n";
        for (const std::string& item : summary.failedSamples) {
            out << "  - " << item << "\n";
        }
        out << "[Init][ModelCache] Set PAC_BACKEND_MODEL_VERBOSE=1 for full per-model cache logs.\n";
    }
    if (summary.preloadInterrupted) {
        out << "[Init][ModelCache] Preload interrupted by window close or quit request.\n";
    }

    if (callbacks.setTitle) {
        callbacks.setTitle("Pokemon Autochess");
    }
    if (callbacks.pumpPreloadEvents) {
        callbacks.pumpPreloadEvents();
    }

    return summary;
}

} // namespace game::runtime::backend_model_prewarm
