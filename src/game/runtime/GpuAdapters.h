#pragma once

#include <string>
#include <vector>

namespace game::video {

struct SystemGpuAdapter {
    std::string name;
    bool discrete = false;
};

std::vector<SystemGpuAdapter> enumerateSystemGpuAdapters();

} // namespace game::video
