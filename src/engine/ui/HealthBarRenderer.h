// HealthBarRenderer.h

#pragma once
#include <vector>
#include "HealthBarData.h"
#include "engine/utils/Shader.h"
#include <memory>

class ShaderCache;

class HealthBarRenderer {
public:
    HealthBarRenderer() = default;  // No shader initialization here
    void init();
    void init(ShaderCache& shaders);
    void render(const std::vector<HealthBarData>& healthBars);

private:
    std::shared_ptr<Shader> shader; 
    void renderQuad();
};