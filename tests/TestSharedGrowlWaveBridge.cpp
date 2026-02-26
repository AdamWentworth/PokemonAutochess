#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBridge.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

game::runtime::backend_model::MeshData makeTinyMesh() {
    game::runtime::backend_model::MeshData mesh;
    game::runtime::backend_model::MeshVertex a, b, c;
    a.position = glm::vec3(0.0f, 0.0f, 0.0f);
    b.position = glm::vec3(1.0f, 0.0f, 0.0f);
    c.position = glm::vec3(0.0f, 0.0f, 1.0f);
    a.color = b.color = c.color = glm::vec4(1.0f);
    a.uv = glm::vec2(0.0f, 0.0f);
    b.uv = glm::vec2(1.0f, 0.0f);
    c.uv = glm::vec2(0.0f, 1.0f);
    mesh.vertices = {a, b, c};
    mesh.indices = {0u, 1u, 2u};
    return mesh;
}

} // namespace

bool test_shared_growl_wave_bridge_contract(std::string& outFail) {
    using namespace game::runtime::shared_growl_bridge;

    GrowlWaveVFX::RenderSnapshot snapshot;
    snapshot.config.meshForwardAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    snapshot.config.fadeStart = 0.65f;

    GrowlWaveVFX::RenderRing ring;
    ring.pos = glm::vec3(0.0f, 0.5f, 0.0f);
    ring.forward = glm::vec3(0.0f, 0.0f, 1.0f);
    ring.lifeSec = 1.0f;
    ring.ageSec = 0.15f;
    ring.startScale = 0.8f;
    ring.endScale = 1.1f;
    ring.randomSeed = 7u;
    snapshot.rings.push_back(ring);

    GrowlWaveVFX::Config::DrawPass quarterPass;
    quarterPass.id = "quarter";
    quarterPass.enabled = true;
    quarterPass.textureQuarterRing = true;
    quarterPass.texturePath = "quarter.png";
    quarterPass.quarterCount = 1;
    quarterPass.alphaMul = 1.0f;
    quarterPass.scaleMul = 1.0f;
    snapshot.drawPasses.push_back(quarterPass);

    GrowlWaveVFX::Config::DrawPass meshPass;
    meshPass.id = "mesh";
    meshPass.enabled = true;
    meshPass.textureQuarterRing = false;
    meshPass.meshPath = "mesh.glb";
    meshPass.texturePath = "mesh.png";
    meshPass.alphaMul = 1.0f;
    meshPass.scaleMul = 1.0f;
    snapshot.drawPasses.push_back(meshPass);

    game::runtime::backend_model::MeshData tinyMesh = makeTinyMesh();
    static const unsigned char kWhite[4] = {255u, 255u, 255u, 255u};
    int meshResolveCalls = 0;
    int textureResolveCalls = 0;

    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> outBatches;
    const bool appended = appendBatches(
        snapshot,
        outBatches,
        glm::vec3(0.0f, 1.0f, 3.0f),
        [&](const std::string& meshPath) -> game::runtime::backend_model::MeshData* {
            ++meshResolveCalls;
            if (meshPath == "mesh.glb") return &tinyMesh;
            return nullptr;
        },
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const game::runtime::shared_growl::TevState&,
            game::runtime::shared_growl_batches::TextureView& outTex) {
            ++textureResolveCalls;
            if (!pass.enabled) return false;
            outTex.rgba = kWhite;
            outTex.width = 1;
            outTex.height = 1;
            return true;
        });

    if (!expect(appended,
                "appendBatches should append at least one growl batch when valid passes/rings resolve.",
                outFail)) {
        return false;
    }
    if (!expect(outBatches.size() == 2u,
                "appendBatches should append one batch per valid growl pass (quarter + mesh in this contract).",
                outFail)) {
        return false;
    }
    if (!expect(meshResolveCalls == 1,
                "appendBatches should only resolve meshes for non-quarter growl passes.",
                outFail)) {
        return false;
    }
    if (!expect(textureResolveCalls == 2,
                "appendBatches should resolve textures for each enabled pass.",
                outFail)) {
        return false;
    }

    snapshot.drawPasses.clear();
    if (!expect(!appendBatches(snapshot,
                               outBatches,
                               glm::vec3(0.0f),
                               [&](const std::string&) { return &tinyMesh; },
                               [&](const GrowlWaveVFX::Config::DrawPass&,
                                   const game::runtime::shared_growl::TevState&,
                                   game::runtime::shared_growl_batches::TextureView&) { return true; }),
                "appendBatches should no-op when no draw passes are present.",
                outFail)) {
        return false;
    }

    return true;
}

