#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAnchorMath.h"
#include "game/vfx/TailFireVFXConfigDB.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>

namespace anchor_math = game::runtime::shared_tail_fire_anchor_math;

namespace game::runtime::shared_tail_fire_coordinator {
namespace {

std::string toLowerCopy(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string normalizePlaybackSpeciesKey(std::string_view species) {
    std::string key = toLowerCopy(species);
    if (key.empty() || !speciesUsesTailFireMeshPlayback(key)) {
        key = std::string(primaryPlaybackSpecies());
    }
    return key;
}

int resolvePlaybackNodeIndex(
    const render_model::MeshData& mesh,
    std::string_view nodeName,
    int fallbackIndex,
    const std::function<int(std::string_view)>& resolveNamedNodeIndex) {
    if (nodeName.empty()) {
        return fallbackIndex;
    }
    for (std::size_t nodeIndex = 0; nodeIndex < mesh.nodeNames.size(); ++nodeIndex) {
        if (mesh.nodeNames[nodeIndex] == nodeName) {
            return static_cast<int>(nodeIndex);
        }
    }
    if (resolveNamedNodeIndex) {
        const int resolvedIndex = resolveNamedNodeIndex(nodeName);
        if (resolvedIndex >= 0) {
            return resolvedIndex;
        }
    }
    return fallbackIndex >= 0 ? fallbackIndex : -1;
}

void logAnchorFrame(
    const AnchorExportArgs& args,
    const shared_tail_fire_fallback::Anchor& anchor,
    int tailNodeIndex,
    int fireAnchorBaseNodeIndex,
    int fireAnchorTipNodeIndex) {
    if (!args.logDebug) {
        return;
    }

    std::cout
        << "[TailFire][Debug][Anchor] unit=" << args.unitId
        << " exact=" << (anchor.exactFireAnchor ? 1 : 0)
        << " tailNode=" << tailNodeIndex
        << " baseNode=" << fireAnchorBaseNodeIndex
        << " tipNode=" << fireAnchorTipNodeIndex;
    if (anchor.exactFireAnchor) {
        std::cout
            << " basePos=(" << anchor.pos.x << "," << anchor.pos.y << "," << anchor.pos.z << ")"
            << " tipPos=(" << anchor.tipPos.x << "," << anchor.tipPos.y << "," << anchor.tipPos.z << ")";
    } else {
        std::cout
            << " tailPos=(" << anchor.pos.x << "," << anchor.pos.y << "," << anchor.pos.z << ")";
    }
    std::cout
        << " up=(" << anchor.basis[1].x << "," << anchor.basis[1].y << "," << anchor.basis[1].z << ")"
        << " back=(" << anchor.backDir.x << "," << anchor.backDir.y << "," << anchor.backDir.z << ")"
        << " scale=" << anchor.particleSizeScale
        << "\n";
}

} // namespace

bool speciesUsesTailFireMeshPlayback(std::string_view species) {
    return shared_tail_fire_mesh_playback::isTailFireMeshPlaybackSpecies(species);
}

bool unitUsesTailFireMeshPlayback(const PokemonInstance& unit) {
    return unit.alive &&
           !unit.fainting &&
           speciesUsesTailFireMeshPlayback(unit.name);
}

const std::array<std::string_view, 3>& playbackSpeciesOrder() {
    static const std::array<std::string_view, 3> kSpecies = {
        "charmander",
        "charmeleon",
        "charizard",
    };
    return kSpecies;
}

std::string_view primaryPlaybackSpecies() {
    return "charmander";
}

const TailFireVFXConfig& resolvePlaybackConfig(std::string_view species) {
    static std::unordered_map<std::string, TailFireVFXConfig> sConfigBySpecies;

    const std::string key = normalizePlaybackSpeciesKey(species);
    const auto found = sConfigBySpecies.find(key);
    if (found != sConfigBySpecies.end()) {
        return found->second;
    }

    const auto start = std::chrono::steady_clock::now();
    TailFireVFXConfig cfg;
    TailFireVFXConfigDB::get().ensureLoaded();
    TailFireVFXConfigDB::get().applyIfAny(key, cfg);
    const auto [it, inserted] = sConfigBySpecies.emplace(key, std::move(cfg));
    const auto end = std::chrono::steady_clock::now();
    if (inserted) {
        const double totalMs =
            std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "[TailFire][CPU] fallback_config species="
                  << key
                  << " total="
                  << totalMs
                  << "ms flipbook0="
                  << it->second.flipbookPath
                  << " flipbook1="
                  << (it->second.useFlipbook2
                          ? it->second.flipbook2Path
                          : std::string("<disabled>"))
                  << "\n";
    }
    return it->second;
}

const TailFireVFXConfig& resolvePrimaryPlaybackConfig() {
    return resolvePlaybackConfig(primaryPlaybackSpecies());
}

const std::array<shared_tail_fire_mesh_playback::FlipbookSpec, 3>& authoredFlipbookSpecs() {
    return shared_tail_fire_mesh_playback::authoredFlipbookSpecs();
}

const shared_tail_fire_mesh_playback::FlipbookSpec* resolvePrimaryAuthoredFlipbookSpec() {
    const auto& specs = authoredFlipbookSpecs();
    return specs.empty() ? nullptr : &specs.front();
}

bool backendUsesAuthoredMeshPlayback(const char* backendId) {
    (void)backendId;
    return true;
}

bool backendUsesGpuClipSkinning(const char* backendId, std::string_view species) {
    return !(backendId &&
             std::string_view(backendId) == "d3d12" &&
             speciesUsesTailFireMeshPlayback(species));
}

const shared_tail_fire_mesh_playback::Profile* resolvePlaybackProfile(
    std::string_view species,
    const render_model::MeshData* mesh) {
    if (!mesh || !speciesUsesTailFireMeshPlayback(species)) {
        return nullptr;
    }
    return &shared_tail_fire_mesh_playback::resolveProfile(*mesh);
}

bool baseSubmeshUsesAuthoredFire(
    std::size_t baseSubmeshIndex,
    const shared_tail_fire_mesh_playback::Profile* profile) {
    return profile &&
           baseSubmeshIndex < profile->fireSubmeshMask.size() &&
           profile->fireSubmeshMask[baseSubmeshIndex] != 0u;
}

bool exportPlaybackAnchor(
    const AnchorExportArgs& args,
    shared_tail_fire_fallback::Anchor& outAnchor) {
    outAnchor = {};
    if (!args.mesh || !args.config || !args.worldMatrixForNode) {
        return false;
    }

    const auto& mesh = *args.mesh;
    const auto& tailCfg = *args.config;
    const auto& nodeGlobals =
        (args.scenePose && args.scenePose->hasScenePose)
            ? args.scenePose->nodeGlobals
            : mesh.bindNodeGlobals;

    const int tailNodeIndex =
        resolvePlaybackNodeIndex(
            mesh,
            tailCfg.tailTipNodeName,
            tailCfg.tailTipNodeIndex,
            args.resolveNamedNodeIndex);
    const int fireAnchorBaseNodeIndex =
        resolvePlaybackNodeIndex(
            mesh,
            tailCfg.fireAnchorBaseNodeName,
            -1,
            args.resolveNamedNodeIndex);
    const int fireAnchorTipNodeIndex =
        resolvePlaybackNodeIndex(
            mesh,
            tailCfg.fireAnchorTipNodeName,
            -1,
            args.resolveNamedNodeIndex);

    const float particleSizeScale =
        std::max(0.01f, std::max(0.01f, mesh.modelScaleFactor) * args.resolvedScaleCorrection);

    const bool hasExactFireAnchorNodes =
        fireAnchorBaseNodeIndex >= 0 &&
        fireAnchorTipNodeIndex >= 0 &&
        static_cast<std::size_t>(fireAnchorBaseNodeIndex) < nodeGlobals.size() &&
        static_cast<std::size_t>(fireAnchorTipNodeIndex) < nodeGlobals.size();
    if (hasExactFireAnchorNodes) {
        const auto frame = anchor_math::buildExactFireAnchorFrame(
            args.worldMatrixForNode(fireAnchorBaseNodeIndex),
            args.worldMatrixForNode(fireAnchorTipNodeIndex),
            tailCfg.backDir);
        outAnchor.valid = true;
        outAnchor.exactFireAnchor = true;
        outAnchor.meshCarrierActive = false;
        outAnchor.pos = frame.posWorld;
        outAnchor.tipPos = frame.tipPosWorld;
        outAnchor.basis = frame.basis;
        outAnchor.backDir = frame.backDirWorld;
        outAnchor.particleSizeScale = particleSizeScale;
        logAnchorFrame(
            args,
            outAnchor,
            tailNodeIndex,
            fireAnchorBaseNodeIndex,
            fireAnchorTipNodeIndex);
        return true;
    }

    if (tailNodeIndex < 0 ||
        static_cast<std::size_t>(tailNodeIndex) >= nodeGlobals.size()) {
        return false;
    }

    const auto frame = anchor_math::buildTailTipAnchorFrame(
        args.worldMatrixForNode(tailNodeIndex),
        tailCfg.backDir);
    outAnchor.valid = true;
    outAnchor.exactFireAnchor = false;
    outAnchor.meshCarrierActive = false;
    outAnchor.pos = frame.posWorld;
    outAnchor.tipPos = frame.tipPosWorld;
    outAnchor.basis = frame.basis;
    outAnchor.backDir = frame.backDirWorld;
    outAnchor.particleSizeScale = particleSizeScale;
    logAnchorFrame(
        args,
        outAnchor,
        tailNodeIndex,
        fireAnchorBaseNodeIndex,
        fireAnchorTipNodeIndex);
    return true;
}

} // namespace game::runtime::shared_tail_fire_coordinator
