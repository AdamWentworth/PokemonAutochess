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
    using game::runtime::backend_material::composeGltfLikeColor;
    using game::runtime::backend_material::linearToSrgb;
    using game::runtime::backend_material::modulateBaseAndTexture;
    using game::runtime::backend_material::opacityFromAlphaMode;
    using game::runtime::backend_material::shadeVertexLitColor;
    using game::runtime::backend_material::srgbToLinear;

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

    {
        const glm::vec3 srgb(0.08f, 0.25f, 0.66f);
        const glm::vec3 roundTrip = linearToSrgb(srgbToLinear(srgb));
        if (!approx(roundTrip.r, srgb.r, 0.0015f) ||
            !approx(roundTrip.g, srgb.g, 0.0015f) ||
            !approx(roundTrip.b, srgb.b, 0.0015f)) {
            outFail = "sRGB linear conversion round-trip drifted too far";
            return false;
        }
    }

    {
        const glm::vec3 base(0.35f, 0.22f, 0.11f);
        const glm::vec3 emissive(0.9f, 0.4f, 0.2f);
        const glm::vec3 noEmi = composeGltfLikeColor(base, emissive, glm::vec3(0.0f));
        const glm::vec3 withEmi = composeGltfLikeColor(base, emissive, glm::vec3(1.0f, 0.8f, 0.5f));
        if (!(withEmi.r >= noEmi.r && withEmi.g >= noEmi.g && withEmi.b >= noEmi.b)) {
            outFail = "composeGltfLikeColor should brighten when emissive contribution increases";
            return false;
        }
    }

    {
        const glm::vec3 base(0.45f, 0.45f, 0.45f);
        const glm::vec3 lightDir = glm::normalize(glm::vec3(0.45f, 0.90f, 0.35f));
        const glm::vec3 viewDir = glm::normalize(glm::vec3(0.0f, 0.5f, 1.0f));
        const glm::vec3 litFacing = shadeVertexLitColor(base, lightDir, lightDir, viewDir, false);
        const glm::vec3 litAway = shadeVertexLitColor(base, -lightDir, lightDir, viewDir, false);
        if (!(litFacing.r > litAway.r && litFacing.g > litAway.g && litFacing.b > litAway.b)) {
            outFail = "shadeVertexLitColor should brighten surfaces facing light";
            return false;
        }
        if (litFacing.r < 0.0f || litFacing.r > 1.0f ||
            litFacing.g < 0.0f || litFacing.g > 1.0f ||
            litFacing.b < 0.0f || litFacing.b > 1.0f) {
            outFail = "shadeVertexLitColor output must remain clamped";
            return false;
        }

        const glm::vec3 litDoubleSided =
            shadeVertexLitColor(base, -lightDir, lightDir, viewDir, true);
        if (!(litDoubleSided.r >= litFacing.r * 0.90f)) {
            outFail = "shadeVertexLitColor backface flip should keep two-sided faces lit";
            return false;
        }
    }

    return true;
}
