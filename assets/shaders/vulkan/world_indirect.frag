#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "world_indirect_state.glsl"

layout(set = 0, binding = 0) uniform sampler2D baseColorTextures[PAC_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 1) uniform sampler2D normalTextures[PAC_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTextures[PAC_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 3) uniform sampler2D occlusionTextures[PAC_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 4) uniform sampler2D emissiveTextures[PAC_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 0, binding = 5) uniform sampler2D environmentTextures[PAC_VULKAN_MAX_INDEXED_WORLD_MATERIALS];
layout(set = 1, binding = 0) uniform WorldViewState {
    vec4 cameraPosition;
    vec4 cameraForward;
    vec4 cameraTarget;
} worldView;

layout(location = 0) in vec2 vertexUv;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec3 vertexNormal;
layout(location = 3) in vec4 vertexTangent;
layout(location = 4) in vec3 worldPosition;
layout(location = 5) in vec3 vertexGenerated;
layout(location = 6) flat in uint drawStateIndex;
#if defined(PAC_VULKAN_DUAL_SOURCE_BLEND)
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outBlendAlpha;
#else
layout(location = 0) out vec4 outColor;
#endif

#include "world_material.glsl"
#include "world_tail_fire.glsl"

void writeWorldColor(vec4 color) {
#if defined(PAC_VULKAN_DUAL_SOURCE_BLEND)
    float blendAlpha = clamp(color.a, 0.0, 1.0);
    float quantizedAlpha = floor(blendAlpha * 63.0 + 0.5) / 63.0;
    outColor = vec4(color.rgb, quantizedAlpha);
    outBlendAlpha = vec4(0.0, 0.0, 0.0, blendAlpha);
#else
    outColor = color;
#endif
}

void main() {
    WorldIndirectDrawState drawState = worldIndirectDraws.states[drawStateIndex];
    uint materialIndex = drawState.drawParams.x;
    float alphaMode = drawState.materialParams.x;
    float alphaCutoff = drawState.materialParams.y;
    float materialMode = drawState.materialParams.w;
    TailFireMaterialState tailFireMaterial = TailFireMaterialState(
        drawState.specializedTimingFlagsAtlas,
        drawState.specializedRect0,
        drawState.specializedRect1,
        drawState.specializedFlipbook0,
        drawState.specializedFlipbook1);

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (gl_FrontFacing) discard;
        writeWorldColor(vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    if (materialMode > 0.5 && materialMode < 1.5) {
        writeWorldColor(evaluateTailFire(
            baseColorTextures[nonuniformEXT(materialIndex)],
            tailFireMaterial));
        return;
    }

    vec4 sampled = texture(
        baseColorTextures[nonuniformEXT(materialIndex)],
        vertexUv);
    vec3 linearColor = clamp(sampled.rgb, 0.0, 1.0) * clamp(vertexColor.rgb, 0.0, 1.0);
    float alpha = clamp(vertexColor.a * sampled.a, 0.0, 1.0);

    float alphaWindowMin = clamp(drawState.shadingParams.x, 0.0, 1.0);
    float alphaWindowMax = clamp(drawState.shadingParams.y, 0.0, 1.0);
    if ((alphaWindowMax < 1.0 || alphaWindowMin > 0.0) &&
        (alpha < alphaWindowMin || alpha >= alphaWindowMax)) {
        discard;
    }
    if (alphaMode < 0.5) {
        alpha = clamp(vertexColor.a, 0.0, 1.0);
    } else if (alphaMode < 1.5) {
        if (alpha < alphaCutoff) discard;
        alpha = clamp(vertexColor.a, 0.0, 1.0);
    }

    if (materialMode >= 1.5 && materialMode < 2.5) {
        linearColor = evaluateWorldMaterial(
            linearColor,
            vertexUv,
            worldPosition,
            vertexNormal,
            vertexTangent,
            worldView.cameraPosition.xyz,
            worldView.cameraForward.xyz,
            worldView.cameraTarget.xyz,
            normalTextures[nonuniformEXT(materialIndex)],
            metallicRoughnessTextures[nonuniformEXT(materialIndex)],
            occlusionTextures[nonuniformEXT(materialIndex)],
            emissiveTextures[nonuniformEXT(materialIndex)],
            environmentTextures[nonuniformEXT(materialIndex)],
            drawState.pbrFactors,
            drawState.emissiveAndCamera.rgb);
    }

    const float toneMappingExposure = 1.15;
    vec3 mapped = tonemapACESFilmic(max(linearColor, vec3(0.0)), toneMappingExposure);
    writeWorldColor(vec4(linearToSrgb(mapped), alpha));
}
