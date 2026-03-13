#include "game/runtime/video/VideoPreferences.h"

#include <filesystem>
#include <string>

bool test_video_preferences_parse_and_roundtrip(std::string& outFail) {
    using game::video::RendererBackend;

    if (game::video::parseRendererBackend("opengl") != RendererBackend::OpenGL) {
        outFail = "parseRendererBackend(opengl) failed";
        return false;
    }
    if (game::video::parseRendererBackend("opengl_shared") != RendererBackend::OpenGL) {
        outFail = "parseRendererBackend(opengl_shared alias) failed";
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
    if (!game::video::isKnownRendererBackendToken("opengl_shared")) {
        outFail = "known renderer token check failed for opengl_shared";
        return false;
    }
#if defined(_WIN32)
    if (!game::video::isRendererBackendImplemented(RendererBackend::D3D12)) {
        outFail = "D3D12 backend should be marked implemented on Windows";
        return false;
    }
#else
    if (game::video::isRendererBackendImplemented(RendererBackend::D3D12)) {
        outFail = "D3D12 backend should be marked unimplemented on non-Windows";
        return false;
    }
#endif

    const std::filesystem::path tempPath =
        std::filesystem::temp_directory_path() / "pac_video_settings_test.json";
    const std::string path = tempPath.string();

    game::video::Preferences prefs;
    prefs.rendererBackend = "vulkan";
    prefs.requireDiscreteGpu = true;
    prefs.preferredGpuAdapter = "NVIDIA GeForce GTX 1050";
    prefs.restartOnExit = true;
    prefs.bootMenuScreen = "video";

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
    if (!loaded.restartOnExit) {
        outFail = "roundtrip restartOnExit mismatch";
        return false;
    }
    if (loaded.bootMenuScreen != "video") {
        outFail = "roundtrip bootMenuScreen mismatch";
        return false;
    }

    prefs.rendererBackend = "opengl_shared"; // backward-compat alias should canonicalize to opengl
    prefs.requireDiscreteGpu = false;
    prefs.preferredGpuAdapter.clear();
    prefs.restartOnExit = false;
    prefs.bootMenuScreen = {};
    if (!game::video::savePreferences(prefs, path, &err)) {
        outFail = "savePreferences failed for opengl_shared alias: " + err;
        return false;
    }
    const game::video::Preferences loadedShared = game::video::loadPreferences(path);
    std::filesystem::remove(tempPath, ec);
    if (loadedShared.rendererBackend != "opengl") {
        outFail = "roundtrip renderer backend mismatch for opengl_shared alias";
        return false;
    }

    return true;
}

