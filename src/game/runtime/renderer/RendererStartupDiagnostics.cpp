#include "game/runtime/renderer/RendererStartupDiagnostics.h"

#include <algorithm>
#include <cctype>
#include <ostream>

namespace {

std::string toLowerCopy(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool containsCi(std::string_view haystack, std::string_view needle) {
    return toLowerCopy(std::string(haystack)).find(toLowerCopy(std::string(needle))) != std::string::npos;
}

} // namespace

namespace game::runtime::startup_diag {

std::vector<std::string> collectGpuAdapterNames(const std::vector<game::video::SystemGpuAdapter>& adapters) {
    std::vector<std::string> out;
    out.reserve(adapters.size());
    for (const auto& adapter : adapters) {
        out.push_back(adapter.name);
    }
    return out;
}

bool containsPreferredGpuAdapter(const std::vector<game::video::SystemGpuAdapter>& adapters,
                                 std::string_view preferredAdapter) {
    if (preferredAdapter.empty()) return false;
    return std::any_of(
        adapters.begin(),
        adapters.end(),
        [&](const game::video::SystemGpuAdapter& adapter) {
            return adapter.name == preferredAdapter;
        });
}

bool activeRendererMatchesPreferredAdapter(std::string_view activeGpuRenderer,
                                          std::string_view preferredAdapter) {
    if (preferredAdapter.empty()) return true;
    if (activeGpuRenderer.empty()) return false;
    return containsCi(activeGpuRenderer, preferredAdapter);
}

void logGpuAdapterInventory(const std::vector<game::video::SystemGpuAdapter>& adapters,
                            std::string_view preferredAdapter,
                            std::ostream& out) {
    if (!adapters.empty()) {
        out << "[GPU] Adapters detected: " << adapters.size() << "\n";
        for (std::size_t i = 0; i < adapters.size(); ++i) {
            out << "  [" << i << "] " << adapters[i].name
                << " (" << (adapters[i].discrete ? "discrete" : "integrated") << ")\n";
        }
    } else {
        out << "[GPU] Adapter enumeration unavailable for this platform/runtime.\n";
    }

    if (preferredAdapter.empty()) return;
    if (containsPreferredGpuAdapter(adapters, preferredAdapter)) {
        out << "[GPU] Preferred adapter setting: " << preferredAdapter << "\n";
    } else {
        out << "[GPU] Preferred adapter setting not found on this machine: "
            << preferredAdapter << "\n";
    }
}

void logActiveRendererSummary(const ActiveRendererSummary& summary,
                              std::ostream& out) {
    out << "[Renderer] Requested: " << summary.requestedBackend << "\n";
    out << "[Renderer] Active:    " << summary.activeBackend << "\n";
    out << "[GPU] Vendor:   " << summary.gpuVendor << "\n";
    out << "[GPU] Renderer: " << summary.gpuRenderer << "\n";
    if (summary.hasOpenGlStrings) {
        out << "[GPU] OpenGL:   " << summary.glVersion << "\n";
        out << "[GPU] GLSL:     " << summary.glslVersion << "\n";
    }
    out << "[GPU] Class:    " << (summary.gpuDiscrete ? "discrete" : "integrated") << "\n";
    out << "[Video] VSync:  " << (summary.vsyncEnabled ? "On" : "Off") << "\n";
}

void logPreferredActiveAdapterMismatch(std::string_view preferredAdapter,
                                       std::string_view activeGpuRenderer,
                                       std::ostream& out) {
    if (preferredAdapter.empty()) return;
    if (activeRendererMatchesPreferredAdapter(activeGpuRenderer, preferredAdapter)) return;
    out << "[GPU] Preferred adapter '" << preferredAdapter
        << "' was not selected by active backend.\n";
}

} // namespace game::runtime::startup_diag

