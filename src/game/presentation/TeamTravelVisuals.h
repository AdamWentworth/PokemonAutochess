#pragma once

#include <algorithm>
#include <vector>
#include <glm/glm.hpp>

namespace game::presentation {

// Render-only recall/send-out values. They never set capture flags or change ownership.
struct TravelUnitVisual {
    int id = -1;
    float scale = 1.0f;
    float tint = 0.0f;
    bool sendingOut = false;
    glm::vec3 unitOffset{};
    glm::vec3 ballPosition{};
    float ballScale = 0.0f;
    float ballClip = 0.0f;
    float ballPitchDeg = 0.0f;
    float light = 0.0f;
};

struct TeamTravelVisuals {
    bool active = false;
    std::vector<TravelUnitVisual> units;
    const TravelUnitVisual* find(int id) const {
        for (const auto& unit : units) if (unit.id == id) return &unit;
        return nullptr;
    }
};

inline float travelEase(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t*t*(3.0f - 2.0f*t);
}

} // namespace game::presentation
