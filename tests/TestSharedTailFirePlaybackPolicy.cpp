#include <string>
#include <vector>

#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_shared_tail_fire_playback_policy_contract(std::string& outFail) {
    using namespace game::runtime::shared_tail_fire_playback_policy;

    if (!expect(usesAuthoredFireMeshMaterialFlags(
                    static_cast<float>(kAuthoredFireMeshFlagBit)),
                "usesAuthoredFireMeshMaterialFlags should detect the authored fire-mesh bit.",
                outFail)) {
        return false;
    }
    if (!expect(!usesAuthoredFireMeshMaterialFlags(0.0f),
                "usesAuthoredFireMeshMaterialFlags should ignore empty material flags.",
                outFail)) {
        return false;
    }

    game::runtime::shared_world_batches::WorldIndexedBatch templateBatch;
    templateBatch.materialFlags = static_cast<float>(kAuthoredFireMeshFlagBit);

    game::runtime::shared_world_batches::WorldIndexedBatch inheritedBatch;
    inheritedBatch.sharedTemplate = &templateBatch;

    game::runtime::shared_world_batches::WorldIndexedBatch plainBatch;
    plainBatch.materialFlags = 0.0f;

    if (!expect(batchUsesAuthoredFireMesh(templateBatch) &&
                    batchUsesAuthoredFireMesh(inheritedBatch) &&
                    !batchUsesAuthoredFireMesh(plainBatch),
                "batchUsesAuthoredFireMesh should resolve authored fire state through shared templates.",
                outFail)) {
        return false;
    }

    const std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> plainBatches{
        plainBatch
    };
    const std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> authoredBatches{
        inheritedBatch,
        plainBatch
    };

    if (!expect(!hasAuthoredFireMeshBatches(plainBatches) &&
                    hasAuthoredFireMeshBatches(authoredBatches),
                "hasAuthoredFireMeshBatches should only succeed when at least one authored fire batch is present.",
                outFail)) {
        return false;
    }

    if (!expect(shouldRenderSyntheticTailFireFallback("bulbasaur", plainBatches) &&
                    shouldRenderSyntheticTailFireFallback("charmander", plainBatches) &&
                    shouldRenderSyntheticTailFireFallback("charmeleon", plainBatches) &&
                    shouldRenderSyntheticTailFireFallback("charizard", plainBatches) &&
                    !shouldRenderSyntheticTailFireFallback("charmander", authoredBatches) &&
                    !shouldRenderSyntheticTailFireFallback("charmeleon", authoredBatches) &&
                    !shouldRenderSyntheticTailFireFallback("charizard", authoredBatches),
                "shouldRenderSyntheticTailFireFallback should preserve fallback for non-mesh species and only suppress Charmander-line fallback when authored batches really exist.",
                outFail)) {
        return false;
    }

    return true;
}
