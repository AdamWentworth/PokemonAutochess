#define SDL_MAIN_HANDLED

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "engine/core/Paths.h"
#include "engine/tools/vfx_preview/VfxPreviewApp.h"
#include "game/preview/PokemonAutochessVfxPreviewProject.h"

#if defined(_WIN32)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

void appendBootLog(const std::string& line) {
    std::ofstream out("debug_vfx_previewer_boot.log", std::ios::app);
    if (!out.is_open()) return;
    out << line << "\n";
    out.flush();
}

void setRepoWorkingDirectory() {
    std::error_code ec;
    const std::filesystem::path repoRoot = engine::paths::dataRoot();
    if (repoRoot.empty()) return;
    std::filesystem::current_path(repoRoot, ec);
    if (ec) {
        std::cerr << "[VfxPreviewer] Warning: failed to set working directory to "
                  << repoRoot.string() << ": " << ec.message() << "\n";
    }
}

} // namespace

int main() {
    try {
        appendBootLog("[boot] previewer start");
        setRepoWorkingDirectory();
        appendBootLog(std::string("[boot] cwd=") + std::filesystem::current_path().string());
        appendBootLog("[boot] constructing preview project");
        auto project = std::make_unique<game::preview::PokemonAutochessVfxPreviewProject>();
        appendBootLog("[boot] constructing app");
        engine::tools::vfx_preview::VfxPreviewApp app(
            std::move(project));
        appendBootLog("[boot] entering app.run()");
        const int rc = app.run();
        appendBootLog(std::string("[boot] app.run() returned ") + std::to_string(rc));
        return rc;
    } catch (const std::exception& ex) {
        appendBootLog(std::string("[boot] fatal exception: ") + ex.what());
        std::cerr << "[VfxPreviewer] Fatal: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        appendBootLog("[boot] fatal unknown exception");
        std::cerr << "[VfxPreviewer] Fatal: unknown exception\n";
        return 1;
    }
}
