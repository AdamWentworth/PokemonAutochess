// src/vfx/effects/shared/SharedAuthoredBatchVFX.h
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/core/Random.h"

class Camera3D;
class Model;
class Shader;

class SharedAuthoredBatchVFX {
public:
    struct Config {
        struct AuthoredStreakSegment {
            glm::vec3 startLocal{0.0f};
            glm::vec3 endLocal{0.0f, 0.0f, 1.0f};
            float alphaMul = 1.0f;
        };

        struct DrawPass {
            std::string id = "growl_eid_1076";
            int eid = 1076;
            std::string renderMode = "mesh";
            std::string meshPath = "assets/meshes/growl_1076_mesh.glb";
            std::string texturePath = "assets/textures/moves/growl/Texture3918.png";
            std::string vertShaderPath;
            std::string fragShaderPath;
            bool overrideBlendMode = false;
            std::uint8_t blendMode = 1u;
            bool textureQuarterRing = false;
            bool cameraFacing = false;
            int quarterCount = 4;
            float quarterStepDeg = 90.0f;
            float quarterStartDeg = 0.0f;
            float billboardSpinTurns = 0.0f;
            float billboardSpinStartDeg = 0.0f;
            glm::vec3 tintColor = glm::vec3(0.93f, 0.28f, 0.14f);
            bool useAlphaMaskForColor = true;
            float scaleMul = 1.0f;
            float alphaMul = 1.0f;
            float forwardOffset = 0.0f;
            float heightOffset = 0.0f;
            float startRadiusMul = 1.0f;
            float timeStartSec = 0.0f;
            float timeEndSec = -1.0f;
            bool timeFadeLocal = false;
            float timeFadeStart = -1.0f;
            float localScaleStartMul = 1.0f;
            float localScaleEndMul = 1.0f;
            float localScaleRampSec = 0.0f;
            int sequenceCount = 1;
            int sequenceIndex = -1;
            float sequenceStep = 0.0f;
            float sequenceLife = 1.0f;
            bool sequenceFadeLocal = false;
            float radiusGrowthMul = 1.0f;
            bool overrideMeshForwardAxis = false;
            glm::vec3 meshForwardAxis = glm::vec3(0.0f, 1.0f, 0.0f);
            bool overrideDirection = false;
            // Local direction in caster-space basis: +X right, +Y up, +Z forward.
            glm::vec3 directionLocal = glm::vec3(0.0f, 0.0f, 1.0f);
            // Optional multi-line fan for line passes.
            std::vector<glm::vec3> directionsLocal;
            std::string authoredSegmentsPath;
            std::vector<AuthoredStreakSegment> authoredSegmentsLocal;
            bool authoredSegmentCenterOrigin = false;
            float authoredSegmentPositionScale = 1.0f;
            float authoredSegmentLengthScale = 1.0f;
            float authoredSegmentTravelMul = 0.0f;
            float authoredSegmentTravelDecayPerFrame = 1.0f;
            float authoredSegmentTravelFrameRate = 30.0f;
            float authoredSegmentLengthDecayPerFrame = 1.0f;
            float authoredSegmentAlphaDecayPerFrame = 1.0f;
            float authoredSegmentMaxVisibleDistance = -1.0f;
            // Optional generated circular direction set when directions_local is omitted.
            int generatedDirectionCount = 0;
            std::string generatedDirectionMode = "circle";
            float generatedDirectionStartDeg = 0.0f;
            float generatedDirectionArcDeg = 360.0f;
            float generatedDirectionForward = 0.0f;
            // Per-emission angular jitter for directions_local (degrees).
            // 0 = disabled (uses authored even spacing as-is).
            float directionSpacingJitterDeg = 0.0f;
            // Random radial spawn distance multiplier for per-direction clumps / streaks.
            float radialDistanceMinMul = 1.0f;
            float radialDistanceMaxMul = 1.0f;
            // Per-line alpha variation range (multiplies alpha_mul), for dynamic line brightness.
            float lineAlphaMin = 1.0f;
            float lineAlphaMax = 1.0f;
            float radiusMul = 1.0f;
            float thicknessMul = 1.0f;
            bool overrideTev = false;
            glm::vec3 tevC0 = glm::vec3(1.0f, 1.0f, 1.0f);
            glm::vec3 tevC1 = glm::vec3(0.0f, 0.0f, 0.0f);
            glm::vec3 tevK0 = glm::vec3(1.0f, 1.0f, 1.0f);
            float tevK1A = 1.0f;
            bool enabled = true;
        };

