#include "game/runtime/video/VideoPreferences.h"

#include <filesystem>
#include <fstream>
#include <string>

bool test_video_preferences_parse_and_roundtrip(std::string& outFail) {
    using game::video::RendererBackend;
    using game::video::GraphicsQuality;

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
    if (!game::video::isRendererBackendImplemented(RendererBackend::Vulkan)) {
        outFail = "Vulkan backend should be marked implemented.";
        return false;
    }
    if (game::video::parseGraphicsQuality("high") != GraphicsQuality::High) {
        outFail = "parseGraphicsQuality(high) failed";
        return false;
    }
    if (!game::video::isKnownGraphicsQualityToken("maximum")) {
        outFail = "known graphics quality token check failed for maximum alias";
        return false;
    }
    if (game::video::sanitizeGraphicsQuality(99) != static_cast<int>(GraphicsQuality::Ultra)) {
        outFail = "sanitizeGraphicsQuality should clamp high values to Ultra";
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
    prefs.vsync = true;
    prefs.fpsCap = 144;
    prefs.graphicsQuality = static_cast<int>(GraphicsQuality::Medium);
    prefs.requireDiscreteGpu = true;
    prefs.preferredGpuAdapter = "NVIDIA GeForce GTX 1050";
    prefs.characterInking = true;
    prefs.audioMasterVolume = 95;
    prefs.audioMusicVolume = 65;
    prefs.audioSfxVolume = 70;
    prefs.audioVoiceVolume = 55;
    prefs.audioMute = true;
    prefs.restartOnExit = true;
    prefs.fullscreen = true;
    prefs.windowedWidth = 1720;
    prefs.windowedHeight = 980;
    prefs.windowedMaximized = true;
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
    if (loaded.fpsCap != 144) {
        outFail = "roundtrip fpsCap mismatch";
        return false;
    }
    if (!loaded.vsync) {
        outFail = "roundtrip vsync mismatch";
        return false;
    }
    if (loaded.graphicsQuality != static_cast<int>(GraphicsQuality::Medium)) {
        outFail = "roundtrip graphicsQuality mismatch";
        return false;
    }
    if (!loaded.characterInking) {
        outFail = "roundtrip characterInking mismatch";
        return false;
    }
    if (loaded.audioMasterVolume != 95 ||
        loaded.audioMusicVolume != 65 ||
        loaded.audioSfxVolume != 70 ||
        loaded.audioVoiceVolume != 55 ||
        !loaded.audioMute) {
        outFail = "roundtrip audio preference mismatch";
        return false;
    }
    if (!loaded.restartOnExit) {
        outFail = "roundtrip restartOnExit mismatch";
        return false;
    }
    if (!loaded.fullscreen ||
        loaded.windowedWidth != 1720 ||
        loaded.windowedHeight != 980 ||
        !loaded.windowedMaximized) {
        outFail = "roundtrip window video preference mismatch";
        return false;
    }
    if (loaded.bootMenuScreen != "video") {
        outFail = "roundtrip bootMenuScreen mismatch";
        return false;
    }

    prefs.rendererBackend = "opengl_shared"; // backward-compat alias should canonicalize to opengl
    prefs.fpsCap = -12;
    prefs.graphicsQuality = -7;
    prefs.requireDiscreteGpu = false;
    prefs.preferredGpuAdapter.clear();
    prefs.audioMasterVolume = 101;
    prefs.audioMusicVolume = -7;
    prefs.audioSfxVolume = 44;
    prefs.audioVoiceVolume = 150;
    prefs.audioMute = false;
    prefs.fullscreen = false;
    prefs.windowedWidth = 0;
    prefs.windowedHeight = 0;
    prefs.windowedMaximized = false;
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
    if (loadedShared.fpsCap != 0) {
        outFail = "fpsCap should sanitize negative values to uncapped";
        return false;
    }
    if (loadedShared.graphicsQuality != static_cast<int>(GraphicsQuality::Low)) {
        outFail = "graphicsQuality should sanitize negative values to Low";
        return false;
    }
    if (loadedShared.audioMasterVolume != 100 ||
        loadedShared.audioMusicVolume != 0 ||
        loadedShared.audioSfxVolume != 44 ||
        loadedShared.audioVoiceVolume != 100 ||
        loadedShared.audioMute) {
        outFail = "audio volume sanitization mismatch";
        return false;
    }

    prefs.bootMenuScreen = "advanced";
    if (!game::video::savePreferences(prefs, path, &err)) {
        outFail = "savePreferences failed for advanced boot screen: " + err;
        return false;
    }
    const game::video::Preferences loadedAdvanced = game::video::loadPreferences(path);
    std::filesystem::remove(tempPath, ec);
    if (loadedAdvanced.bootMenuScreen != "advanced") {
        outFail = "roundtrip bootMenuScreen mismatch for advanced screen";
        return false;
    }

    {
        std::ofstream raw(path, std::ios::trunc);
        raw << "{\n"
               "  \"graphics_quality\": \"high\"\n"
               "}\n";
        raw.close();
        const game::video::Preferences loadedStringQuality =
            game::video::loadPreferences(path);
        std::filesystem::remove(tempPath, ec);
        if (loadedStringQuality.graphicsQuality != static_cast<int>(GraphicsQuality::High)) {
            outFail = "graphics_quality string token should parse to High";
            return false;
        }
    }

    return true;
}

