#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"

#include "game/runtime/render_model_cache/RenderModelCache.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace game::runtime::shared_tail_fire_mesh_playback {
namespace {

constexpr int kCharmeleonFlipbookIndex = 0;
constexpr int kCharizardFlipbookIndex = 1;

bool equalsInsensitiveAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool containsInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    for (std::size_t offset = 0; offset + needle.size() <= haystack.size(); ++offset) {
        bool match = true;
        for (std::size_t i = 0; i < needle.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(haystack[offset + i])) !=
                std::tolower(static_cast<unsigned char>(needle[i]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool nodeNameLooksLikeFireMesh(std::string_view nodeName) {
    return containsInsensitive(nodeName, authoredFireMeshNodeToken());
}

const std::array<FlipbookSpec, 2> kFlipbookSpecs{{
    {
        "assets/textures/CharmeleonFireUVFlipbook.png",
        8.0f,
        5.0f,
        40.0f,
        24.0f,
        4096.0f,
        2560.0f,
    },
    {
        "assets/textures/CharizardFireUVFlipbook.png",
        16.0f,
        6.0f,
        81.0f,
        24.0f,
        8192.0f,
        3072.0f,
    },
}};

int selectFlipbookIndex(const render_model::MeshData& mesh) {
    for (const std::string& nodeName : mesh.nodeNames) {
        if (containsInsensitive(nodeName, "pm0006") ||
            containsInsensitive(nodeName, "charizard")) {
            return kCharizardFlipbookIndex;
        }
    }
    for (const std::string& nodeName : mesh.nodeNames) {
        if (containsInsensitive(nodeName, "pm0005") ||
            containsInsensitive(nodeName, "charmeleon")) {
            return kCharmeleonFlipbookIndex;
        }
    }
    return kCharmeleonFlipbookIndex;
}

glm::vec2 computeUvShift(const render_model::MeshData& mesh, int flipbookIndex) {
    if (flipbookIndex != kCharizardFlipbookIndex || mesh.vertices.empty()) {
        return glm::vec2(0.0f, 0.0f);
    }

    float minU = std::numeric_limits<float>::infinity();
    float minV = std::numeric_limits<float>::infinity();
    for (const auto& vertex : mesh.vertices) {
        minU = (std::min)(minU, vertex.uv.x);
        minV = (std::min)(minV, vertex.uv.y);
    }

    return glm::vec2(-std::floor(minU), -std::floor(minV));
}

Profile buildProfile(const render_model::MeshData& mesh) {
    Profile out;
    const int flipbookIndex = selectFlipbookIndex(mesh);
    out.spec = kFlipbookSpecs[static_cast<std::size_t>(flipbookIndex)];
    out.uvShift = computeUvShift(mesh, flipbookIndex);

    out.fireSubmeshMask.assign(mesh.submeshMeshIndex.size(), 0u);
    if (mesh.submeshMeshIndex.empty() || mesh.meshIndexToNode.empty()) {
        return out;
    }

    std::vector<std::uint8_t> fireMeshIndexMask(mesh.meshIndexToNode.size(), 0u);
    for (std::size_t nodeIndex = 0; nodeIndex < mesh.nodeNames.size(); ++nodeIndex) {
        if (!nodeNameLooksLikeFireMesh(mesh.nodeNames[nodeIndex])) continue;
        if (nodeIndex >= mesh.nodeMesh.size()) continue;
        const int meshIndex = mesh.nodeMesh[nodeIndex];
        if (meshIndex < 0 ||
            static_cast<std::size_t>(meshIndex) >= fireMeshIndexMask.size()) {
            continue;
        }
        fireMeshIndexMask[static_cast<std::size_t>(meshIndex)] = 1u;
    }

    for (std::size_t submeshIndex = 0; submeshIndex < mesh.submeshMeshIndex.size(); ++submeshIndex) {
        const int meshIndex = mesh.submeshMeshIndex[submeshIndex];
        if (meshIndex < 0 ||
            static_cast<std::size_t>(meshIndex) >= fireMeshIndexMask.size()) {
            continue;
        }
        if (fireMeshIndexMask[static_cast<std::size_t>(meshIndex)] == 0u) {
            continue;
        }
        out.fireSubmeshMask[submeshIndex] = 1u;
        out.hasFireSubmesh = true;
    }

    return out;
}

struct CachedProfile {
    std::size_t vertexCount = 0u;
    std::size_t nodeNameCount = 0u;
    std::size_t nodeMeshCount = 0u;
    std::size_t submeshCount = 0u;
    std::size_t meshIndexToNodeCount = 0u;
    Profile profile{};
};

thread_local std::unordered_map<const render_model::MeshData*, CachedProfile> g_profiles;

} // namespace

bool isTailFireMeshPlaybackSpecies(std::string_view species) {
    return equalsInsensitiveAscii(species, "charmeleon") ||
           equalsInsensitiveAscii(species, "charizard");
}

std::string_view authoredFireMeshNodeToken() {
    return "fire_mesh";
}

const std::array<FlipbookSpec, 2>& authoredFlipbookSpecs() {
    return kFlipbookSpecs;
}

const Profile& resolveProfile(const render_model::MeshData& mesh) {
    auto& cached = g_profiles[&mesh];
    if (cached.vertexCount == mesh.vertices.size() &&
        cached.nodeNameCount == mesh.nodeNames.size() &&
        cached.nodeMeshCount == mesh.nodeMesh.size() &&
        cached.submeshCount == mesh.submeshMeshIndex.size() &&
        cached.meshIndexToNodeCount == mesh.meshIndexToNode.size()) {
        return cached.profile;
    }

    cached.vertexCount = mesh.vertices.size();
    cached.nodeNameCount = mesh.nodeNames.size();
    cached.nodeMeshCount = mesh.nodeMesh.size();
    cached.submeshCount = mesh.submeshMeshIndex.size();
    cached.meshIndexToNodeCount = mesh.meshIndexToNode.size();
    cached.profile = buildProfile(mesh);
    return cached.profile;
}

} // namespace game::runtime::shared_tail_fire_mesh_playback
