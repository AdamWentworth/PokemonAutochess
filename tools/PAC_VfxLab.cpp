#define SDL_MAIN_HANDLED

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "engine/core/Paths.h"
#include "engine/tools/vfx_preview/VfxPreviewApp.h"
#include "vfx/preview/VfxLibraryPreviewProject.h"

#if defined(_WIN32)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

void setRepoWorkingDirectory() {
    std::error_code ec;
    const std::filesystem::path repoRoot = engine::paths::dataRoot();
    if (repoRoot.empty()) return;
    std::filesystem::current_path(repoRoot, ec);
    if (ec) {
        std::cerr << "[VfxLab] Warning: failed to set working directory to "
                  << repoRoot.string() << ": " << ec.message() << "\n";
    }
}

} // namespace

int main() {
    try {
        setRepoWorkingDirectory();
        engine::tools::vfx_preview::VfxPreviewApp app(
            std::make_unique<vfx::preview::VfxLibraryPreviewProject>());
        return app.run();
    } catch (const std::exception& ex) {
        std::cerr << "[VfxLab] Fatal: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[VfxLab] Fatal: unknown exception\n";
        return 1;
    }
}
