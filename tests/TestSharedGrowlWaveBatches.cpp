#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/shared/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/SharedGrowlWaveBatches.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_shared_growl_wave_batches_contract(std::string& outFail) {
    using namespace game::runtime::shared_growl_batches;

    GrowlWaveVFX::RenderSnapshot snapshot;
    snapshot.config.fadeStart = 0.65f;
    snapshot.config.meshForwardAxis = glm::vec3(0.0f, 1.0f, 0.0f);

    GrowlWaveVFX::RenderRing ring;
    ring.pos = glm::vec3(1.0f, 0.5f, -2.0f);
    ring.forward = glm::vec3(0.0f, 0.0f, 1.0f);
    ring.lifeSec = 1.0f;
    ring.ageSec = 0.2f;
    ring.startScale = 0.8f;
    ring.endScale = 1.2f;
    ring.randomSeed = 12345u;
    snapshot.rings.push_back(ring);

    GrowlWaveVFX::Config::DrawPass pass;
    pass.id = "growl_test_quarter";
    pass.enabled = true;
    pass.texturePath = "assets/textures/test.png";
    pass.textureQuarterRing = true;
    pass.quarterCount = 2;
    pass.quarterStepDeg = 90.0f;
    pass.scaleMul = 1.0f;
    pass.alphaMul = 0.9f;
    pass.radiusMul = 1.0f;
    pass.thicknessMul = 1.0f;

    const auto tev = game::runtime::shared_growl::resolveTevState(snapshot.config, pass);
    static const unsigned char kTex[4] = {255u, 255u, 255u, 255u};
    TextureView tex;
    tex.rgba = kTex;
    tex.width = 1;
    tex.height = 1;

    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> batches;
    const bool appended =
        appendPassBatch(batches, snapshot, pass, tev, nullptr, tex, glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(appended && batches.size() == 1u,
                "appendPassBatch should append one quarter-ring batch for a valid pass/ring/texture.",
                outFail)) {
        return false;
    }

    const auto& batch = batches.front();
    if (!expect(batch.vertices.size() == 8u && batch.indices.size() == 12u,
                "Quarter-ring growl batch should emit 4 vertices and 6 indices per quarter.",
                outFail)) {
        return false;
    }
    if (!expect(batch.alphaMode == 2u && batch.blendMode == 1u,
                "Growl batches should keep additive blended alpha-mode payload.",
                outFail)) {
        return false;
    }
    if (!expect(batch.textureKey.find("growl:growl_test_quarter:") == 0,
                "Growl batch texture key should be prefixed with growl pass id.",
                outFail)) {
        return false;
    }
    if (!expect(batch.sortDepth > 0.0f,
                "Growl batch should compute a positive sort depth from ring/camera distance.",
                outFail)) {
        return false;
    }

    pass.enabled = false;
    if (!expect(!appendPassBatch(batches, snapshot, pass, tev, nullptr, tex, glm::vec3(0.0f)),
                "appendPassBatch should no-op for disabled draw passes.",
                outFail)) {
        return false;
    }

    return true;
}

