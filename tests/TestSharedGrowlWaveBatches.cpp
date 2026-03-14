#include <string>
#include <vector>

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

    if (!expect(appended && batches.size() == 2u,
                "appendPassBatch should append one cached quarter-ring batch per quarter for a valid pass/ring/texture.",
                outFail)) {
        return false;
    }

    for (const auto& batch : batches) {
        if (!expect(batch.vertices.empty() && batch.indices.empty(),
                    "Quarter-ring cached growl batches should not rebuild local vertices every frame.",
                    outFail)) {
            return false;
        }
        if (!expect(batch.sharedVertexCount == 4u && batch.sharedIndexCount == 6u,
                    "Quarter-ring cached growl batches should reference the shared unit quad geometry.",
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
        if (!expect(batch.textureCacheKey == "__growl_baked:growl_test_quarter:q:assets/textures/test.png",
                    "Growl batch should carry a stable texture cache key so startup prewarm and runtime rendering reuse the same backend texture.",
                    outFail)) {
            return false;
        }
        if (!expect(batch.geometryCacheKey == "__growl_geom_quarter_unit_v1__",
                    "Quarter-ring growl batches should use the shared geometry cache key.",
                    outFail)) {
            return false;
        }
        if (!expect(batch.sortDepth > 0.0f,
                    "Growl batch should compute a positive sort depth from ring/camera distance.",
                    outFail)) {
            return false;
        }
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

    if (!expect(lineAppended && lineBatches.size() == linePass.directionsLocal.size(),
                "Single-ring growl line passes should append one cached shared-geometry batch per direction.",
                outFail)) {
        return false;
    }

    const std::string expectedLineGeometryKey = "__growl_geom_line_v1__:growl_test_line:assets/meshes/test_line.glb:1000";
    for (const auto& lineBatch : lineBatches) {
        if (!expect(lineBatch.vertices.empty() && lineBatch.indices.empty(),
                    "Cached growl line batches should not rebuild transformed mesh vertices every frame.",
                    outFail)) {
            return false;
        }
        if (!expect(lineBatch.sharedVertexCount == 3u && lineBatch.sharedIndexCount == 3u,
                    "Cached growl line batches should reference the source mesh geometry.",
                    outFail)) {
            return false;
        }
        if (!expect(lineBatch.geometryCacheKey == expectedLineGeometryKey,
                    "Cached growl line batches should use a stable geometry cache key that includes the pass and quantized TEV alpha.",
                    outFail)) {
            return false;
        }
        if (!expect(lineBatch.textureCacheKey == "__growl_white__",
                    "Cached growl line batches should share the white texture cache entry.",
                    outFail)) {
            return false;
        }
        if (!expect(lineBatch.vertexColorMulR > 0.0f &&
                        lineBatch.vertexColorMulG > 0.0f &&
                        lineBatch.vertexColorMulB > 0.0f &&
                        lineBatch.vertexColorMulA > 0.0f,
                    "Cached growl line batches should carry tint and fade in the vertex color multipliers.",
                    outFail)) {
            return false;
        }
    }

    pass.enabled = false;
    if (!expect(!appendPassBatch(batches, snapshot, pass, tev, nullptr, tex, glm::vec3(0.0f)),
                "appendPassBatch should no-op for disabled draw passes.",
                outFail)) {
        return false;
    }

    return true;
}

