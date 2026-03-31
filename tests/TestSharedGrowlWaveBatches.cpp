#include <string>
#include <vector>
#include <cmath>

#include <glm/glm.hpp>

#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlWaveBatches.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_shared_growl_wave_batches_contract(std::string& outFail) {
    using namespace vfx::runtime::growl_batches;
    using Batch = WorldIndexedBatch;
    using MeshData = vfx::runtime::growl_batches::MeshData;

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

    const auto tev = vfx::runtime::growl::resolveTevState(snapshot.config, pass);
    static const unsigned char kTex[4] = {255u, 255u, 255u, 255u};
    TextureView tex;
    tex.rgba = kTex;
    tex.width = 1;
    tex.height = 1;

    std::vector<Batch> batches;
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
        vfx::runtime::growl::resolveTevState(sequenceSnapshot.config, quarterSequencePass);
    std::vector<Batch> sequenceBatches;
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

    MeshData mesh;
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

    const auto meshTev = vfx::runtime::growl::resolveTevState(snapshot.config, meshPass);
    std::vector<Batch> meshBatches;
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

    GrowlWaveVFX::Config::DrawPass delayedMeshPass = meshPass;
    delayedMeshPass.id = "growl_test_mesh_delayed";
    delayedMeshPass.sequenceCount = 3;
    delayedMeshPass.sequenceIndex = 1;
    delayedMeshPass.sequenceStep = 0.30f;
    delayedMeshPass.sequenceLife = 0.55f;

    GrowlWaveVFX::RenderSnapshot delayedMeshEarlySnapshot = snapshot;
    delayedMeshEarlySnapshot.rings.front().ageSec = 0.20f;
    std::vector<Batch> delayedMeshEarlyBatches;
    const bool delayedMeshEarlyAppended =
        appendPassBatch(delayedMeshEarlyBatches,
                        delayedMeshEarlySnapshot,
                        delayedMeshPass,
                        meshTev,
                        &mesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(!delayedMeshEarlyAppended && delayedMeshEarlyBatches.empty(),
                "Delayed growl mesh passes should stay hidden until their assigned timing slot begins.",
                outFail)) {
        return false;
    }

    GrowlWaveVFX::RenderSnapshot delayedMeshLiveSnapshot = snapshot;
    delayedMeshLiveSnapshot.rings.front().ageSec = 0.40f;
    std::vector<Batch> delayedMeshLiveBatches;
    const bool delayedMeshLiveAppended =
        appendPassBatch(delayedMeshLiveBatches,
                        delayedMeshLiveSnapshot,
                        delayedMeshPass,
                        meshTev,
                        &mesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(delayedMeshLiveAppended && delayedMeshLiveBatches.size() == 1u,
                "Delayed growl mesh passes should become visible once their timing slot is active.",
                outFail)) {
        return false;
    }
    const auto& delayedMeshLiveBatch = delayedMeshLiveBatches.front();
    const float delayedMeshLiveZ = delayedMeshLiveBatch.modelMatrix[14];
    const float delayedMeshOriginZ = delayedMeshLiveSnapshot.rings.front().pos.z;
    if (!expect(delayedMeshLiveZ > delayedMeshOriginZ &&
                    delayedMeshLiveZ < delayedMeshOriginZ + delayedMeshPass.forwardOffset,
                "Delayed growl mesh passes should start inside their paired ring before reaching their full forward travel.",
                outFail)) {
        return false;
    }

    GrowlWaveVFX::RenderSnapshot delayedMeshLateSnapshot = snapshot;
    delayedMeshLateSnapshot.rings.front().ageSec = 0.65f;
    std::vector<Batch> delayedMeshLateBatches;
    const bool delayedMeshLateAppended =
        appendPassBatch(delayedMeshLateBatches,
                        delayedMeshLateSnapshot,
                        delayedMeshPass,
                        meshTev,
                        &mesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(delayedMeshLateAppended && delayedMeshLateBatches.size() == 1u,
                "Delayed growl mesh passes should remain visible later in their local lifetime.",
                outFail)) {
        return false;
    }
    const auto& delayedMeshLateBatch = delayedMeshLateBatches.front();
    if (!expect(delayedMeshLateBatch.modelMatrix[14] > delayedMeshLiveZ,
                "Delayed growl mesh passes should advance farther forward as their local lifetime progresses.",
                outFail)) {
        return false;
    }

    GrowlWaveVFX::RenderSnapshot delayedMeshSharedFadeSnapshot = snapshot;
    delayedMeshSharedFadeSnapshot.rings.front().ageSec = 0.90f;
    std::vector<Batch> delayedMeshSharedFadeBatches;
    const bool delayedMeshSharedFadeAppended =
        appendPassBatch(delayedMeshSharedFadeBatches,
                        delayedMeshSharedFadeSnapshot,
                        delayedMeshPass,
                        meshTev,
                        &mesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(delayedMeshSharedFadeAppended && delayedMeshSharedFadeBatches.size() == 1u,
                "Delayed growl mesh passes should stay visible until the shared end-of-sequence fade instead of dying on their individual launch lifetime.",
                outFail)) {
        return false;
    }
    const auto& delayedMeshSharedFadeBatch = delayedMeshSharedFadeBatches.front();
    if (!expect(delayedMeshSharedFadeBatch.modelMatrix[14] >= delayedMeshLateBatch.modelMatrix[14],
                "Delayed growl mesh passes should hold their fully launched forward position while waiting for the shared fade-out.",
                outFail)) {
        return false;
    }

    GrowlWaveVFX::Config::DrawPass meshQuarterTexturePass = meshPass;
    meshQuarterTexturePass.id = "growl_test_mesh_quarter_tex";
    meshQuarterTexturePass.fragShaderPath =
        "assets/shaders/vfx/moves/growl/growl_quarter_ring_shared.frag";

    std::vector<Batch> meshQuarterBatches;
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

    MeshData sparkleMesh;
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

    std::vector<Batch> sparkleBatches;
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

    GrowlWaveVFX::Config::DrawPass glowPass;
    glowPass.id = "growl_test_glow_billboard";
    glowPass.eid = 1284;
    glowPass.renderMode = "glow_billboard";
    glowPass.texturePath = "assets/textures/test_glow.png";
    glowPass.fragShaderPath =
        "assets/shaders/vfx/moves/growl/growl_quarter_ring_shared.frag";
    glowPass.scaleMul = 1.4f;
    glowPass.alphaMul = 0.9f;
    glowPass.radiusMul = 1.0f;
    glowPass.thicknessMul = 1.0f;

    std::vector<Batch> glowBatches;
    const bool glowAppended =
        appendPassBatch(glowBatches,
                        snapshot,
                        glowPass,
                        meshTev,
                        nullptr,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(glowAppended && glowBatches.size() == 1u,
                "Glow billboard growl passes should append one no-mesh batch.",
                outFail)) {
        return false;
    }

    const auto& glowBatch = glowBatches.front();
    if (!expect(glowBatch.sharedVertexCount == 4u &&
                    glowBatch.sharedIndexCount == 6u &&
                    glowBatch.instances.size() == 1u,
                "Glow billboard growl passes should render with one shared centered quad instance.",
                outFail)) {
        return false;
    }
    if (!expect(glowBatch.geometryCacheKey == "__growl_geom_centered_quad_v1__",
                "Glow billboard growl passes should use the centered shared quad geometry.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(glowBatch.instances.front().modelMatrix[12] - ring.pos.x) <= 0.0001f &&
                    std::abs(glowBatch.instances.front().modelMatrix[13] - ring.pos.y) <= 0.0001f &&
                    std::abs(glowBatch.instances.front().modelMatrix[14] - ring.pos.z) <= 0.0001f,
                "Glow billboard growl passes should spawn at the active ring center by default.",
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

    const auto lineTev = vfx::runtime::growl::resolveTevState(snapshot.config, linePass);
    std::vector<Batch> lineBatches;
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

    GrowlWaveVFX::Config::DrawPass sequencedLinePass = linePass;
    sequencedLinePass.id = "growl_test_line_sequence";
    sequencedLinePass.sequenceCount = 3;
    sequencedLinePass.sequenceStep = 0.10f;
    sequencedLinePass.sequenceLife = 0.80f;

    GrowlWaveVFX::RenderSnapshot lineSequenceSnapshot = snapshot;
    lineSequenceSnapshot.rings.front().ageSec = 0.50f;
    std::vector<Batch> sequencedLineBatches;
    const bool sequencedLineAppended =
        appendPassBatch(sequencedLineBatches,
                        lineSequenceSnapshot,
                        sequencedLinePass,
                        lineTev,
                        &mesh,
                        tex,
                        glm::vec3(0.0f, 1.0f, 4.0f));

    if (!expect(sequencedLineAppended && sequencedLineBatches.size() == 1u,
                "Growl line batching should support staggered cone echoes.",
                outFail)) {
        return false;
    }

    const auto& sequencedLineBatch = sequencedLineBatches.front();
    if (!expect(sequencedLineBatch.instances.size() ==
                    sequencedLinePass.directionsLocal.size() * 3u,
                "Sequenced growl line batching should emit one instance per direction for each visible cone echo.",
                outFail)) {
        return false;
    }
    const glm::vec3 lineSequenceOrigin = lineSequenceSnapshot.rings.front().pos;
    const auto seq0Pos = glm::vec3(sequencedLineBatch.instances[0].modelMatrix[12],
                                   sequencedLineBatch.instances[0].modelMatrix[13],
                                   sequencedLineBatch.instances[0].modelMatrix[14]);
    const auto seq1Pos = glm::vec3(sequencedLineBatch.instances[1].modelMatrix[12],
                                   sequencedLineBatch.instances[1].modelMatrix[13],
                                   sequencedLineBatch.instances[1].modelMatrix[14]);
    const auto seq2Pos = glm::vec3(sequencedLineBatch.instances[2].modelMatrix[12],
                                   sequencedLineBatch.instances[2].modelMatrix[13],
                                   sequencedLineBatch.instances[2].modelMatrix[14]);
    if (!expect(glm::distance(seq0Pos, lineSequenceOrigin) >
                    glm::distance(seq1Pos, lineSequenceOrigin) &&
                    glm::distance(seq1Pos, lineSequenceOrigin) >
                    glm::distance(seq2Pos, lineSequenceOrigin),
                "Sequenced growl line batching should keep older cone echoes farther from the emitter than newer ones.",
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

