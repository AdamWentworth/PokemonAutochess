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
layout(location = 6) in vec2 vertexSourceUv1;
layout(location = 7) in vec2 vertexSourceUv2;
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

vec3 evaluateLgpeFieldCliffSurface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    vec2 uv1 = vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 uv2 = vec2(vertexSourceUv2.x, 1.0 - vertexSourceUv2.y);
    vec4 cliffTex = sampleLgpeGroundTexture(baseColorTexture, uv1);
    vec4 ground02 = sampleLgpeGroundTexture(normalTexture, uv0);
    vec4 ground01 =
        sampleLgpeGroundTexture(metallicRoughnessTexture, uv0);
    float blend = clamp(
        sampleLgpeGroundTexture(occlusionTexture, blendUv).r,
        0.0,
        1.0);
    vec4 borderTex = sampleLgpeGroundTexture(emissiveTexture, uv2);
    vec3 normal = normalize(vertexNormal);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin = pushData.pbrFactors.x;
    float rimMax = pushData.pbrFactors.y;
    float rimStrength = pushData.pbrFactors.z;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              ((1.0 - dot(normal, viewDirection)) - rimMin) / rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    vec3 cliff =
        cliffTex.rgb +
        max(pushData.emissiveAndCamera.rgb, vec3(0.0)) *
            rim * cliffTex.a;
    vec3 grass = mix(ground02.rgb, ground01.rgb, blend);
    vec3 surface =
        mix(cliff, grass, clamp(borderTex.a, 0.0, 1.0));
    return borderTex.rgb * vertexColor.rgb * surface;
}

vec4 evaluateLgpeFieldTree05Surface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 = vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    if (texture01.a <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    vec3 texture02 = texture(normalTexture, uv1, 0.0).rgb;
    float texture03 = texture(metallicRoughnessTexture, uv0, 0.0).r;
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            0.0).r,
        0.0,
        1.0);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin = worldSpecializedMaterial.timingFlagsAtlas.x;
    float rimMax = worldSpecializedMaterial.timingFlagsAtlas.y;
    float rimStrength = worldSpecializedMaterial.timingFlagsAtlas.z;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              ((1.0 - dot(normal, viewDirection)) - rimMin) / rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    float lightGate =
        1.0 -
        clamp(
            (1.0 - normalDotLight) * 12.7408008575,
            0.0,
            1.0);
    vec3 secondaryDirection = mix(
        viewDirection,
        -sourceSunRay,
        worldSpecializedMaterial.flipbook0.w);
    float secondaryMin = worldSpecializedMaterial.flipbook1.z;
    float secondaryMax = worldSpecializedMaterial.flipbook1.w;
    float secondarySpan =
        max(secondaryMax, secondaryMin) - secondaryMin;
    float secondaryCoordinate =
        clamp(1.0 - dot(normal, secondaryDirection), 0.0, 1.0);
    float secondary = secondarySpan > 0.0
        ? clamp(
              (secondaryCoordinate - secondaryMin) / secondarySpan,
              0.0,
              1.0)
        : 0.0;
    vec3 shadowColor = max(pushData.pbrFactors.xyz, vec3(0.0));
    vec3 rimColor = max(
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x,
            worldSpecializedMaterial.rect0.y),
        vec3(0.0));
    vec3 rimColor02 = max(
        vec3(
            worldSpecializedMaterial.rect0.z,
            worldSpecializedMaterial.rect0.w,
            worldSpecializedMaterial.rect1.x),
        vec3(0.0));
    vec3 surface =
        texture01.rgb +
        texture02 * rim * rimColor +
        vec3(0.110647157, 0.3070065, 0.0411512256) *
            (1.0 - secondary) +
        texture03 * lightGate * rimColor02;
    return vec4(mix(shadowColor, vec3(1.0), toon) * surface,
                texture01.a);
}

vec4 evaluateLgpeFieldObjectTreeMikiSurface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 = vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    float highlightAlpha = texture(normalTexture, uv1, 0.0).a;
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            0.0).r,
        0.0,
        1.0);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin = worldSpecializedMaterial.timingFlagsAtlas.x;
    float rimMax = worldSpecializedMaterial.timingFlagsAtlas.y;
    float rimStrength = worldSpecializedMaterial.timingFlagsAtlas.z;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              (clamp(
                   1.0 - dot(normal, viewDirection),
                   0.0,
                   1.0) -
               rimMin) /
                  rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    vec3 shadowColor = max(pushData.pbrFactors.xyz, vec3(0.0));
    vec3 rimColor = max(
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x,
            worldSpecializedMaterial.rect0.y),
        vec3(0.0));
    vec3 lighting = mix(shadowColor, vec3(1.0), toon);
    vec3 surface = texture01.rgb + rimColor * rim * highlightAlpha;
    return vec4(
        lighting * surface * vertexColor.rgb,
        texture01.a * vertexColor.a);
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
    if (materialMode > 4.5 && materialMode < 5.5) {
        vec3 cliffLinear = evaluateLgpeFieldCliffSurface();
        const float cliffExposure = 1.15;
        vec3 cliffMapped =
            tonemapACESFilmic(max(cliffLinear, vec3(0.0)), cliffExposure);
        writeWorldColor(vec4(linearToSrgb(cliffMapped), 1.0));
        return;
    }
    if (materialMode > 5.5 && materialMode < 6.5) {
        vec4 treeSurface = evaluateLgpeFieldTree05Surface();
        const float treeExposure = 1.15;
        vec3 treeMapped =
            tonemapACESFilmic(
                max(treeSurface.rgb, vec3(0.0)),
                treeExposure);
        writeWorldColor(
            vec4(linearToSrgb(treeMapped), treeSurface.a));
        return;
    }
    if (materialMode > 6.5 && materialMode < 7.5) {
        vec4 trunkSurface = evaluateLgpeFieldObjectTreeMikiSurface();
        const float trunkExposure = 1.15;
        vec3 trunkMapped =
            tonemapACESFilmic(
                max(trunkSurface.rgb, vec3(0.0)),
                trunkExposure);
        writeWorldColor(
            vec4(linearToSrgb(trunkMapped), trunkSurface.a));
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
