// HealthBarRenderer.h

#pragma once
#include <vector>
#include "../../game/ui/HealthBarData.h"
#include "../utils/Shader.h"

class HealthBarRenderer {
public:
    HealthBarRenderer() = default;  // No shader initialization here
    void init();  // New initialization method
    void render(const std::vector<HealthBarData>& healthBars);

private:
    Shader* shader = nullptr;
    void renderQuad();
};