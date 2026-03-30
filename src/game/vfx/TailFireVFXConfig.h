#pragma once

#include <string>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"

struct TailFireVFXConfig {
    float emitRatePerSec = 65.0f;
    float spawnRadius = 0.010f;

    // Preferred: stable across GLB node reordering.
    std::string tailTipNodeName;

    // Optional exact fire attachment helpers. When both exist, the flipbook
    // follows the authored fire rig instead of approximating from the tail tip.
    std::string fireAnchorBaseNodeName;
    std::string fireAnchorTipNodeName;

    // Legacy fallback (keep for transition).
    int tailTipNodeIndex = 45;

    float tailWorldYOffset = 0.2f;

    // Interpreted as a LOCAL direction in the tail node's frame.
    // Example "0,0,1" means "forward" in the tail bone frame.
    glm::vec3 backDir = glm::vec3(0.0f, 0.0f, 1.0f);

    // How much tail tip linear velocity gets inherited by particles.
    // 0 = none, 1 = fully inherit.
    float inheritVelocity = 0.9f;

    // Exponential smoothing strength for the emission anchor (0 disables).
    // Higher = follows more tightly; lower = smoother/laggier.
    float followSmoothing = 0.0f;

    std::string vertShaderPath = "assets/shaders/vfx/particle.vert";
    std::string fragShaderPath = "assets/shaders/vfx/fire/fire_tail.frag";

    std::string flipbookPath = "assets/textures/fire_flipbook_8x5.png";
    int flipbookCols = 8;
    int flipbookRows = 5;
    int flipbookFrames = 40;
    float flipbookFps = 30.0f;

    std::string flipbook2Path = "assets/textures/fire_flipbook2_8x5.png";
    int flipbook2Cols = 8;
    int flipbook2Rows = 5;
    int flipbook2Frames = 40;
    float flipbook2Fps = 30.0f;
    bool useFlipbook2 = true;

    ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Premultiplied;
    bool depthTest = true;
    bool depthWrite = false;

    glm::vec3 acceleration = glm::vec3(0.0f, 1.2f, 0.0f);
    float dampingBase = 0.07f;

    float pointScale = 900.0f;
    bool useFlipbook = true;
    bool useUnitScaleChain = false;
};
