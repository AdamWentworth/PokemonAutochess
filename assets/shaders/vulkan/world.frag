#version 450
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 3) uniform sampler2D occlusionTexture;
layout(set = 0, binding = 4) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 5) uniform sampler2D environmentTexture;
layout(set = 1, binding = 0) uniform WorldViewState {
    vec4 cameraPosition;
    vec4 cameraForward;
    vec4 cameraTarget;
} worldView;
layout(set = 1, binding = 1) uniform WorldSpecializedMaterialState {
    vec4 timingFlagsAtlas;
    vec4 rect0;
    vec4 rect1;
    vec4 flipbook0;
    vec4 flipbook1;
} worldSpecializedMaterial;

layout(push_constant) uniform WorldPushConstants {
    mat4 viewProjection;
    vec4 materialParams;
    vec4 shadingParams;
    vec4 pbrFactors;
    vec4 emissiveAndCamera;
} pushData;

layout(location = 0) in vec2 vertexUv;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec3 vertexNormal;
layout(location = 3) in vec4 vertexTangent;
layout(location = 4) in vec3 worldPosition;
layout(location = 5) in vec3 vertexGenerated;
layout(location = 6) in vec2 vertexSourceUv2;
#if defined(PAC_VULKAN_DUAL_SOURCE_BLEND)
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outBlendAlpha;
#else
layout(location = 0) out vec4 outColor;
#endif

#include "world_material.glsl"
#include "world_tail_fire.glsl"

vec4 sampleLgpeGroundTexture(sampler2D textureSampler, vec2 uv) {
    return texture(textureSampler, uv, -2.0);
}

vec3 evaluateLgpeFieldGroundSurface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 blendUv = vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    vec2 uv2 = vec2(vertexSourceUv2.x, 1.0 - vertexSourceUv2.y);
    vec4 ground01 = sampleLgpeGroundTexture(baseColorTexture, uv0);
    vec4 ground02 = sampleLgpeGroundTexture(normalTexture, uv0);
    vec4 grass02 =
        sampleLgpeGroundTexture(metallicRoughnessTexture, uv0);
    vec4 grass01 = sampleLgpeGroundTexture(occlusionTexture, uv0);
    float blend = clamp(
        sampleLgpeGroundTexture(emissiveTexture, blendUv).r,
        0.0,
        1.0);
    vec4 grassBlend =
        sampleLgpeGroundTexture(environmentTexture, uv2);
    vec3 ground = mix(ground01.rgb, ground02.rgb, blend);
    vec3 grass = mix(grass02.rgb, grass01.rgb, blend);
    vec3 surface = mix(ground, grass, clamp(grassBlend.a, 0.0, 1.0));
    return grassBlend.rgb * vertexColor.rgb * surface +
           max(pushData.emissiveAndCamera.rgb, vec3(0.0)) *
               (1.0 - clamp(vertexColor.a, 0.0, 1.0));
}

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
    float alphaMode = pushData.materialParams.x;
    float alphaCutoff = pushData.materialParams.y;
    float materialMode = pushData.materialParams.w;

    if (materialMode > 2.5 && materialMode < 3.5) {
        if (gl_FrontFacing) discard;
        writeWorldColor(vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    if (materialMode > 0.5 && materialMode < 1.5) {
        TailFireMaterialState tailFireMaterial = TailFireMaterialState(
            worldSpecializedMaterial.timingFlagsAtlas,
            worldSpecializedMaterial.rect0,
            worldSpecializedMaterial.rect1,
            worldSpecializedMaterial.flipbook0,
            worldSpecializedMaterial.flipbook1);
        writeWorldColor(evaluateTailFire(baseColorTexture, tailFireMaterial));
        return;
    }
    if (materialMode > 3.5 && materialMode < 4.5) {
        vec3 groundLinear = evaluateLgpeFieldGroundSurface();
        const float groundExposure = 1.15;
        vec3 groundMapped =
            tonemapACESFilmic(max(groundLinear, vec3(0.0)), groundExposure);
        writeWorldColor(vec4(linearToSrgb(groundMapped), 1.0));
        return;
    }

    vec4 sampled = texture(baseColorTexture, vertexUv);
    vec3 linearColor = clamp(sampled.rgb, 0.0, 1.0) * clamp(vertexColor.rgb, 0.0, 1.0);
    float alpha = clamp(vertexColor.a * sampled.a, 0.0, 1.0);

    float alphaWindowMin = clamp(pushData.shadingParams.x, 0.0, 1.0);
    float alphaWindowMax = clamp(pushData.shadingParams.y, 0.0, 1.0);
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
            normalTexture,
            metallicRoughnessTexture,
            occlusionTexture,
            emissiveTexture,
            environmentTexture,
            pushData.pbrFactors,
            pushData.emissiveAndCamera.rgb);
    }

    const float toneMappingExposure = 1.15;
    vec3 mapped = tonemapACESFilmic(max(linearColor, vec3(0.0)), toneMappingExposure);
    writeWorldColor(vec4(linearToSrgb(mapped), alpha));
}
