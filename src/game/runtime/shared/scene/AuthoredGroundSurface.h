#pragma once

#include <glm/vec3.hpp>
#include <array>
#include <map>
#include <utility>
#include <vector>

namespace game::runtime::authored_environment {

// A spatial index over authored floor triangles. The caller selects floor
// materials; decorative meshes and vertical walls never supply standing height.
class GroundSurface {
  public:
    using Triangle = std::array<glm::vec3, 3>;
    void add(const Triangle &triangle);
    bool sample(float sourceX, float sourceZ, float &outHeight) const noexcept;

  private:
    static constexpr float kIndexCellSize = 100.0f;
    std::map<std::pair<int, int>, std::vector<Triangle>> cells_;
};

} // namespace game::runtime::authored_environment
