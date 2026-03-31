#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "engine/render/SpriteTextureCardArt.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"

namespace {

std::filesystem::path makeTempFile(const std::filesystem::path& path, std::size_t size) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    std::string chunk(4096, 'x');
    std::size_t written = 0u;
    while (written < size) {
        const std::size_t toWrite = std::min<std::size_t>(chunk.size(), size - written);
        out.write(chunk.data(), static_cast<std::streamsize>(toWrite));
        written += toWrite;
    }
    return path;
}

} // namespace

bool test_runtime_startup_asset_prewarm_contract(std::string& outFail) {
    using game::runtime::startup_asset_prewarm::Callbacks;
    using game::runtime::startup_asset_prewarm::GrowlStats;
    using game::runtime::startup_asset_prewarm::Options;
    using game::runtime::startup_asset_prewarm::ParticleVfxStats;
    using game::runtime::startup_asset_prewarm::TailFireStats;

    {
        const auto tempRoot =
            std::filesystem::temp_directory_path() / "pac_runtime_startup_asset_prewarm";
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
        const std::string bigPortrait =
            makeTempFile(tempRoot / "portraits" / "big_portrait.png", 1400000u).string();
        const std::string smallPortrait =
            makeTempFile(tempRoot / "portraits" / "small_portrait.png", 128u).string();

        std::vector<std::string> titles;
        std::vector<float> progressValues;
        std::vector<std::vector<std::string>> spritePrewarmCalls;
        std::vector<std::string> cardUiPaths;
        int requestQuitCalls = 0;
        int worldShadingCalls = 0;
        int tailFireCalls = 0;
        int growlCalls = 0;
        int particleVfxCalls = 0;
        std::ostringstream logs;

        const auto summary = game::runtime::startup_asset_prewarm::run(
            Options{
                .usesBackendRenderPath = true,
                .uiSpritePrewarmEnabled = true,
                .drawableW = 1920,
                .drawableH = 1080,
            },
            {
                "assets/ui/frame_gold.png",
                "assets/images/item_placeholder.png",
                bigPortrait,
                smallPortrait,
            },
            Callbacks{
                .setTitle = [&](const std::string& title) { titles.push_back(title); },
                .renderBootLoading = [&](float progress) { progressValues.push_back(progress); },
                .pumpPreloadEvents = []() { return true; },
                .requestQuit = [&]() { ++requestQuitCalls; },
                .prewarmWorldShading = [&]() { ++worldShadingCalls; },
                .prewarmTailFire =
                    [&]() {
                        ++tailFireCalls;
                        return TailFireStats{2u, 3u, 3u};
                    },
                .prewarmGrowlVfx =
                    [&]() {
                        ++growlCalls;
                        return GrowlStats{8u, 7u, 8u};
                    },
                .prewarmParticleVfx =
                    [&]() {
                        ++particleVfxCalls;
                        return ParticleVfxStats{8u, 8u};
                    },
                .prewarmSpriteTextures =
                    [&](const std::vector<std::string>& paths) { spritePrewarmCalls.push_back(paths); },
                .prewarmBackendCardUi =
                    [&](int, int, const std::vector<std::string>& paths) {
                        cardUiPaths = paths;
                    },
            },
            logs);

        if (summary.interrupted ||
            summary.tailFire.legacyAtlases != 2u ||
            summary.tailFire.meshFlipbookCpu != 3u ||
            summary.tailFire.meshFlipbookGpu != 3u ||
            summary.growl.drawPasses != 8u ||
            summary.growl.bakedTextures != 7u ||
            summary.growl.warmedBatches != 8u ||
            summary.particleVfx.textures != 8u ||
            summary.particleVfx.warmedBatches != 8u ||
            summary.uiSpritesRequested != 2u ||
            summary.cardArtRequested < 1u ||
            !summary.cardUiPrewarmed) {
            outFail = "run should preserve tail-fire, growl, particle-VFX, and UI/card prewarm summary fields.";
            return false;
        }

        if (requestQuitCalls != 0 || worldShadingCalls != 1 || tailFireCalls != 1 ||
            growlCalls != 1 || particleVfxCalls != 1) {
            outFail = "run should execute world-shading, tail-fire, growl, and particle-VFX prewarm once without requesting quit.";
            return false;
        }

        if (spritePrewarmCalls.size() != 2u ||
            spritePrewarmCalls[0].size() != 2u ||
            std::find(
                spritePrewarmCalls[1].begin(),
                spritePrewarmCalls[1].end(),
                engine::render::sprite_card_art::makeProxyPath(bigPortrait)) ==
                spritePrewarmCalls[1].end()) {
            outFail = "run should split generic UI sprite prewarm from eager backend card-art proxy prewarm.";
            return false;
        }

        if (cardUiPaths.size() != 4u) {
            outFail = "run should pass the full UI sprite path list into backend card-UI prewarm.";
            return false;
        }

        if (titles.size() < 5u ||
            titles.front() != "PokemonAutochess - Loading world shading..." ||
            titles[1] != "PokemonAutochess - Loading tail fire..." ||
            titles[2] != "PokemonAutochess - Loading growl VFX..." ||
            titles[3] != "PokemonAutochess - Loading particle VFX..." ||
            titles[4].find("Loading UI sprites") == std::string::npos) {
            outFail = "run should drive startup title updates for each asset prewarm stage.";
            return false;
        }

        if (progressValues.size() < 7u ||
            progressValues.front() != 0.92f ||
            progressValues[1] != 0.93f ||
            progressValues[2] != 0.935f ||
            progressValues[3] != 0.937f ||
            progressValues[4] != 0.94f ||
            progressValues.back() != 0.985f) {
            outFail = "run should emit boot-loading progress across world, tail-fire, growl, particle-VFX, and UI stages.";
            return false;
        }

        const std::string logText = logs.str();
        if (logText.find("Backend world shading prewarm complete") == std::string::npos ||
            logText.find("Backend tail fire prewarm complete: atlases=2 mesh_flipbook_cpu=3 mesh_flipbook_gpu=3") == std::string::npos ||
            logText.find("Backend growl VFX prewarm complete: passes=8 baked_textures=7 warmed_batches=8") == std::string::npos ||
            logText.find("Backend particle VFX prewarm complete: textures=8 warmed_batches=8") == std::string::npos ||
            logText.find("UI sprite prewarm complete: requested=2") == std::string::npos ||
            logText.find("UI card art prewarm complete: requested=") == std::string::npos ||
            logText.find("UI card prewarm complete") == std::string::npos) {
            outFail = "run should preserve the startup asset prewarm summary logs.";
            return false;
        }
    }

    {
        int requestQuitCalls = 0;
        int worldShadingCalls = 0;
        int tailFireCalls = 0;
        int growlCalls = 0;
        int particleVfxCalls = 0;
        int spritePrewarmCalls = 0;
        std::ostringstream logs;
        const auto summary = game::runtime::startup_asset_prewarm::run(
            Options{
                .usesBackendRenderPath = true,
                .uiSpritePrewarmEnabled = true,
                .drawableW = 1280,
                .drawableH = 720,
            },
            {"assets/ui/frame_gold.png"},
            Callbacks{
                .pumpPreloadEvents =
                    [calls = 0]() mutable {
                        ++calls;
                        return calls < 1;
                    },
                .requestQuit = [&]() { ++requestQuitCalls; },
                .prewarmWorldShading = [&]() { ++worldShadingCalls; },
                .prewarmTailFire =
                    [&]() {
                        ++tailFireCalls;
                        return TailFireStats{};
                    },
                .prewarmGrowlVfx =
                    [&]() {
                        ++growlCalls;
                        return GrowlStats{};
                    },
                .prewarmParticleVfx =
                    [&]() {
                        ++particleVfxCalls;
                        return ParticleVfxStats{};
                    },
                .prewarmSpriteTextures =
                    [&](const std::vector<std::string>&) { ++spritePrewarmCalls; },
            },
            logs);

        if (!summary.interrupted ||
            requestQuitCalls != 1 ||
            worldShadingCalls != 1 ||
            tailFireCalls != 0 ||
            growlCalls != 0 ||
            particleVfxCalls != 0 ||
            spritePrewarmCalls != 0) {
            outFail = "run should stop after the first stage when preload event pumping requests quit.";
            return false;
        }
    }

    return true;
}
