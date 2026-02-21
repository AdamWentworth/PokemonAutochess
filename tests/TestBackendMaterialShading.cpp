#include "game/runtime/BackendMaterialShading.h"

#include <cmath>
#include <string>

namespace {

bool approx(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_backend_material_shading_contract(std::string& outFail) {
    using game::runtime::backend_material::AlphaMode;
    using game::runtime::backend_material::alphaModeFromByte;
    using game::runtime::backend_material::blendBaseAndTexture;
    using game::runtime::backend_material::modulateBaseAndTexture;
    using game::runtime::backend_material::opacityFromAlphaMode;

    if (alphaModeFromByte(0u) != AlphaMode::Opaque ||
        alphaModeFromByte(1u) != AlphaMode::Mask ||
        alphaModeFromByte(2u) != AlphaMode::Blend ||
        alphaModeFromByte(77u) != AlphaMode::Opaque) {
        outFail = "alphaModeFromByte mapping mismatch";
        return false;
    }

    if (!approx(opacityFromAlphaMode(AlphaMode::Opaque, 0.0f, 0.5f), 1.0f)) {
        outFail = "opaque mode should force full opacity";
        return false;
    }
    if (!approx(opacityFromAlphaMode(AlphaMode::Mask, 0.4f, 0.5f), 0.0f) ||
        !approx(opacityFromAlphaMode(AlphaMode::Mask, 0.6f, 0.5f), 1.0f)) {
        outFail = "mask mode cutoff behavior mismatch";
        return false;
    }
    if (!approx(opacityFromAlphaMode(AlphaMode::Blend, 0.25f, 0.5f), 0.25f)) {
        outFail = "blend mode should preserve source alpha";
        return false;
    }

    {
        const glm::vec3 base(0.2f, 0.3f, 0.4f);
        const glm::vec3 tex(0.8f, 0.6f, 0.1f);
        const glm::vec3 mixed = blendBaseAndTexture(base, tex, 0.0f, 0.35f);
        if (!(mixed.r > base.r && mixed.g > base.g && mixed.b < base.b)) {
            outFail = "blendBaseAndTexture should pull toward texture color with minimum blend";
            return false;
        }
    }

    {
        const glm::vec3 base(0.5f, 0.25f, 1.0f);
        const glm::vec3 tex(0.4f, 0.8f, 0.5f);
        const glm::vec3 modulated = modulateBaseAndTexture(base, tex);
        if (!approx(modulated.r, 0.2f) ||
            !approx(modulated.g, 0.2f) ||
            !approx(modulated.b, 0.5f)) {
            outFail = "modulateBaseAndTexture should multiply channels";
            return false;
        }
    }

    return true;
}
