#pragma once

#include <vector>

namespace game::runtime {

struct SharedBackendTextureCacheEntry {
    bool attemptedLoad = false;
    bool valid = false;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

} // namespace game::runtime
