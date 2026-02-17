#include "game/runtime/VideoPreferences.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "engine/core/Paths.h"

namespace game::video {
namespace {

std::string normalizeToken(std::string token) {
    token.erase(std::remove_if(token.begin(), token.end(),
                               [](unsigned char c) { return std::isspace(c) != 0; }),
                token.end());
    for (char& c : token) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '-') c = '_';
    }
    return token;
}

std::string normalizeMenuScreenToken(std::string token) {
    token = normalizeToken(std::move(token));
    if (token == "display") {
        return "video";
    }
    if (token == "video" ||
        token == "settings" ||
        token == "audio" ||
        token == "controls" ||
        token == "gameplay" ||
        token == "accessibility" ||
        token == "main") {
        return token;
    }
    return {};
}

} // namespace

std::string defaultPreferencesPath() {
    return engine::paths::data("config/user/video_settings.json");
}

RendererBackend parseRendererBackend(std::string_view tokenView) {
    const std::string token = normalizeToken(std::string(tokenView));
    if (token.empty() || token == "auto") return RendererBackend::Auto;
    if (token == "opengl" || token == "gl") return RendererBackend::OpenGL;
    if (token == "vulkan" || token == "vk") return RendererBackend::Vulkan;
    if (token == "d3d12" || token == "dx12" || token == "direct3d12") return RendererBackend::D3D12;
    return RendererBackend::Auto;
}

bool isKnownRendererBackendToken(std::string_view tokenView) {
    const std::string token = normalizeToken(std::string(tokenView));
    if (token.empty()) return true;
    return token == "auto" || token == "opengl" || token == "gl" ||
           token == "vulkan" || token == "vk" ||
           token == "d3d12" || token == "dx12" || token == "direct3d12";
}

const char* rendererBackendName(RendererBackend backend) {
    switch (backend) {
    case RendererBackend::Auto:
        return "auto";
    case RendererBackend::OpenGL:
        return "opengl";
    case RendererBackend::Vulkan:
        return "vulkan";
    case RendererBackend::D3D12:
        return "d3d12";
    default:
        return "auto";
    }
}

bool isRendererBackendImplemented(RendererBackend backend) {
    if (backend == RendererBackend::Auto || backend == RendererBackend::OpenGL) {
        return true;
    }
#if defined(_WIN32)
    if (backend == RendererBackend::D3D12) {
        return true;
    }
#endif
    return false;
}

Preferences loadPreferences(const std::string& path) {
    Preferences out;

    std::ifstream in(path);
    if (!in.is_open()) return out;

    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return out;
    }

    if (!j.is_object()) return out;

    const std::string backend = j.value("renderer_backend", out.rendererBackend);
    if (isKnownRendererBackendToken(backend)) {
        out.rendererBackend = rendererBackendName(parseRendererBackend(backend));
    }
    out.requireDiscreteGpu = j.value("require_discrete_gpu", out.requireDiscreteGpu);
    out.preferredGpuAdapter = j.value("preferred_gpu_adapter", out.preferredGpuAdapter);
    out.restartOnExit = j.value("restart_on_exit", out.restartOnExit);

    const std::string menuScreen = j.value("boot_menu_screen", std::string());
    out.bootMenuScreen = normalizeMenuScreenToken(menuScreen);
    return out;
}

bool savePreferences(const Preferences& prefs, const std::string& path, std::string* outError) {
    nlohmann::json j = nlohmann::json::object();
    j["renderer_backend"] = rendererBackendName(parseRendererBackend(prefs.rendererBackend));
    j["require_discrete_gpu"] = prefs.requireDiscreteGpu;
    j["preferred_gpu_adapter"] = prefs.preferredGpuAdapter;
    j["restart_on_exit"] = prefs.restartOnExit;
    j["boot_menu_screen"] = normalizeMenuScreenToken(prefs.bootMenuScreen);

    std::error_code ec;
    const std::filesystem::path outPath(path);
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec) {
        if (outError) *outError = "create_directories failed: " + ec.message();
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (outError) *outError = "failed to open file for write";
        return false;
    }

    try {
        out << j.dump(2) << "\n";
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

} // namespace game::video
