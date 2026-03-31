#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"

#include "engine/core/Environment.h"
#include "engine/render/SpriteTextureCardArt.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/ui/ImagePath.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace game::runtime::startup_asset_prewarm {

namespace {

bool pumpPreloadEventsOrQuit(const Callbacks& callbacks) {
    if (!callbacks.pumpPreloadEvents) {
        return true;
    }
    if (callbacks.pumpPreloadEvents()) {
        return true;
    }
    if (callbacks.requestQuit) {
        callbacks.requestQuit();
    }
    return false;
}

bool isPlaceholderOrHudSprite(const std::string& path) {
    return path == "assets/ui/frame_gold.png" ||
           path == "assets/images/item_placeholder.png" ||
           path == "assets/images/items_atlas.png" ||
           path == "assets/images/pokedollar.png" ||
           path == "assets/images/pokegold.png";
}

std::uintmax_t backendCardArtEagerPrewarmBytes() {
    static const std::uintmax_t limit = []() -> std::uintmax_t {
        constexpr std::uintmax_t kDefault = 1000000u;
        constexpr std::uintmax_t kMin = 0u;
        constexpr std::uintmax_t kMax = 16u * 1024u * 1024u;
        const auto env = engine::env::get("PAC_BACKEND_CARD_ART_EAGER_BYTES");
        if (!env.has_value()) return kDefault;
        try {
            return std::clamp<std::uintmax_t>(
                static_cast<std::uintmax_t>(std::stoull(*env)),
                kMin,
                kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return limit;
}

std::vector<std::string> collectBackendCardArtProxyPrewarmPaths(
    const std::vector<std::string>& uiSpritePrewarmPaths) {
    std::vector<std::string> out;
    out.reserve(uiSpritePrewarmPaths.size() + 4u);
    std::unordered_set<std::string> seen;
    seen.reserve(uiSpritePrewarmPaths.size() + 4u);
    const auto addIfWanted = [&](const std::string& path) {
        if (isPlaceholderOrHudSprite(path)) return;
        const bool classicStarterPortrait =
            path == "assets/images/bulbasaur.png" ||
            path == "assets/images/charmander.png" ||
            path == "assets/images/squirtle.png";
        std::error_code ec;
        const std::uintmax_t bytes = std::filesystem::file_size(path, ec);
        if (ec) return;
        if (!classicStarterPortrait && bytes < backendCardArtEagerPrewarmBytes()) return;
        if (!seen.insert(path).second) return;
        out.push_back(engine::render::sprite_card_art::makeProxyPath(path));
    };

    for (const std::string& path : uiSpritePrewarmPaths) {
        addIfWanted(path);
    }

    addIfWanted("assets/images/bulbasaur.png");
    addIfWanted("assets/images/charmander.png");
    addIfWanted("assets/images/squirtle.png");

    std::sort(out.begin(), out.end());
    return out;
}

std::string formatMs(double ms) {
    std::ostringstream timing;
    timing << std::fixed << std::setprecision(1) << ms;
    return timing.str();
}

} // namespace

std::vector<std::string> collectUiSpritePrewarmPaths(const GameDataDb& dataDb) {
    std::vector<std::string> paths;
    paths.reserve(32u);
    std::unordered_set<std::string> seenPaths;
    const auto addPath = [&](const std::string& path) {
        if (path.empty()) return;
        if (!seenPaths.insert(path).second) return;
        paths.push_back(path);
    };

    addPath("assets/ui/frame_gold.png");
    addPath("assets/images/item_placeholder.png");
    addPath("assets/images/items_atlas.png");
    addPath("assets/images/pokedollar.png");
    addPath("assets/images/pokegold.png");

    for (const auto& [speciesName, stats] : dataDb.pokemon.all()) {
        (void)stats;
        const std::string path =
            game::runtime::ui_images::candidatePokemonPortraitPath(speciesName);
        if (path.empty()) continue;
        if (!game::runtime::ui_images::fileExistsCached(path)) continue;
        addPath(path);
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

Summary run(const Options& options,
            const std::vector<std::string>& uiSpritePrewarmPaths,
            const Callbacks& callbacks,
            const engine::log::Sink& log) {
    Summary summary;
    if (!options.usesBackendRenderPath) {
        return summary;
    }

    if (callbacks.prewarmWorldShading) {
        if (callbacks.setTitle) callbacks.setTitle("PokemonAutochess - Loading world shading...");
        if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.92f);
        const auto t0 = std::chrono::high_resolution_clock::now();
        callbacks.prewarmWorldShading();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        log.info("[Init] Backend world shading prewarm complete: time=" +
                 formatMs(ms) + "ms");
        if (!pumpPreloadEventsOrQuit(callbacks)) {
            summary.interrupted = true;
            return summary;
        }
    }

    if (callbacks.prewarmTailFire) {
        if (callbacks.setTitle) callbacks.setTitle("PokemonAutochess - Loading tail fire...");
        if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.93f);
        const auto t0 = std::chrono::high_resolution_clock::now();
        summary.tailFire = callbacks.prewarmTailFire();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        log.info("[Init] Backend tail fire prewarm complete: atlases=" +
                 std::to_string(summary.tailFire.legacyAtlases) +
                 " mesh_flipbook_cpu=" + std::to_string(summary.tailFire.meshFlipbookCpu) +
                 " mesh_flipbook_gpu=" + std::to_string(summary.tailFire.meshFlipbookGpu) +
                 " time=" + formatMs(ms) + "ms");
        if (!pumpPreloadEventsOrQuit(callbacks)) {
            summary.interrupted = true;
            return summary;
        }
    }

    if (callbacks.prewarmGrowlVfx) {
        if (callbacks.setTitle) callbacks.setTitle("PokemonAutochess - Loading growl VFX...");
        if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.935f);
        const auto t0 = std::chrono::high_resolution_clock::now();
        summary.growl = callbacks.prewarmGrowlVfx();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        log.info("[Init] Backend growl VFX prewarm complete: passes=" +
                 std::to_string(summary.growl.drawPasses) +
                 " baked_textures=" + std::to_string(summary.growl.bakedTextures) +
                 " warmed_batches=" + std::to_string(summary.growl.warmedBatches) +
                 " time=" + formatMs(ms) + "ms");
        if (!pumpPreloadEventsOrQuit(callbacks)) {
            summary.interrupted = true;
            return summary;
        }
    }

    if (callbacks.prewarmParticleVfx) {
        if (callbacks.setTitle) callbacks.setTitle("PokemonAutochess - Loading particle VFX...");
        if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.937f);
        const auto t0 = std::chrono::high_resolution_clock::now();
        summary.particleVfx = callbacks.prewarmParticleVfx();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        log.info("[Init] Backend particle VFX prewarm complete: textures=" +
                 std::to_string(summary.particleVfx.textures) +
                 " warmed_batches=" + std::to_string(summary.particleVfx.warmedBatches) +
                 " time=" + formatMs(ms) + "ms");
        if (!pumpPreloadEventsOrQuit(callbacks)) {
            summary.interrupted = true;
            return summary;
        }
    }

    if (!options.uiSpritePrewarmEnabled || !callbacks.prewarmSpriteTextures) {
        return summary;
    }

    const bool backendCardUiWillPrewarm =
        options.drawableW > 0 && options.drawableH > 0 &&
        static_cast<bool>(callbacks.prewarmBackendCardUi);

    if (callbacks.setTitle) callbacks.setTitle("PokemonAutochess - Loading UI sprites...");
    if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.94f);

