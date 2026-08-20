#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/render_model_cache/RenderModelCacheFormat.h"
#include "game/runtime/render_model_cache/RenderModelCacheLoadOrRebuild.h"
#include "game/runtime/render_model_cache/RenderModelCacheReadDecode.h"
#include "game/runtime/render_model_cache/RenderModelCacheSourceBuild.h"
#include "game/runtime/render_model_cache/RenderModelCacheWrite.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "engine/core/Environment.h"

namespace {
namespace fs = std::filesystem;

std::uint64_t fnv1a64(const std::string& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::string hexHash64(std::uint64_t v) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << v;
    return out.str();
}

bool rebuildCacheFromSource(const std::string& modelPath, std::string* outError) {
    game::runtime::render_model::detail::SourceCacheBuildData data;
    std::string err;
    if (!game::runtime::render_model::detail::buildRenderCacheSourceData(modelPath, data, &err)) {
        if (outError) *outError = err;
        return false;
    }
    if (!game::runtime::render_model::detail::writeRenderCacheFromSourceData(modelPath, data, &err)) {
        if (outError) *outError = err;
        return false;
    }
    return true;
}

bool isNativeModelManifestPath(const std::string& modelPath) {
    std::string extension = fs::path(modelPath).extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return extension == ".phmodel";
}

std::string canonicalNodeName(std::string_view sourceName) {
    const std::size_t separator = sourceName.find_last_of("|/:\\");
    if (separator != std::string_view::npos) {
        sourceName.remove_prefix(separator + 1u);
    }
    std::string name(sourceName);
    std::transform(
        name.begin(),
        name.end(),
        name.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return name;
}

bool isSupportJointName(std::string_view sourceName) {
    const std::string name = canonicalNodeName(sourceName);
    constexpr std::array<std::string_view, 4u> tokens{
        "foot", "toe", "hoof", "paw"};
    return std::any_of(
        tokens.begin(),
        tokens.end(),
        [&](std::string_view token) {
            return name.find(token) != std::string::npos;
        });
}

int gameFreakSerpentineGroundNode(const game::runtime::render_model::MeshData& mesh) {
    int originNode = -1;
    bool hasWaist = false;
    bool hasSpine = false;
    bool hasTail = false;
    for (std::size_t node = 0u; node < mesh.nodeNames.size(); ++node) {
        const std::string name = canonicalNodeName(mesh.nodeNames[node]);
        if (name == "origin") {
            originNode = static_cast<int>(node);
        } else if (name == "waist") {
            hasWaist = true;
        } else if (name == "spine_01") {
            hasSpine = true;
        } else if (name == "tail_01") {
            hasTail = true;
        }
    }
    return originNode >= 0 && hasWaist && hasSpine && hasTail
        ? originNode
        : -1;
}

struct SupportContactCacheEntry {
    std::string assetCacheIdentity;
    std::size_t vertexCount = 0u;
    std::size_t indexCount = 0u;
    const std::string* nodeNamesData = nullptr;
    float contactY = 0.0f;
};

} // namespace

namespace game::runtime::render_model {

std::string cachePathForModel(const std::string& modelPath) {
    const fs::path out = fs::path("cache") / "models" / (hexHash64(fnv1a64(modelPath)) + ".pacmdl");
    return out.string();
}

bool loadLegacyMeshFromCache(
    const std::string& modelPath,
    MeshData& out,
    std::string* outError) {
    out = MeshData{};
    if (engine::env::truthyNonZero("PAC_DISABLE_MODELCACHE")) {
        if (outError) *outError = "model cache disabled by PAC_DISABLE_MODELCACHE";
        return false;
    }
    if (modelPath.empty()) {
        if (outError) *outError = "empty model path";
        return false;
    }

    std::ifstream in;
    detail::CacheHeader hdr{};
    if (!detail::openValidatedCacheStreamForModel(modelPath,
                                                  &cachePathForModel,
                                                  &rebuildCacheFromSource,
                                                  in,
                                                  hdr,
                                                  outError)) {
        return false;
    }

    return detail::decodeMeshFromValidatedCacheStream(in, hdr, out, outError);
}

bool loadMeshFromCache(
    const std::string& modelPath,
    MeshData& out,
    std::string* outError) {
    out = MeshData{};
    const std::string phloPath =
        phlosion::objectPathForModel(modelPath);
    std::error_code errorCode;
    if (fs::exists(phloPath, errorCode) && !errorCode) {
        if (!phlosion::loadModelObject(phloPath, out, outError)) {
            return false;
        }
        if (engine::env::truthyNonZero(
                "PHLOSION_TRACE_ASSET_LOADS")) {
            std::clog
                << "[Phlosion][PHLO] " << modelPath
                << " -> " << phloPath << "\n";
        }
        return true;
    }
    const bool nativeModel = isNativeModelManifestPath(modelPath);
    if (nativeModel || engine::env::truthyNonZero(
            "PHLOSION_REQUIRE_COOKED_ASSETS")) {
        if (outError) {
            *outError = nativeModel
                ? "native .phmodel assets are offline inputs and require a cooked PHLO prefab for " +
                    modelPath + ": " + phloPath
                : "required PHLO prefab is missing for " + modelPath +
                    ": " + phloPath;
        }
        return false;
    }
    return loadLegacyMeshFromCache(modelPath, out, outError);
}

float modelSupportContactY(const MeshData& mesh) {
    static thread_local std::unordered_map<
        const MeshData*,
        SupportContactCacheEntry> cache;
    const auto found = cache.find(&mesh);
    if (found != cache.end() &&
        found->second.assetCacheIdentity == mesh.assetCacheIdentity &&
        found->second.vertexCount == mesh.vertices.size() &&
        found->second.indexCount == mesh.indices.size() &&
        found->second.nodeNamesData == mesh.nodeNames.data()) {
        return found->second.contactY;
    }

    std::vector<std::unordered_set<std::uint16_t>>
        supportSlotsBySkin(mesh.skins.size());
    std::unordered_set<int> supportNodes;
    for (std::size_t skinIndex = 0u;
         skinIndex < mesh.skins.size();
         ++skinIndex) {
        const auto& joints = mesh.skins[skinIndex].joints;
        auto& slots = supportSlotsBySkin[skinIndex];
        for (std::size_t slot = 0u; slot < joints.size(); ++slot) {
            const int node = joints[slot];
            if (node < 0 ||
                static_cast<std::size_t>(node) >=
                    mesh.nodeNames.size() ||
                !isSupportJointName(
                    mesh.nodeNames[static_cast<std::size_t>(node)])) {
                continue;
            }
            slots.insert(static_cast<std::uint16_t>(slot));
            supportNodes.insert(node);
        }
    }

    float contactY = std::numeric_limits<float>::max();
    const std::size_t triangleCount = mesh.indices.size() / 3u;
    for (std::size_t triangle = 0u;
         triangle < triangleCount;
         ++triangle) {
        int skinIndex =
            triangle < mesh.triangleSkinIndex.size()
                ? mesh.triangleSkinIndex[triangle]
                : -1;
        if (skinIndex < 0 && mesh.skins.size() == 1u) {
            skinIndex = 0;
        }
        if (skinIndex < 0 ||
            static_cast<std::size_t>(skinIndex) >=
                supportSlotsBySkin.size()) {
            continue;
        }
        const auto& supportSlots = supportSlotsBySkin[
            static_cast<std::size_t>(skinIndex)];
        if (supportSlots.empty()) continue;
        for (std::size_t corner = 0u; corner < 3u; ++corner) {
            const std::uint32_t vertexIndex =
                mesh.indices[triangle * 3u + corner];
            if (vertexIndex >= mesh.vertices.size()) continue;
            const auto& vertex = mesh.vertices[vertexIndex];
            const std::uint16_t joints[4] = {
                vertex.j0, vertex.j1, vertex.j2, vertex.j3};
            const float weights[4] = {
                vertex.w0, vertex.w1, vertex.w2, vertex.w3};
            bool footWeighted = false;
            for (std::size_t component = 0u;
                 component < 4u;
                 ++component) {
                if (weights[component] > 0.01f &&
                    supportSlots.find(joints[component]) !=
                        supportSlots.end()) {
                    footWeighted = true;
                    break;
                }
            }
            if (footWeighted && std::isfinite(vertex.position.y)) {
                contactY = std::min(contactY, vertex.position.y);
            }
        }
    }

    if (!std::isfinite(contactY) ||
        contactY == std::numeric_limits<float>::max()) {
        // Some Game Freak rigs author EffFoot nodes as explicit ground/effect
        // anchors without skinning any vertices to them (Caterpie is one
        // example). Raw native vertex bounds are not in the final posed model
        // space for those rigs, so treating boundsMin as a foot makes the
        // rendered character float. Prefer the authored support-node origins
        // in bind-model space before falling back to geometry bounds.
        for (const int node : supportNodes) {
            if (node < 0 ||
                static_cast<std::size_t>(node) >=
                    mesh.bindNodeGlobals.size()) {
                continue;
            }
            const float anchorY =
                mesh.bindNodeGlobals[static_cast<std::size_t>(node)][3].y;
            if (std::isfinite(anchorY)) {
                contactY = std::min(contactY, anchorY);
            }
        }
    }
    if ((!std::isfinite(contactY) ||
         contactY == std::numeric_limits<float>::max()) &&
        supportNodes.empty()) {
        // Game Freak's legless snake rigs have no foot/effect joint. Their
        // long straight bind mesh crosses far below Y=0, while the selected
        // motion coils the spine around an origin authored on the floor.
        // Treat that origin as the support plane instead of lifting the posed
        // model by the unrelated bind-mesh minimum.
        const int groundNode = gameFreakSerpentineGroundNode(mesh);
        if (groundNode >= 0 &&
            static_cast<std::size_t>(groundNode) <
                mesh.bindNodeGlobals.size()) {
            const float anchorY = mesh.bindNodeGlobals[
                static_cast<std::size_t>(groundNode)][3].y;
            if (std::isfinite(anchorY)) {
                contactY = anchorY;
            }
        }
    }
    if (!std::isfinite(contactY) ||
        contactY == std::numeric_limits<float>::max()) {
        contactY = mesh.boundsMin.y;
    }
    SupportContactCacheEntry entry;
    entry.assetCacheIdentity = mesh.assetCacheIdentity;
    entry.vertexCount = mesh.vertices.size();
    entry.indexCount = mesh.indices.size();
    entry.nodeNamesData = mesh.nodeNames.data();
    entry.contactY = contactY;
    cache[&mesh] = std::move(entry);
    return contactY;
}

} // namespace game::runtime::render_model
