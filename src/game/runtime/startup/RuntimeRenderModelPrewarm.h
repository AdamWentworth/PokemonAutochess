#pragma once

#include "engine/utils/LogSink.h"

#include "game/runtime/render_model_cache/RenderModelCache.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace game::runtime::render_model_prewarm {

struct Options {
    bool verboseModelCacheLog = false;
    bool prewarmAnimRoles = false;
    bool prewarmModelTextures = false;
    bool prewarmModelGeometry = false;
    std::size_t maxFailureSamples = 8u;
};

struct ModelLoadResult {
    bool loadedFresh = false;
    const render_model::MeshData* mesh = nullptr;
    std::string error;
};

struct Summary {
    std::size_t loaded = 0u;
    std::size_t failed = 0u;
    std::size_t animRolesWarmed = 0u;
    std::size_t modelTextureMaterialsWarmed = 0u;
    std::size_t modelGeometryBatchesWarmed = 0u;
    bool preloadInterrupted = false;
    std::vector<std::string> failedSamples;
};

struct Callbacks {
    std::function<void(const std::string&)> setTitle;
    std::function<void(float)> renderBootLoading;
    std::function<bool()> pumpPreloadEvents;
    std::function<void()> requestQuit;
    std::function<ModelLoadResult(const std::string&)> loadModel;
    std::function<bool(const std::string&, const render_model::MeshData&)> prewarmAnimRoles;
    std::function<std::size_t(const std::string&, const render_model::MeshData&)> prewarmTextures;
    std::function<std::size_t(const render_model::MeshData&)> prewarmGeometry;
};

Summary run(const std::vector<std::string>& modelPathsToPreload,
            const Options& options,
            const Callbacks& callbacks,
            const engine::log::Sink& log);

} // namespace game::runtime::render_model_prewarm
