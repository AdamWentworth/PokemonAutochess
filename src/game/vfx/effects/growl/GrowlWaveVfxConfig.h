#pragma once

#include "game/vfx/effects/growl/GrowlWaveVFX.h"

namespace vfx::growl_wave_config {

inline GrowlWaveVFX::Config makeSourceAlignedConfig() {
    GrowlWaveVFX::Config config;
    config.vertShaderPath = "assets/shaders/vfx/moves/growl/growl_ring_shared.vert";
    config.fragShaderPath = "assets/shaders/vfx/moves/growl/growl_ring_shared.frag";
    using DrawPass = GrowlWaveVFX::Config::DrawPass;
    config.drawPasses = [] {
        std::vector<DrawPass> passes;
        passes.emplace_back();
        passes[0].forwardOffset = 0.12f;

        DrawPass p;
        p.id = "growl_eid_1085";
        p.eid = 1085;
        p.meshPath = "assets/meshes/growl_1085_mesh.glb";
        p.texturePath = "assets/textures/moves/growl/Texture3921.png";
        p.tintColor = glm::vec3(0.98f, 0.40f, 0.17f);
        p.useAlphaMaskForColor = true;
        p.scaleMul = 1.04f;
        p.alphaMul = 0.90f;
        p.forwardOffset = 0.06f;
        p.enabled = true;
        passes.push_back(p);

        DrawPass p2;
        p2.id = "growl_eid_1092";
        p2.eid = 1092;
        p2.meshPath = "assets/meshes/growl_1092_mesh.glb";
        p2.texturePath = "assets/textures/moves/growl/Texture3921.png";
        p2.tintColor = glm::vec3(0.98f, 0.40f, 0.17f);
        p2.useAlphaMaskForColor = true;
        p2.scaleMul = 0.98f;
        p2.alphaMul = 0.88f;
        p2.forwardOffset = 0.00f;
        p2.enabled = true;
        passes.push_back(p2);
        return passes;
    }();
    config.drawPasses[0].id = "growl_eid_1076";
    config.drawPasses[0].eid = 1076;
    config.drawPasses[0].meshPath = "assets/meshes/growl_1076_mesh.glb";
    config.drawPasses[0].texturePath = "assets/textures/moves/growl/Texture3918.png";
    // The Growl FSYS control block's strongest global cue is a 0.94-like persistence
    // window plus a 10-degree angular term. Keep the spawn offsets neutral, but make the
    // shared lifetime deterministic so authored pass timing is no longer riding on the
    // generic randomized shared-authored defaults.
    config.spawnForwardOffset = 0.0f;
    config.spawnHeightOffset = 0.0f;
    config.drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
    config.ringMinLifeSec = 0.94f;
    config.ringMaxLifeSec = 0.94f;
    config.fadeStart = 0.94f;
    return config;
}

} // namespace vfx::growl_wave_config
