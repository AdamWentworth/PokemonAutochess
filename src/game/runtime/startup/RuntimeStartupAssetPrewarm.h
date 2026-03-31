#pragma once

#include "engine/utils/LogSink.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct GameDataDb;

namespace game::runtime::startup_asset_prewarm {

struct TailFireStats {
    std::size_t legacyAtlases = 0u;
    std::size_t meshFlipbookCpu = 0u;
    std::size_t meshFlipbookGpu = 0u;
};

struct GrowlStats {
    std::size_t drawPasses = 0u;
    std::size_t bakedTextures = 0u;
    std::size_t warmedBatches = 0u;
};

struct ParticleVfxStats {
    std::size_t textures = 0u;
    std::size_t warmedBatches = 0u;
};

struct Options {
    bool usesBackendRenderPath = false;
    bool uiSpritePrewarmEnabled = false;
    int drawableW = 0;
    int drawableH = 0;
};

struct Summary {
    TailFireStats tailFire;
    GrowlStats growl;
    ParticleVfxStats particleVfx;
    std::size_t uiSpritesRequested = 0u;
    std::size_t cardArtRequested = 0u;
    bool cardUiPrewarmed = false;
    bool interrupted = false;
};

struct Callbacks {
    std::function<void(const std::string&)> setTitle;
    std::function<void(float)> renderBootLoading;
    std::function<bool()> pumpPreloadEvents;
    std::function<void()> requestQuit;
    std::function<void()> prewarmWorldShading;
    std::function<TailFireStats()> prewarmTailFire;
    std::function<GrowlStats()> prewarmGrowlVfx;
    std::function<ParticleVfxStats()> prewarmParticleVfx;
    std::function<void(const std::vector<std::string>&)> prewarmSpriteTextures;
    std::function<void(int, int, const std::vector<std::string>&)> prewarmBackendCardUi;
};

std::vector<std::string> collectUiSpritePrewarmPaths(const GameDataDb& dataDb);

Summary run(const Options& options,
            const std::vector<std::string>& uiSpritePrewarmPaths,
            const Callbacks& callbacks,
            const engine::log::Sink& log);

} // namespace game::runtime::startup_asset_prewarm
