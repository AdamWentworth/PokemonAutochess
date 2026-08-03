#include <cmath>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool approx(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_shared_tail_fire_mesh_playback_contract(std::string& outFail) {
    using namespace game::runtime::shared_tail_fire_mesh_playback;

    const auto& specs = authoredFlipbookSpecs();
    if (!expect(specs.size() == 2u,
                "authoredFlipbookSpecs should expose the remaining legacy authored flipbooks.",
                outFail)) {
        return false;
    }
    if (!expect(std::string_view(specs[0].path) == "assets/textures/CharmeleonFireUVFlipbook.png" &&
                    std::string_view(specs[1].path) == "assets/textures/CharizardFireUVFlipbook.png",
                "authoredFlipbookSpecs should expose the expected authored fire-mesh texture paths.",
                outFail)) {
        return false;
    }
    if (!expect(!isTailFireMeshPlaybackSpecies("Charmander") &&
                    isTailFireMeshPlaybackSpecies("charmeleon") &&
                    isTailFireMeshPlaybackSpecies("CHARIZARD") &&
                    !isTailFireMeshPlaybackSpecies("squirtle"),
                "isTailFireMeshPlaybackSpecies should match the current starter-line playback policy case-insensitively.",
                outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData charizardMesh;
    charizardMesh.nodeNames = {"root", "PM0006_Charizard", "tail_fire_mesh"};
    charizardMesh.nodeMesh = {-1, 0, 1};
    charizardMesh.meshIndexToNode = {1, 2};
    charizardMesh.submeshMeshIndex = {0, 1, 1};
    charizardMesh.vertices.resize(2);
    charizardMesh.vertices[0].uv = glm::vec2(-1.25f, -2.75f);
    charizardMesh.vertices[1].uv = glm::vec2(0.25f, -1.0f);

    const auto& charizardProfile = resolveProfile(charizardMesh);
    if (!expect(charizardProfile.hasFireSubmesh,
                "resolveProfile should detect authored fire submeshes when a fire_mesh node is present.",
                outFail)) {
        return false;
    }
    if (!expect(charizardProfile.fireSubmeshMask.size() == 3u &&
                    charizardProfile.fireSubmeshMask[0] == 0u &&
                    charizardProfile.fireSubmeshMask[1] == 1u &&
                    charizardProfile.fireSubmeshMask[2] == 1u,
                "resolveProfile should mark only the fire-mesh-backed submeshes for authored playback.",
                outFail)) {
        return false;
    }
    if (!expect(std::string_view(charizardProfile.spec.path) ==
                    "assets/textures/CharizardFireUVFlipbook.png",
                "resolveProfile should select the Charizard authored fire flipbook from mesh identity.",
                outFail)) {
        return false;
    }
    if (!expect(approx(charizardProfile.uvShift.x, 2.0f) &&
                    approx(charizardProfile.uvShift.y, 3.0f),
                "resolveProfile should precompute the Charizard UV tile shift once from authored mesh UVs.",
                outFail)) {
        return false;
    }
    const auto* cachedAddress = &charizardProfile;
    if (!expect(cachedAddress == &resolveProfile(charizardMesh),
                "resolveProfile should reuse the cached profile for the same immutable mesh.",
                outFail)) {
        return false;
    }

    game::runtime::render_model::MeshData charmeleonMesh;
    charmeleonMesh.nodeNames = {"root", "PM0005_Charmeleon"};
    charmeleonMesh.nodeMesh = {-1, 0};
    charmeleonMesh.meshIndexToNode = {1};
    charmeleonMesh.submeshMeshIndex = {0};

    const auto& charmeleonProfile = resolveProfile(charmeleonMesh);
    if (!expect(!charmeleonProfile.hasFireSubmesh,
                "resolveProfile should leave hasFireSubmesh false when the mesh has no authored fire_mesh nodes.",
                outFail)) {
        return false;
    }
    if (!expect(std::string_view(charmeleonProfile.spec.path) ==
                    "assets/textures/CharmeleonFireUVFlipbook.png",
                "resolveProfile should fall back to the Charmeleon flipbook spec for legacy meshes.",
                outFail)) {
        return false;
    }

    return true;
}
