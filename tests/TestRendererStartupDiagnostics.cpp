#include <sstream>
#include <string>
#include <vector>

#include "game/runtime/renderer/RendererStartupDiagnostics.h"

bool test_renderer_startup_diagnostics_contract(std::string& outFail) {
    using game::runtime::startup_diag::ActiveRendererSummary;
    using game::runtime::startup_diag::activeRendererMatchesPreferredAdapter;
    using game::runtime::startup_diag::collectGpuAdapterNames;
    using game::runtime::startup_diag::containsPreferredGpuAdapter;
    using game::runtime::startup_diag::logActiveRendererSummary;
    using game::runtime::startup_diag::logGpuAdapterInventory;
    using game::runtime::startup_diag::logPreferredActiveAdapterMismatch;
    using game::video::SystemGpuAdapter;

    const std::vector<SystemGpuAdapter> adapters = {
        {"Intel UHD", false},
        {"NVIDIA GeForce GTX 1050", true}
    };

    const auto names = collectGpuAdapterNames(adapters);
    if (names.size() != 2u || names[0] != "Intel UHD" || names[1] != "NVIDIA GeForce GTX 1050") {
        outFail = "collectGpuAdapterNames should preserve adapter names and order.";
        return false;
    }

    if (!containsPreferredGpuAdapter(adapters, "NVIDIA GeForce GTX 1050")) {
        outFail = "containsPreferredGpuAdapter should detect exact preferred adapter match.";
        return false;
    }
    if (containsPreferredGpuAdapter(adapters, "AMD Radeon")) {
        outFail = "containsPreferredGpuAdapter should reject missing preferred adapter.";
        return false;
    }

    if (!activeRendererMatchesPreferredAdapter("NVIDIA GeForce GTX 1050 Ti", "geforce gtx 1050")) {
        outFail = "activeRendererMatchesPreferredAdapter should be case-insensitive substring match.";
        return false;
    }
    if (activeRendererMatchesPreferredAdapter("Intel UHD", "NVIDIA")) {
        outFail = "activeRendererMatchesPreferredAdapter should reject unrelated adapters.";
        return false;
    }

    {
        std::ostringstream oss;
        logGpuAdapterInventory(adapters, "NVIDIA GeForce GTX 1050", oss);
        const std::string text = oss.str();
        if (text.find("[GPU] Adapters detected: 2") == std::string::npos ||
            text.find("Intel UHD (integrated)") == std::string::npos ||
            text.find("NVIDIA GeForce GTX 1050 (discrete)") == std::string::npos ||
            text.find("Preferred adapter setting: NVIDIA GeForce GTX 1050") == std::string::npos) {
            outFail = "logGpuAdapterInventory should list adapters and preferred adapter when present.";
            return false;
        }
    }

    {
        std::ostringstream oss;
        logGpuAdapterInventory({}, "NVIDIA GeForce GTX 1050", oss);
        const std::string text = oss.str();
        if (text.find("Adapter enumeration unavailable") == std::string::npos ||
            text.find("Preferred adapter setting not found on this machine") == std::string::npos) {
            outFail = "logGpuAdapterInventory should explain unavailable enumeration and missing preferred adapter.";
            return false;
        }
    }

    {
        ActiveRendererSummary summary;
        summary.requestedBackend = "d3d12";
        summary.activeBackend = "opengl";
        summary.gpuVendor = "intel";
        summary.gpuRenderer = "Intel UHD";
        summary.gpuDiscrete = false;
        summary.vsyncEnabled = true;
        summary.hasOpenGlStrings = true;
        summary.glVersion = "4.6";
        summary.glslVersion = "4.60";

        std::ostringstream oss;
        logActiveRendererSummary(summary, oss);
        const std::string text = oss.str();
        if (text.find("[Renderer] Requested: d3d12") == std::string::npos ||
            text.find("[Renderer] Active:    opengl") == std::string::npos ||
            text.find("[GPU] OpenGL:   4.6") == std::string::npos ||
            text.find("[GPU] GLSL:     4.60") == std::string::npos ||
            text.find("[Video] VSync:  On") == std::string::npos) {
            outFail = "logActiveRendererSummary should include backend, GL, and VSync details.";
            return false;
        }
    }

    {
        std::ostringstream oss;
        logPreferredActiveAdapterMismatch("NVIDIA", "Intel UHD", oss);
        if (oss.str().find("Preferred adapter 'NVIDIA' was not selected") == std::string::npos) {
            outFail = "logPreferredActiveAdapterMismatch should report adapter mismatch.";
            return false;
        }
    }

    {
        std::ostringstream oss;
        logPreferredActiveAdapterMismatch("NVIDIA", "NVIDIA GeForce GTX 1050", oss);
        if (!oss.str().empty()) {
            outFail = "logPreferredActiveAdapterMismatch should be quiet when the active adapter matches.";
            return false;
        }
    }

    return true;
}

