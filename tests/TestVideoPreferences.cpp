#include "game/runtime/VideoPreferences.h"

#include <filesystem>
#include <string>

bool test_video_preferences_parse_and_roundtrip(std::string& outFail) {
    using game::video::RendererBackend;

    if (game::video::parseRendererBackend("opengl") != RendererBackend::OpenGL) {
        outFail = "parseRendererBackend(opengl) failed";
        return false;
    }
    if (game::video::parseRendererBackend("vk") != RendererBackend::Vulkan) {
        outFail = "parseRendererBackend(vk) failed";
        return false;
    }
    if (game::video::parseRendererBackend("dx12") != RendererBackend::D3D12) {
        outFail = "parseRendererBackend(dx12) failed";
        return false;
    }
    if (!game::video::isKnownRendererBackendToken("direct3d12")) {
        outFail = "known renderer token check failed";
        return false;
    }

    const std::filesystem::path tempPath =
        std::filesystem::temp_directory_path() / "pac_video_settings_test.json";
    const std::string path = tempPath.string();

    game::video::Preferences prefs;
    prefs.rendererBackend = "vulkan";
    prefs.requireDiscreteGpu = true;
    prefs.preferredGpuAdapter = "NVIDIA GeForce GTX 1050";

    std::string err;
    if (!game::video::savePreferences(prefs, path, &err)) {
        outFail = "savePreferences failed: " + err;
        return false;
    }

    const game::video::Preferences loaded = game::video::loadPreferences(path);
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);

    if (loaded.rendererBackend != "vulkan") {
        outFail = "roundtrip renderer backend mismatch";
        return false;
    }
    if (!loaded.requireDiscreteGpu) {
        outFail = "roundtrip requireDiscreteGpu mismatch";
        return false;
    }
    if (loaded.preferredGpuAdapter != "NVIDIA GeForce GTX 1050") {
        outFail = "roundtrip preferredGpuAdapter mismatch";
        return false;
    }

    return true;
}
