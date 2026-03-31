#include <sstream>
#include <string>
#include <vector>

#include "engine/utils/LogSink.h"
#include "game/runtime/startup/RuntimeRenderModelPrewarm.h"

bool test_runtime_render_model_prewarm_contract(std::string& outFail) {
    using game::runtime::render_model::MeshData;
    using game::runtime::render_model_prewarm::Callbacks;
    using game::runtime::render_model_prewarm::ModelLoadResult;
    using game::runtime::render_model_prewarm::Options;

    {
        MeshData mesh;
        mesh.vertices.resize(4u);
        mesh.indices.resize(6u);
        mesh.submeshBaseTextures.resize(2u);

        std::vector<std::string> titles;
        std::vector<float> progressValues;
        int requestQuitCalls = 0;
        int loadCalls = 0;
        std::ostringstream logs;
        engine::log::Sink log("TestRenderModelPrewarm", &logs, &logs);

        const auto summary = game::runtime::render_model_prewarm::run(
            {"assets/models/a.glb", "assets/models/b.glb", "assets/models/c.glb"},
            Options{
                .verboseModelCacheLog = false,
                .prewarmAnimRoles = true,
                .prewarmModelTextures = true,
                .prewarmModelGeometry = true,
                .maxFailureSamples = 8u,
            },
            Callbacks{
                .setTitle = [&](const std::string& title) { titles.push_back(title); },
                .renderBootLoading = [&](float progress) { progressValues.push_back(progress); },
                .pumpPreloadEvents = []() { return true; },
                .requestQuit = [&]() { ++requestQuitCalls; },
                .loadModel =
                    [&](const std::string& modelPath) {
                        ++loadCalls;
                        if (modelPath == "assets/models/a.glb") {
                            return ModelLoadResult{true, &mesh, {}};
                        }
                        if (modelPath == "assets/models/b.glb") {
                            return ModelLoadResult{false, &mesh, {}};
                        }
                        return ModelLoadResult{true, nullptr, "cache miss"};
                    },
                .prewarmAnimRoles =
                    [&](const std::string& modelPath, const MeshData&) {
                        return modelPath == "assets/models/a.glb";
                    },
                .prewarmTextures =
                    [&](const std::string&, const MeshData&) {
                        return std::size_t{3u};
                    },
                .prewarmGeometry =
                    [&](const MeshData&) {
                        return std::size_t{4u};
                    },
            },
            log);

        if (summary.loaded != 1u ||
            summary.failed != 1u ||
            summary.animRolesWarmed != 1u ||
            summary.modelTextureMaterialsWarmed != 6u ||
            summary.modelGeometryBatchesWarmed != 8u ||
            summary.preloadInterrupted ||
            summary.failedSamples.size() != 1u ||
            summary.failedSamples.front().find("assets/models/c.glb") == std::string::npos) {
            outFail = "run should preserve preload counts for fresh loads, cached loads, and failures.";
            return false;
        }

        if (requestQuitCalls != 0 || loadCalls != 3) {
            outFail = "run should not request quit during a successful preload pass and should visit each model once.";
            return false;
        }

        if (titles.size() < 5u ||
            titles.front() != "PokemonAutochess - Loading." ||
            titles[1].find("1/3  assets/models/a.glb") == std::string::npos ||
            titles.back() != "Pokemon Autochess") {
            outFail = "run should drive startup title updates for initial, per-model, and completion states.";
            return false;
        }

        if (progressValues.size() != 4u ||
            progressValues.front() != 0.0f ||
            progressValues.back() != 1.0f) {
            outFail = "run should report initial and per-model boot progress.";
            return false;
        }

        const std::string logText = logs.str();
        if (logText.find("Render model cache preload complete: loaded=1 failed=1") == std::string::npos ||
            logText.find("Backend anim role prewarm complete: warmed=1") == std::string::npos ||
            logText.find("Backend model texture prewarm complete: materials=6") == std::string::npos ||
            logText.find("Backend model geometry prewarm complete: cached_batches=8") == std::string::npos ||
            logText.find("Sample failures") == std::string::npos) {
            outFail = "run should keep the preload summary logs intact.";
            return false;
        }
    }

    {
        int requestQuitCalls = 0;
        int loadCalls = 0;
        std::ostringstream logs;
        engine::log::Sink log("TestRenderModelPrewarm", &logs, &logs);
        const auto summary = game::runtime::render_model_prewarm::run(
            {"assets/models/a.glb", "assets/models/b.glb"},
            Options{},
            Callbacks{
                .pumpPreloadEvents =
                    [calls = 0]() mutable {
                        ++calls;
                        return calls < 2;
                    },
                .requestQuit = [&]() { ++requestQuitCalls; },
                .loadModel =
                    [&](const std::string&) {
                        ++loadCalls;
                        return ModelLoadResult{};
                    },
            },
            log);

        if (!summary.preloadInterrupted ||
            requestQuitCalls != 1 ||
            loadCalls != 0 ||
            logs.str().find("Preload interrupted") == std::string::npos) {
            outFail = "run should stop cleanly when preload event pumping requests quit before model iteration begins.";
            return false;
        }
    }

    return true;
}
