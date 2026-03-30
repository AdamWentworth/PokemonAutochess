#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>

namespace game::runtime::render_model {
struct MeshData;
}

namespace game::runtime::shared_tail_fire_mesh_playback {

struct FlipbookSpec {
    const char* path = nullptr;
    float cols = 1.0f;
    float rows = 1.0f;
    float frames = 1.0f;
    float fps = 0.0f;
    float atlasWidth = 0.0f;
    float atlasHeight = 0.0f;
};

struct Profile {
    bool hasFireSubmesh = false;
    FlipbookSpec spec{};
    glm::vec2 uvShift{0.0f, 0.0f};
    std::vector<std::uint8_t> fireSubmeshMask;
};

bool isTailFireMeshPlaybackSpecies(std::string_view species);
std::string_view authoredFireMeshNodeToken();
const std::array<FlipbookSpec, 3>& authoredFlipbookSpecs();
const Profile& resolveProfile(const render_model::MeshData& mesh);

} // namespace game::runtime::shared_tail_fire_mesh_playback
