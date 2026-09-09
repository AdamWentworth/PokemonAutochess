#include "game/runtime/shared/scene/AuthoredGroundSurface.h"
#include <cmath>
#include <limits>
#include <string>

bool test_authored_ground_surface_contract(std::string &outFail) {
    game::runtime::authored_environment::GroundSurface ground;
    // A lower floor and an overlapping sloped landing spanning index cells.
    ground.add({glm::vec3{-100, 0, -100}, {200, 0, -100}, {-100, 0, 200}});
    ground.add({glm::vec3{0, 50, 0}, {100, 50, 0}, {0, 100, 100}});
    // A wall cannot become a standing surface.
    ground.add({glm::vec3{0, 0, 0}, {0, 500, 0}, {100, 500, 0}});
    float height = -1.0f;
    if (!ground.sample(25, 25, height) || std::abs(height - 62.5f) > .001f ||
        !ground.sample(-25, -25, height) || height != 0.0f ||
        !ground.sample(0, 100, height) || height != 100.0f) {
        outFail = "Authored floor sampling lost the highest landing, ramp interpolation, or cell boundary.";
        return false;
    }
    height = 123.0f;
    if (ground.sample(200, 200, height) ||
        ground.sample(std::numeric_limits<float>::quiet_NaN(), 0, height) ||
        ground.sample(std::numeric_limits<float>::max(), 0, height) || height != 123.0f) {
        outFail = "Missing or invalid authored floor queries must preserve the caller's result.";
        return false;
    }
    return true;
}
