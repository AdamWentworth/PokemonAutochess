#pragma once

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

struct GameDataDb;

namespace game::runtime::startup_asset_prewarm {

struct TailFireStats {
    std::size_t legacyAtlases = 0u;
    std::size_t meshFlipbookCpu = 0u;
    std::size_t meshFlipbookGpu = 0u;
};

struct Options {
    bool usesBackendRenderPath = false;
    bool uiSpritePrewarmEnabled = false;
    int drawableW = 0;
    int drawableH = 0;
};

struct Summary {
    TailFireStats tailFire;
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
    std::function<void(const std::vector<std::string>&)> prewarmSpriteTextures;
    std::function<void(int, int, const std::vector<std::string>&)> prewarmBackendCardUi;
};

std::vector<std::string> collectUiSpritePrewarmPaths(const GameDataDb& dataDb);

Summary run(const Options& options,
            const std::vector<std::string>& uiSpritePrewarmPaths,
            const Callbacks& callbacks,
            std::ostream& out);

} // namespace game::runtime::startup_asset_prewarm
