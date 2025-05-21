// HealthBarRenderer.h

#pragma once
#include <vector>
#include "HealthBarData.h"
#include "../utils/Shader.h"
#include <memory>

class HealthBarRenderer {
public:
    HealthBarRenderer() = default;  // No shader initialization here
    void init();  // New initialization method
    void render(const std::vector<HealthBarData>& healthBars);

private:
    std::unique_ptr<Shader> shader;
    void renderQuad();
};