        float spawnForwardOffset = 0.20f;
        float spawnHeightOffset = 0.40f;

        // Lead ring + trailing rings.
        float ringForwardOffset = 0.03f;
        float ringMinSpeed = 0.30f;
        float ringMaxSpeed = 0.45f;
        float ringMinLifeSec = 0.58f;
        float ringMaxLifeSec = 0.92f;
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
        std::uint8_t blendMode = 1u;

        // Optional manifest for pass routing by EID.
        // Keep move-first naming so Growl config is discoverable under the move name.
        std::string drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
        std::vector<DrawPass> drawPasses = [] {
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

        bool depthTest = true;
        bool depthWrite = false;
    };

    struct RenderRing {
        glm::vec3 pos{0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        float lifeSec = 0.0f;
        float ageSec = 0.0f;
        float startScale = 1.0f;
        float endScale = 1.0f;
        std::uint32_t randomSeed = 0u;
    };

    struct RenderSnapshot {
        Config config{};
        std::vector<Config::DrawPass> drawPasses;
        std::vector<RenderRing> rings;
    };

public:
    SharedAuthoredBatchVFX() = default;
    ~SharedAuthoredBatchVFX();

    void setConfig(const Config& c);
    const Config& getConfig() const { return cfg; }

    void update(float dt);
    void render(const Camera3D& camera);
    bool buildRenderSnapshot(RenderSnapshot& out) const;
    std::uint32_t activeRingCount() const;

    void emitFrom(const glm::vec3& mouthWorldPos,
                  const glm::vec3& forwardDir,
                  const glm::mat4* viewMatrix = nullptr);

private:
    struct DrawPassRuntime {
        Config::DrawPass cfg;
        std::unique_ptr<Model> meshModel;
        std::shared_ptr<Shader> shader;
        unsigned int textureID = 0;
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
    };

    struct RingInstance {
        glm::vec3 pos{0.0f};
        glm::vec3 vel{0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
        float lifeSec = 0.0f;
        float ageSec = 0.0f;
        float startScale = 1.0f;
        float endScale = 1.0f;
        std::uint32_t randomSeed = 0u;
    };

    void ensureConfigured();
    void releaseResources();
    void applyDrawManifestOverrides();
    void ensureQuarterQuadResources();
    void ensureCenteredQuadResources();
    void ensureStreakQuadResources();
    void drawQuarterQuad(const Camera3D& camera, const glm::mat4& world, int locMVP) const;
    void drawCenteredQuad(const Camera3D& camera, const glm::mat4& world, int locMVP) const;
    void drawStreakQuad(const Camera3D& camera, const glm::mat4& world, int locMVP) const;
    float rand01();
    float randRange(float a, float b);
    glm::vec3 safeForwardXZ(const glm::vec3& v) const;
    glm::quat rotationFromToSafe(const glm::vec3& from, const glm::vec3& to) const;

private:
    std::vector<DrawPassRuntime> drawPasses;

    std::vector<RingInstance> rings;
    Config cfg{};
    bool configured = false;
    bool configFailed = false;
    unsigned int quarterQuadVAO = 0;
    unsigned int quarterQuadVBO = 0;
    unsigned int centeredQuadVAO = 0;
    unsigned int centeredQuadVBO = 0;
    unsigned int streakQuadVAO = 0;
    unsigned int streakQuadVBO = 0;

    engine::XorShift32 rng{0xA17F2Du};
};