    std::vector<std::string> genericUiSpritePrewarmPaths;
    genericUiSpritePrewarmPaths.reserve(uiSpritePrewarmPaths.size());
    for (const std::string& path : uiSpritePrewarmPaths) {
        if (backendCardUiWillPrewarm && !isPlaceholderOrHudSprite(path)) {
            continue;
        }
        genericUiSpritePrewarmPaths.push_back(path);
    }

    summary.uiSpritesRequested = genericUiSpritePrewarmPaths.size();
    if (callbacks.setTitle && summary.uiSpritesRequested > 0u) {
        callbacks.setTitle(
            std::string("PokemonAutochess - Loading UI sprites (") +
            std::to_string(summary.uiSpritesRequested) + ")");
    }

    const auto t0 = std::chrono::high_resolution_clock::now();
    if (!genericUiSpritePrewarmPaths.empty()) {
        callbacks.prewarmSpriteTextures(genericUiSpritePrewarmPaths);
    }
    if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.98f);
    if (!pumpPreloadEventsOrQuit(callbacks)) {
        summary.interrupted = true;
        return summary;
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double uiSpriteMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    log.info("[Init] UI sprite prewarm complete: requested=" +
             std::to_string(summary.uiSpritesRequested) +
             " time=" + formatMs(uiSpriteMs) + "ms");

    if (backendCardUiWillPrewarm) {
        if (callbacks.setTitle) callbacks.setTitle("PokemonAutochess - Loading card art...");
        if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.975f);
        const std::vector<std::string> cardArtProxyPaths =
            collectBackendCardArtProxyPrewarmPaths(uiSpritePrewarmPaths);
        summary.cardArtRequested = cardArtProxyPaths.size();
        const auto tArt0 = std::chrono::high_resolution_clock::now();
        if (!cardArtProxyPaths.empty()) {
            callbacks.prewarmSpriteTextures(cardArtProxyPaths);
        }
        if (!pumpPreloadEventsOrQuit(callbacks)) {
            summary.interrupted = true;
            return summary;
        }
        const auto tArt1 = std::chrono::high_resolution_clock::now();
        const double cardArtMs =
            std::chrono::duration<double, std::milli>(tArt1 - tArt0).count();
        log.info("[Init] UI card art prewarm complete: requested=" +
                 std::to_string(summary.cardArtRequested) +
                 " time=" + formatMs(cardArtMs) + "ms");
    }

    if (!uiSpritePrewarmPaths.empty() && backendCardUiWillPrewarm) {
        if (callbacks.setTitle) callbacks.setTitle("PokemonAutochess - Loading UI cards...");
        if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.985f);
        log.info("[Init] UI card prewarm begin");
        const auto tCardUi0 = std::chrono::high_resolution_clock::now();
        callbacks.prewarmBackendCardUi(options.drawableW, options.drawableH, uiSpritePrewarmPaths);
        const auto tCardUi1 = std::chrono::high_resolution_clock::now();
        const double cardUiMs =
            std::chrono::duration<double, std::milli>(tCardUi1 - tCardUi0).count();
        log.info("[Init] UI card prewarm complete: time=" +
                 formatMs(cardUiMs) + "ms");
        if (callbacks.renderBootLoading) callbacks.renderBootLoading(0.985f);
        if (!pumpPreloadEventsOrQuit(callbacks)) {
            summary.interrupted = true;
            return summary;
        }
        summary.cardUiPrewarmed = true;
    }

    return summary;
}

} // namespace game::runtime::startup_asset_prewarm



