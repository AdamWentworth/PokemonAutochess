#pragma once

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "game/runtime/video/GpuAdapters.h"

namespace game::runtime::startup_diag {

struct ActiveRendererSummary {
    std::string requestedBackend;
    std::string activeBackend;
    std::string gpuVendor;
    std::string gpuRenderer;
    bool gpuDiscrete = false;
    bool vsyncEnabled = false;
    bool hasOpenGlStrings = false;
    std::string glVersion;
    std::string glslVersion;
};

std::vector<std::string> collectGpuAdapterNames(const std::vector<game::video::SystemGpuAdapter>& adapters);

bool containsPreferredGpuAdapter(const std::vector<game::video::SystemGpuAdapter>& adapters,
                                 std::string_view preferredAdapter);

bool activeRendererMatchesPreferredAdapter(std::string_view activeGpuRenderer,
                                          std::string_view preferredAdapter);

void logGpuAdapterInventory(const std::vector<game::video::SystemGpuAdapter>& adapters,
                            std::string_view preferredAdapter,
                            std::ostream& out);

void logActiveRendererSummary(const ActiveRendererSummary& summary,
                              std::ostream& out);

void logPreferredActiveAdapterMismatch(std::string_view preferredAdapter,
                                       std::string_view activeGpuRenderer,
                                       std::ostream& out);

} // namespace game::runtime::startup_diag

