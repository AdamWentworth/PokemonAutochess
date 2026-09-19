#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "engine/utils/LogSink.h"
#include "engine/render/SpriteTextureCardArt.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/ui/PokemonArtwork.h"

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
    using game::runtime::startup_asset_prewarm::AuthoredVfxKind;
    using game::runtime::startup_asset_prewarm::AuthoredVfxStats;
    using game::runtime::startup_asset_prewarm::GrowlStats;
    using game::runtime::startup_asset_prewarm::Options;
    using game::runtime::startup_asset_prewarm::ParticleVfxStats;
    using game::runtime::startup_asset_prewarm::ScratchStats;
    using game::runtime::startup_asset_prewarm::TackleStats;

    {
        const auto tempRoot =
            std::filesystem::temp_directory_path() / "pac_runtime_startup_asset_prewarm";
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
        const std::string bigPortrait =
            makeTempFile(tempRoot / "portraits" / "big_portrait.png", 1400000u).string();
        const std::string smallPortrait =
            makeTempFile(tempRoot / "portraits" / "small_portrait.png", 128u).string();

        const auto configPath = tempRoot / "pokemon.json";
        {
            std::ofstream config(configPath);
            config << R"({"bulbasaur":{"model":"0001_Bulbasaur.phmodel"},"nidoran-f":{"model":"0029_NidoranF.phmodel"}})";
        }
        GameDataDb data;
        if (!data.pokemon.loadConfig(configPath.string())) {
            outFail = "Could not load portrait prewarm fixture.";
            return false;
        }
        const auto configuredPaths = game::runtime::startup_asset_prewarm::collectUiSpritePrewarmPaths(data);
        for (const auto *path : {"assets/ui/pokemon/tcg/001.jpg", "assets/ui/pokemon/tcg/029.jpg"}) {
            if (std::count(configuredPaths.begin(), configuredPaths.end(), path) != 1) {
                outFail = "Card and portrait prewarm must share one original scan per configured species.";
                return false;
            }
        }
        if (std::find(configuredPaths.begin(), configuredPaths.end(), "assets/ui/pokemon/tcg/151.jpg") != configuredPaths.end()) {
            outFail = "Unconfigured species portraits should not all allocate textures on startup.";
            return false;
        }

        std::vector<std::string> titles;
        std::vector<float> progressValues;
        std::vector<std::vector<std::string>> spritePrewarmCalls;
        std::vector<std::string> cardUiPaths;
        int requestQuitCalls = 0;
        int worldShadingCalls = 0;
        int growlCalls = 0;
        int tackleCalls = 0;
        int scratchCalls = 0;
        int particleVfxCalls = 0;
        std::ostringstream logs;
        engine::log::Sink log("TestStartupAssetPrewarm", &logs, &logs);

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
                "assets/ui/pokemon/tcg/001.jpg",
                bigPortrait,
                smallPortrait,
            },
            Callbacks{
                .setTitle = [&](const std::string &title) { titles.push_back(title); },
                .renderBootLoading = [&](float progress) { progressValues.push_back(progress); },
                .pumpPreloadEvents = []() { return true; },
                .requestQuit = [&]() { ++requestQuitCalls; },
                .prewarmWorldShading = [&]() { ++worldShadingCalls; },
                .prewarmAuthoredVfx =
                    {
                        game::runtime::startup_asset_prewarm::makeAuthoredVfxPrewarmEntry(
                            AuthoredVfxKind::Growl,
                            [&]() {
                                ++growlCalls;
                                return AuthoredVfxStats{8u, 7u, 8u};
                            }),
                        game::runtime::startup_asset_prewarm::makeAuthoredVfxPrewarmEntry(
                            AuthoredVfxKind::Tackle,
                            [&]() {
                                ++tackleCalls;
                                return AuthoredVfxStats{17u, 12u, 29u};
                            }),
                        game::runtime::startup_asset_prewarm::makeAuthoredVfxPrewarmEntry(
                            AuthoredVfxKind::Scratch,
                            [&]() {
                                ++scratchCalls;
                                return AuthoredVfxStats{23u, 14u, 41u};
                            }),
                    },
                .prewarmParticleVfx =
                    [&]() {
                        ++particleVfxCalls;
                        return ParticleVfxStats{8u, 8u};
                    },
                .prewarmSpriteTextures =
                    [&](const std::vector<std::string> &paths) { spritePrewarmCalls.push_back(paths); },
                .prewarmBackendCardUi =
                    [&](int, int, const std::vector<std::string> &paths) {
                        cardUiPaths = paths;
                    },
            },
            log);

        if (summary.interrupted ||
            summary.growl.drawPasses != 8u ||
            summary.growl.bakedTextures != 7u ||
            summary.growl.warmedBatches != 8u ||
            summary.tackle.drawPasses != 17u ||
            summary.tackle.bakedTextures != 12u ||
            summary.tackle.warmedBatches != 29u ||
            summary.scratch.drawPasses != 23u ||
            summary.scratch.bakedTextures != 14u ||
            summary.scratch.warmedBatches != 41u ||
            summary.particleVfx.textures != 8u ||
            summary.particleVfx.warmedBatches != 8u ||
            summary.uiSpritesRequested != 3u ||
            summary.cardArtRequested < 1u ||
            !summary.cardUiPrewarmed) {
            outFail = "run should preserve growl, tackle, scratch, particle-VFX, and UI/card prewarm summary fields.";
            return false;
        }

        if (requestQuitCalls != 0 || worldShadingCalls != 1 ||
            growlCalls != 1 || tackleCalls != 1 || scratchCalls != 1 || particleVfxCalls != 1) {
            outFail = "run should execute world-shading, growl, tackle, scratch, and particle-VFX prewarm once without requesting quit.";
            return false;
        }

        if (spritePrewarmCalls.size() != 2u ||
            spritePrewarmCalls[0].size() != 3u ||
            std::find(spritePrewarmCalls[0].begin(), spritePrewarmCalls[0].end(), "assets/ui/pokemon/tcg/001.jpg") == spritePrewarmCalls[0].end() ||
            std::find(
                spritePrewarmCalls[1].begin(),
                spritePrewarmCalls[1].end(),
                engine::render::sprite_card_art::makeProxyPath(bigPortrait)) ==
                spritePrewarmCalls[1].end()) {
            outFail = "run should split generic UI sprite prewarm from eager backend card-art proxy prewarm.";
            return false;
        }

        if (cardUiPaths.size() != 5u) {
            outFail = "run should pass the full UI sprite path list into backend card-UI prewarm.";
            return false;
        }

        if (titles.size() < 6u ||
            titles.front() != "PokemonAutochess - Loading world shading..." ||
            titles[1] != game::runtime::startup_asset_prewarm::authoredVfxPrewarmTitle(AuthoredVfxKind::Growl) ||
            titles[2] != game::runtime::startup_asset_prewarm::authoredVfxPrewarmTitle(AuthoredVfxKind::Tackle) ||
            titles[3] != game::runtime::startup_asset_prewarm::authoredVfxPrewarmTitle(AuthoredVfxKind::Scratch) ||
            titles[4] != "PokemonAutochess - Loading particle VFX..." ||
            titles[5].find("Loading UI sprites") == std::string::npos) {
            outFail = "run should drive startup title updates for each asset prewarm stage.";
            return false;
        }

        if (progressValues.size() < 8u ||
            progressValues.front() != 0.92f ||
            progressValues[1] != game::runtime::startup_asset_prewarm::authoredVfxPrewarmProgress(AuthoredVfxKind::Growl) ||
            progressValues[2] != game::runtime::startup_asset_prewarm::authoredVfxPrewarmProgress(AuthoredVfxKind::Tackle) ||
            progressValues[3] != game::runtime::startup_asset_prewarm::authoredVfxPrewarmProgress(AuthoredVfxKind::Scratch) ||
            progressValues[4] != 0.937f ||
            progressValues[5] != 0.94f ||
            progressValues.back() != 0.985f) {
            outFail = "run should emit boot-loading progress across world, growl, tackle, scratch, particle-VFX, and UI stages.";
            return false;
        }

        const std::string logText = logs.str();
        if (logText.find("Backend world shading prewarm complete") == std::string::npos ||
            logText.find("Backend growl VFX prewarm complete: passes=8 baked_textures=7 warmed_batches=8") == std::string::npos ||
            logText.find("Backend tackle VFX prewarm complete: passes=17 baked_textures=12 warmed_batches=29") == std::string::npos ||
            logText.find("Backend scratch VFX prewarm complete: passes=23 baked_textures=14 warmed_batches=41") == std::string::npos ||
            logText.find("Backend particle VFX prewarm complete: textures=8 warmed_batches=8") == std::string::npos ||
            logText.find("UI sprite prewarm complete: requested=3") == std::string::npos ||
            logText.find("UI card art prewarm complete: requested=") == std::string::npos ||
            logText.find("UI card prewarm complete") == std::string::npos) {
            outFail = "run should preserve the startup asset prewarm summary logs.";
            return false;
        }

        spritePrewarmCalls.clear();
        const auto embedded = game::runtime::startup_asset_prewarm::run(
            Options{.usesBackendRenderPath = true, .uiSpritePrewarmEnabled = true},
            {"assets/ui/frame_gold.png", bigPortrait},
            Callbacks{.prewarmSpriteTextures = [&](const auto &paths) { spritePrewarmCalls.push_back(paths); }},
            log);
        if (embedded.cardUiPrewarmed || embedded.cardArtRequested == 0 || spritePrewarmCalls.size() != 2 ||
            std::find(spritePrewarmCalls.back().begin(), spritePrewarmCalls.back().end(),
                      engine::render::sprite_card_art::makeProxyPath(bigPortrait)) == spritePrewarmCalls.back().end()) {
            outFail = "Embedded preview must warm card-art textures without drawing a startup frame to the host backbuffer.";
            return false;
        }
    }

    {
        int requestQuitCalls = 0;
        int worldShadingCalls = 0;
        int growlCalls = 0;
        int tackleCalls = 0;
        int scratchCalls = 0;
        int particleVfxCalls = 0;
        int spritePrewarmCalls = 0;
        std::ostringstream logs;
        engine::log::Sink log("TestStartupAssetPrewarm", &logs, &logs);
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
                .prewarmAuthoredVfx =
                    {
                        game::runtime::startup_asset_prewarm::makeAuthoredVfxPrewarmEntry(
                            AuthoredVfxKind::Growl,
                            [&]() {
                                ++growlCalls;
                                return AuthoredVfxStats{};
                            }),
                        game::runtime::startup_asset_prewarm::makeAuthoredVfxPrewarmEntry(
                            AuthoredVfxKind::Tackle,
                            [&]() {
                                ++tackleCalls;
                                return AuthoredVfxStats{};
                            }),
                        game::runtime::startup_asset_prewarm::makeAuthoredVfxPrewarmEntry(
                            AuthoredVfxKind::Scratch,
                            [&]() {
                                ++scratchCalls;
                                return AuthoredVfxStats{};
                            }),
                    },
                .prewarmParticleVfx =
                    [&]() {
                        ++particleVfxCalls;
                        return ParticleVfxStats{};
                    },
                .prewarmSpriteTextures =
                    [&](const std::vector<std::string>&) { ++spritePrewarmCalls; },
            },
            log);

        if (!summary.interrupted ||
            requestQuitCalls != 1 ||
            worldShadingCalls != 1 ||
            growlCalls != 0 ||
            tackleCalls != 0 ||
            scratchCalls != 0 ||
            particleVfxCalls != 0 ||
            spritePrewarmCalls != 0) {
            outFail = "run should stop after the first stage when preload event pumping requests quit.";
            return false;
        }
    }

    return true;
}
