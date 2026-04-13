#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "vfx/effects/shared/SharedAuthoredBatchVFX.h"

class Camera3D;

class ScratchGlowVFX {
public:
    using Config = SharedAuthoredBatchVFX::Config;
    using RenderSnapshot = SharedAuthoredBatchVFX::RenderSnapshot;

    ScratchGlowVFX();
    ~ScratchGlowVFX();

    static Config makeDefaultConfig();
    static Config makeGameplayConfig();

    void setConfig(const Config& config);
    const Config& getConfig() const;

    void update(float dt);
    void render(const Camera3D& camera);
    bool buildRenderSnapshot(RenderSnapshot& out) const;
    std::uint32_t activeGlowCount() const;

    void emitAt(const glm::vec3& worldPos,
                const glm::vec3& forwardDir,
                const glm::mat4* viewMatrix = nullptr);

    const SharedAuthoredBatchVFX& sharedGlow() const { return effect_; }

private:
    SharedAuthoredBatchVFX effect_;
};
