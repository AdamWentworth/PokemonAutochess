#include <string>
#include <vector>

#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"

bool test_shared_tail_fire_preview_runtime_parity_contract(std::string& outFail) {
    namespace playback = game::runtime::shared_tail_fire_playback_policy;

    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> batches;
    if (playback::resolvePlaybackMode("charmander", batches) !=
        playback::PlaybackMode::None) {
        outFail =
            "Native Scarlet Charmander should bypass legacy Tail Fire playback.";
        return false;
    }
    if (playback::resolvePlaybackMode("charmeleon", batches) !=
        playback::PlaybackMode::SyntheticFallback) {
        outFail =
            "Preview/runtime Tail Fire parity should resolve Charmeleon without authored batches to synthetic fallback.";
        return false;
    }
    if (playback::resolvePlaybackMode("charizard", batches) !=
        playback::PlaybackMode::SyntheticFallback) {
        outFail =
            "Preview/runtime Tail Fire parity should resolve Charizard without authored batches to synthetic fallback.";
        return false;
    }

    game::runtime::shared_world_batches::WorldIndexedBatch authoredBatch;
    authoredBatch.materialFlags =
        static_cast<float>(playback::kAuthoredFireMeshFlagBit);
    batches.push_back(authoredBatch);
    if (playback::resolvePlaybackMode("charmander", batches) !=
        playback::PlaybackMode::None) {
        outFail =
            "Native Scarlet Charmander should ignore legacy authored Tail Fire batches.";
        return false;
    }
    if (playback::resolvePlaybackMode("charmeleon", batches) !=
        playback::PlaybackMode::AuthoredMesh) {
        outFail =
            "Preview/runtime Tail Fire parity should resolve Charmeleon with authored batches to authored mesh playback.";
        return false;
    }
    if (playback::resolvePlaybackMode("charizard", batches) !=
        playback::PlaybackMode::AuthoredMesh) {
        outFail =
            "Preview/runtime Tail Fire parity should resolve Charizard with authored batches to authored mesh playback.";
        return false;
    }

    if (playback::resolvePlaybackMode("pikachu", batches) !=
        playback::PlaybackMode::None) {
        outFail =
            "Preview/runtime Tail Fire parity should resolve non-tail-fire species to no Tail Fire playback mode.";
        return false;
    }

    return true;
}
