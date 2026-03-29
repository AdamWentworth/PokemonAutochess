#include <string>
#include <vector>
#include <cmath>

#include <glm/glm.hpp>

#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBatches.h"

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
                "appendPassBatch should combine quarter-ring growl variants into one batch for a valid pass/ring/texture.",
                outFail)) {
        return false;
    }

    const auto& quarterBatch = batches.front();
    if (!expect(quarterBatch.vertices.empty() && quarterBatch.indices.empty(),
                "Quarter-ring growl batching should reuse shared quad geometry instead of rebuilding vertices.",
                outFail)) {
        return false;
    }
    if (!expect(quarterBatch.sharedVertexCount == 4u && quarterBatch.sharedIndexCount == 6u,
                "Quarter-ring growl batching should reference shared quad geometry.",
                outFail)) {
        return false;
    }
    if (!expect(quarterBatch.instances.size() == 2u,
                "Quarter-ring growl batching should emit one GPU instance per quarter.",
                outFail)) {
        return false;
    }
    if (!expect(quarterBatch.alphaMode == 2u && quarterBatch.blendMode == 1u,
                "Growl batches should keep additive blended alpha-mode payload.",
                outFail)) {
        return false;
    }
    if (!expect(quarterBatch.textureKey.find("growl:growl_test_quarter:") == 0,
                "Growl batch texture key should be prefixed with growl pass id.",
                outFail)) {
        return false;
    }
    if (!expect(quarterBatch.textureCacheKey == "__growl_baked:growl_test_quarter:q:assets/textures/test.png",
                "Growl batch should carry a stable texture cache key so startup prewarm and runtime rendering reuse the same backend texture.",
                outFail)) {
        return false;
    }
    if (!expect(quarterBatch.geometryCacheKey == "__growl_geom_quarter_v1__",
                "Quarter-ring growl batching should use a stable shared geometry cache key.",
                outFail)) {
        return false;
    }
    if (!expect(quarterBatch.sortDepth > 0.0f,
                "Growl batch should compute a positive sort depth from ring/camera distance.",
                outFail)) {
        return false;
    }
    for (const auto& instance : quarterBatch.instances) {
        if (!expect(instance.vertexColorMulA > 0.0f,
                    "Quarter-ring growl batching should preserve fade in the emitted instance data.",
                    outFail)) {
            return false;
        }
    }

    GrowlWaveVFX::Config::DrawPass quarterSequencePass = pass;
    quarterSequencePass.id = "growl_test_quarter_sequence";
    quarterSequencePass.sequenceCount = 3;
    quarterSequencePass.sequenceStep = 0.10f;
    quarterSequencePass.sequenceLife = 0.80f;
    quarterSequencePass.radiusGrowthMul = 1.5f;

    GrowlWaveVFX::RenderSnapshot sequenceSnapshot = snapshot;
    sequenceSnapshot.rings.front().ageSec = 0.50f;
    const auto sequenceTev =
        game::runtime::shared_growl::resolveTevState(sequenceSnapshot.config, quarterSequencePass);
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> sequenceBatches;
    const bool sequenceAppended =
        appendPassBatch(sequenceBatches,
                        sequenceSnapshot,
                        quarterSequencePass,
                        sequenceTev,
                        nullptr,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(sequenceAppended && sequenceBatches.size() == 1u,
                "Quarter-ring growl batching should support sequential expanding ring echoes.",
                outFail)) {
        return false;
    }

    const auto& quarterSequenceBatch = sequenceBatches.front();
    if (!expect(quarterSequenceBatch.instances.size() == 6u,
                "Quarter-ring growl sequencing should emit one quarter instance per visible ring echo.",
                outFail)) {
        return false;
    }
    const auto columnLength = [](const IRenderBackend::WorldMeshInstance& instance,
                                 int columnOffset) {
        const float x = instance.modelMatrix[static_cast<std::size_t>(columnOffset + 0)];
        const float y = instance.modelMatrix[static_cast<std::size_t>(columnOffset + 1)];
        const float z = instance.modelMatrix[static_cast<std::size_t>(columnOffset + 2)];
        return std::sqrt(x * x + y * y + z * z);
    };
    if (!expect(columnLength(quarterSequenceBatch.instances[0], 0) >
                    columnLength(quarterSequenceBatch.instances[4], 0),
                "Older quarter-ring echoes should expand larger than the newest visible echo.",
                outFail)) {
        return false;
    }

    GrowlWaveVFX::Config::DrawPass meshPass;
    meshPass.id = "growl_test_mesh";
    meshPass.eid = 77;
    meshPass.enabled = true;
    meshPass.meshPath = "assets/meshes/test.glb";
    meshPass.texturePath = "assets/textures/test.png";
    meshPass.scaleMul = 1.0f;
    meshPass.alphaMul = 0.8f;
    meshPass.forwardOffset = 0.25f;
    meshPass.radiusMul = 0.6f;
    meshPass.thicknessMul = 0.3f;

    game::runtime::render_model::MeshData mesh;
    mesh.vertices.resize(3u);
    mesh.vertices[0].position = glm::vec3(0.0f, 0.0f, 0.0f);
    mesh.vertices[0].uv = glm::vec2(0.0f, 0.0f);
    mesh.vertices[0].color = glm::vec4(1.0f);
    mesh.vertices[1].position = glm::vec3(1.0f, 0.0f, 0.0f);
    mesh.vertices[1].uv = glm::vec2(1.0f, 0.0f);
    mesh.vertices[1].color = glm::vec4(1.0f);
    mesh.vertices[2].position = glm::vec3(0.0f, 1.0f, 0.0f);
    mesh.vertices[2].uv = glm::vec2(0.0f, 1.0f);
    mesh.vertices[2].color = glm::vec4(1.0f);
    mesh.indices = {0u, 1u, 2u};

    const auto meshTev = game::runtime::shared_growl::resolveTevState(snapshot.config, meshPass);
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> meshBatches;
    const bool meshAppended =
        appendPassBatch(meshBatches,
                        snapshot,
                        meshPass,
                        meshTev,
                        &mesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(meshAppended && meshBatches.size() == 1u,
                "Single-ring growl mesh passes should append one cached shared-geometry batch.",
                outFail)) {
        return false;
    }

    const auto& meshBatch = meshBatches.front();
    if (!expect(meshBatch.vertices.empty() && meshBatch.indices.empty(),
                "Cached growl mesh batches should avoid rebuilding transformed mesh vertices every frame.",
                outFail)) {
        return false;
    }
    if (!expect(meshBatch.sharedVertexCount == 3u && meshBatch.sharedIndexCount == 3u,
                "Cached growl mesh batches should reference the source mesh geometry.",
                outFail)) {
        return false;
    }
    if (!expect(meshBatch.geometryCacheKey == "__growl_geom_mesh_v1__:assets/meshes/test.glb",
                "Cached growl mesh batches should use a stable geometry cache key based on the source mesh path.",
                outFail)) {
        return false;
    }
    if (!expect(meshBatch.vertexColorMulA > 0.0f,
                "Cached growl mesh batches should carry their fade via the vertex color multiplier alpha.",
                outFail)) {
        return false;
    }

    GrowlWaveVFX::Config::DrawPass meshQuarterTexturePass = meshPass;
    meshQuarterTexturePass.id = "growl_test_mesh_quarter_tex";
    meshQuarterTexturePass.fragShaderPath =
        "assets/shaders/vfx/moves/growl/growl_quarter_ring_shared.frag";

    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> meshQuarterBatches;
    const bool meshQuarterAppended =
        appendPassBatch(meshQuarterBatches,
                        snapshot,
                        meshQuarterTexturePass,
                        meshTev,
                        &mesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(meshQuarterAppended && meshQuarterBatches.size() == 1u,
                "Mesh growl passes using the quarter-ring shader should still append one mesh batch.",
                outFail)) {
        return false;
    }

    const auto& meshQuarterBatch = meshQuarterBatches.front();
    if (!expect(meshQuarterBatch.sharedVertexCount == mesh.vertices.size() &&
                    meshQuarterBatch.sharedIndexCount == mesh.indices.size() &&
                    meshQuarterBatch.geometryCacheKey == "__growl_geom_mesh_v1__:assets/meshes/test.glb",
                "Quarter-shaded growl mesh passes should keep mesh geometry rather than falling back to quarter-ring quad geometry.",
                outFail)) {
        return false;
    }
    if (!expect(meshQuarterBatch.textureCacheKey ==
                    "__growl_baked:growl_test_mesh_quarter_tex:q:assets/textures/test.png",
                "Quarter-shaded growl mesh passes should reuse the quarter-style baked texture cache key.",
                outFail)) {
        return false;
    }

    GrowlWaveVFX::Config::DrawPass sparklePass = meshQuarterTexturePass;
    sparklePass.id = "growl_test_sparkle_mesh";
    sparklePass.renderMode = "sparkle_mesh";
    sparklePass.scaleMul = 0.5f;
    sparklePass.alphaMul = 1.0f;
    sparklePass.forwardOffset = 0.15f;
    sparklePass.heightOffset = 0.5f;
    sparklePass.radiusMul = 1.0f;
    sparklePass.thicknessMul = 1.0f;
    sparklePass.startRadiusMul = 0.5f;

    game::runtime::render_model::MeshData sparkleMesh;
    sparkleMesh.vertices.resize(4u);
    sparkleMesh.vertices[0].position = glm::vec3(-1.0f, 0.0f, -1.0f);
    sparkleMesh.vertices[0].uv = glm::vec2(0.0f, 0.0f);
    sparkleMesh.vertices[0].color = glm::vec4(1.0f);
    sparkleMesh.vertices[1].position = glm::vec3(1.0f, 0.0f, -1.0f);
    sparkleMesh.vertices[1].uv = glm::vec2(1.0f, 0.0f);
    sparkleMesh.vertices[1].color = glm::vec4(1.0f);
    sparkleMesh.vertices[2].position = glm::vec3(-1.0f, 0.0f, 1.0f);
    sparkleMesh.vertices[2].uv = glm::vec2(0.0f, 1.0f);
    sparkleMesh.vertices[2].color = glm::vec4(1.0f);
    sparkleMesh.vertices[3].position = glm::vec3(1.0f, 0.0f, 1.0f);
    sparkleMesh.vertices[3].uv = glm::vec2(1.0f, 1.0f);
    sparkleMesh.vertices[3].color = glm::vec4(1.0f);
    sparkleMesh.indices = {0u, 1u, 2u, 2u, 1u, 3u};

    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> sparkleBatches;
    const bool sparkleAppended =
        appendPassBatch(sparkleBatches,
                        snapshot,
                        sparklePass,
                        meshTev,
                        &sparkleMesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(sparkleAppended && sparkleBatches.size() == 1u,
                "Sparkle growl mesh passes should append one batch.",
                outFail)) {
        return false;
    }

    const auto& sparkleBatch = sparkleBatches.front();
    if (!expect(sparkleBatch.sharedVertexCount == 4u &&
                    sparkleBatch.sharedIndexCount == 6u &&
                    sparkleBatch.instances.size() == 1u,
                "Sparkle growl mesh passes should convert authored quad markers into billboard instances.",
                outFail)) {
        return false;
    }
    if (!expect(sparkleBatch.geometryCacheKey ==
                    "__growl_geom_quarter_v1__",
                "Sparkle growl mesh passes should render with the shared quarter-quad geometry so the sparkle faces the camera.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(sparkleBatch.instances.front().modelMatrix[12]) > 0.0001f ||
                    std::abs(sparkleBatch.instances.front().modelMatrix[13]) > 0.0001f ||
                    std::abs(sparkleBatch.instances.front().modelMatrix[14]) > 0.0001f,
                "Sparkle growl billboards should be positioned near the active ring rather than left at the origin.",
                outFail)) {
        return false;
    }
    if (!expect(sparkleBatch.instances.front().modelMatrix[14] > ring.pos.z,
                "Sparkle growl billboards should stay in front of the emitter rather than behind the caster.",
                outFail)) {
        return false;
    }

    GrowlWaveVFX::Config::DrawPass linePass;
    linePass.id = "growl_test_line";
    linePass.eid = 1128;
    linePass.enabled = true;
    linePass.meshPath = "assets/meshes/test_line.glb";
    linePass.texturePath.clear();
    linePass.fragShaderPath = "assets/shaders/vfx/moves/growl/growl_line_shared.frag";
    linePass.meshForwardAxis = glm::vec3(0.0f, 0.0f, -1.0f);
    linePass.overrideMeshForwardAxis = true;
    linePass.scaleMul = 0.44f;
    linePass.alphaMul = 0.3f;
    linePass.heightOffset = 0.25f;
    linePass.startRadiusMul = 0.25f;
    linePass.radiusMul = 0.5f;
    linePass.thicknessMul = 0.48f;
    linePass.directionSpacingJitterDeg = 12.0f;
    linePass.lineAlphaMin = 0.55f;
    linePass.lineAlphaMax = 1.45f;
    linePass.tintColor = glm::vec3(0.8f, 0.6f, 0.4f);
    linePass.directionsLocal = {
        glm::vec3(1.1f, 0.0f, 1.0f),
        glm::vec3(0.0f, 1.1f, 1.0f),
        glm::vec3(-1.1f, 0.0f, 1.0f),
        glm::vec3(0.0f, -1.1f, 1.0f),
    };

    const auto lineTev = game::runtime::shared_growl::resolveTevState(snapshot.config, linePass);
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> lineBatches;
    const bool lineAppended =
        appendPassBatch(lineBatches,
                        snapshot,
                        linePass,
                        lineTev,
                        &mesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(lineAppended && lineBatches.size() == 1u,
                "Single-ring growl line passes should collapse repeated directions into one combined batch.",
                outFail)) {
        return false;
    }

    const auto& lineBatch = lineBatches.front();
    if (!expect(lineBatch.vertices.empty() && lineBatch.indices.empty(),
                "Growl line batching should reuse shared geometry instead of rebuilding per-direction meshes.",
                outFail)) {
        return false;
    }
    if (!expect(lineBatch.sharedVertexCount == mesh.vertices.size() &&
                    lineBatch.sharedIndexCount == mesh.indices.size(),
                "Growl line batching should reference the cached shared mesh geometry.",
                outFail)) {
        return false;
    }
    if (!expect(lineBatch.instances.size() == linePass.directionsLocal.size(),
                "Growl line batching should emit one GPU instance per direction.",
                outFail)) {
        return false;
    }
    if (!expect(lineBatch.geometryCacheKey ==
                    "__growl_geom_line_v1__:growl_test_line:assets/meshes/test_line.glb:1000",
                "Growl line batching should use a stable geometry cache key that includes quantized line alpha state.",
                outFail)) {
        return false;
    }
    if (!expect(lineBatch.textureCacheKey == "__growl_white__",
                "Combined growl line batches should share the white texture cache entry.",
                outFail)) {
        return false;
    }
    bool sawTintedInstance = false;
    for (const auto& instance : lineBatch.instances) {
        sawTintedInstance = sawTintedInstance ||
            (instance.vertexColorMulR > 0.0f &&
             instance.vertexColorMulG > 0.0f &&
             instance.vertexColorMulB > 0.0f &&
             instance.vertexColorMulA > 0.0f);
    }
    if (!expect(sawTintedInstance,
                "Growl line batching should carry tint and fade in the per-instance color payload.",
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

