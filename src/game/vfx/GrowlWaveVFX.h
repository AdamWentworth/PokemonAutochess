// src/game/vfx/GrowlWaveVFX.h
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/core/Random.h"

class Camera3D;
class Model;
class Shader;

class GrowlWaveVFX {
public:
    struct Config {
        struct DrawPass {
            std::string id = "growl_eid_1076";
            int eid = 1076;
            std::string meshPath = "assets/meshes/growl_1076_mesh.glb";
            std::string texturePath = "assets/textures/moves/growl/Texture3918.png";
            glm::vec3 tintColor = glm::vec3(0.93f, 0.28f, 0.14f);
            bool useAlphaMaskForColor = true;
            float scaleMul = 1.0f;
            float alphaMul = 1.0f;
            bool enabled = true;
        };

        float spawnForwardOffset = 0.20f;
        float spawnHeightOffset = 0.40f;

        // Lead ring + trailing rings.
        float ringForwardOffset = 0.03f;
        float ringMinSpeed = 0.45f;
        float ringMaxSpeed = 0.65f;
        float ringMinLifeSec = 0.50f;
        float ringMaxLifeSec = 0.82f;
        float ringMinSize = 0.14f;
        float ringMaxSize = 0.20f;
        int ringTrailCount = 0;
        float ringTrailSpacingMin = 0.10f;
        float ringTrailSpacingMax = 0.18f;
        float ringTrailLateralJitter = 0.03f;
        float ringLeadSizeMul = 1.28f;
        float ringTrailSizeFalloff = 0.88f;
        float ringTrailLifeFalloff = 0.92f;
        float ringTrailSpeedFalloff = 0.90f;
        float ringScaleGrowth = 1.15f;
        float fadeStart = 0.65f;

        // Local forward axis of authored meshes.
        glm::vec3 meshForwardAxis = glm::vec3(0.0f, 1.0f, 0.0f);

        // Shared shader + TEV-ish constants.
        std::string vertShaderPath = "assets/shaders/vfx/moves/growl/growl_ring_shared.vert";
        std::string fragShaderPath = "assets/shaders/vfx/moves/growl/growl_ring_shared.frag";
        glm::vec3 tevC0 = glm::vec3(1.0f, 1.0f, 1.0f);
        glm::vec3 tevC1 = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 tevK0 = glm::vec3(1.0f, 1.0f, 1.0f);
        float tevK1A = 1.0f;

        // Optional manifest for pass routing by EID.
        std::string drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
        std::vector<DrawPass> drawPasses = {
            DrawPass{},
            DrawPass{
                "growl_eid_1085",
                1085,
                "assets/meshes/growl_1085_mesh.glb",
                "assets/textures/moves/growl/Texture3921.png",
                glm::vec3(0.98f, 0.40f, 0.17f),
                true,
                1.04f,
                0.90f,
                true
            }
        };

        bool depthTest = true;
        bool depthWrite = false;
    };

public:
    GrowlWaveVFX() = default;
    ~GrowlWaveVFX();

    void setConfig(const Config& c);
    const Config& getConfig() const { return cfg; }

    void update(float dt);
    void render(const Camera3D& camera);

    void emitFrom(const glm::vec3& mouthWorldPos,
                  const glm::vec3& forwardDir,
                  const glm::mat4* viewMatrix = nullptr);

private:
    struct DrawPassRuntime {
        Config::DrawPass cfg;
        std::unique_ptr<Model> meshModel;
        unsigned int textureID = 0;
    };

    struct RingInstance {
        glm::vec3 pos{0.0f};
        glm::vec3 vel{0.0f};
        glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
        float lifeSec = 0.0f;
        float ageSec = 0.0f;
        float startScale = 1.0f;
        float endScale = 1.0f;
    };

    void ensureConfigured();
    void releaseResources();
    void applyDrawManifestOverrides();
    float rand01();
    float randRange(float a, float b);
    glm::vec3 safeForwardXZ(const glm::vec3& v) const;
    glm::quat rotationFromToSafe(const glm::vec3& from, const glm::vec3& to) const;

private:
    std::vector<DrawPassRuntime> drawPasses;
    std::shared_ptr<Shader> shader;
    int locMVP = -1;
    int locTexture = -1;
    int locFade = -1;
    int locTevC0 = -1;
    int locTevC1 = -1;
    int locTevK0 = -1;
    int locTevK1A = -1;
    int locTintColor = -1;
    int locUseAlphaMaskForColor = -1;
    int locPassAlphaMul = -1;

    std::vector<RingInstance> rings;
    Config cfg{};
    bool configured = false;
    bool configFailed = false;

    engine::XorShift32 rng{0xA17F2Du};
};
