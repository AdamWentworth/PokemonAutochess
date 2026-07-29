#version 450
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 3) uniform sampler2D occlusionTexture;
layout(set = 0, binding = 4) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 5) uniform sampler2D environmentTexture;
layout(set = 0, binding = 6) uniform sampler2D lightProjectionTexture;
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

vec2 lgpeRoute1CloudTextureUv(vec3 position);
float evaluateLgpeRoute1ProjectedCloud();

vec3 applyLgpeGroundCliffSharedLighting(vec3 surface) {
    // shadowtable02_t is uniformly opaque white for both Route 1 materials.
    // With projectedShadow bounded at one, the recovered shared-light
    // equation reduces to the projected cloud sample.
    const vec3 shadowColor = vec3(0.235, 0.361, 0.391);
    float light = clamp(evaluateLgpeRoute1ProjectedCloud(), 0.0, 1.0);
    return mix(shadowColor, vec3(1.0), light) * surface;
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
        sampleLgpeGroundTexture(environmentTexture, blendUv).r,
        0.0,
        1.0);
    vec4 grassMask =
        sampleLgpeGroundTexture(emissiveTexture, uv2);
    vec3 ground = mix(ground01.rgb, ground02.rgb, blend);
    vec3 grass = mix(grass02.rgb, grass01.rgb, blend);
    vec3 surface = mix(ground, grass, clamp(grassMask.a, 0.0, 1.0));
    vec3 sourceSurface =
        grassMask.rgb * vertexColor.rgb * surface +
        max(pushData.emissiveAndCamera.rgb, vec3(0.0)) *
            (1.0 - clamp(vertexColor.a, 0.0, 1.0));
    return applyLgpeGroundCliffSharedLighting(sourceSurface);
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
    vec3 sourceSurface = borderTex.rgb * vertexColor.rgb * surface;
    return applyLgpeGroundCliffSharedLighting(sourceSurface);
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

vec4 evaluateLgpeFieldTree02Surface(bool useProjectedCloud) {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    if (texture01.a <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    vec3 texture02 = texture(normalTexture, uv0, 0.0).rgb;
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    vec2 toonUv = vec2(toonCoordinate, 1.0 - toonCoordinate);
    float toon = clamp(
        texture(occlusionTexture, toonUv, 0.0).r,
        0.0,
        1.0);
    float lightToon = clamp(
        texture(emissiveTexture, toonUv, 0.0).r,
        0.0,
        1.0);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float edge = clamp(1.0 - dot(normal, viewDirection), 0.0, 1.0);
    float rimMin = worldSpecializedMaterial.timingFlagsAtlas.x;
    float rimMax = worldSpecializedMaterial.timingFlagsAtlas.y;
    float rimStrength = worldSpecializedMaterial.timingFlagsAtlas.z;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp((edge - rimMin) / rimSpan, 0.0, 1.0) * rimStrength
        : 0.0;
    float directional = clamp(edge * (5.0 / 3.0), 0.0, 1.0);
    vec3 greenColor = pushData.pbrFactors.xyz;
    vec3 shadowColor = pushData.emissiveAndCamera.rgb;
    vec3 rimColor =
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x,
            worldSpecializedMaterial.rect0.y);
    vec3 directionalLightColor =
        vec3(
            worldSpecializedMaterial.rect0.z,
            worldSpecializedMaterial.rect0.w,
            worldSpecializedMaterial.rect1.x);
    vec3 rimColor02 = worldSpecializedMaterial.rect1.yzw;
    vec3 secondary =
        rim * rimColor +
        (1.0 - directional) * rimColor02 +
        lightToon * directionalLightColor;
    vec3 surface = texture01.rgb + texture02 * secondary;
    vec3 authored = surface * vertexColor.rgb;
    vec3 tinted =
        mix(greenColor, authored, clamp(vertexColor.a, 0.0, 1.0));
    // Source shared ten-tap projected-depth PCF remains held at one. Only
    // pasted__pasted__tree15 samples the recovered stationary LightProjMap.
    float light = useProjectedCloud
        ? min(toon, evaluateLgpeRoute1ProjectedCloud())
        : toon;
    vec3 lighting = mix(
        shadowColor,
        vec3(1.0),
        light);
    return vec4(lighting * tinted, texture01.a);
}

vec4 evaluateLgpeFieldGrassSurface(bool withRim) {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 =
        vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    bool floorFoliageCard =
        !withRim && vertexSourceUv2.x < -2048.0;
    vec3 textureMap01 =
        texture(
            baseColorTexture,
            uv0,
            sourceMipBias).rgb;
    vec3 textureMap02 =
        texture(normalTexture, uv0, sourceMipBias).rgb;
    vec4 greenHikari =
        texture(
            metallicRoughnessTexture,
            floorFoliageCard ? vertexSourceUv1 : uv1,
            sourceMipBias);
    if (floorFoliageCard) {
        if (greenHikari.r >=
            clamp(pushData.materialParams.y, 0.0, 1.0)) {
            discard;
        }
    } else if (greenHikari.a <=
               clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    float greenBlend = clamp(
        texture(
            occlusionTexture,
            blendUv,
            sourceMipBias).r,
        0.0,
        1.0);
    float highlight = clamp(
        texture(
            emissiveTexture,
            uv1,
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            environmentTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 sourceColor = pushData.pbrFactors.xyz;
    vec3 decoration =
        mix(textureMap02, textureMap01, greenBlend) +
        sourceColor * (1.0 - highlight);
    vec3 authoredColor = vertexColor.rgb;
    if (!withRim && sourceMipBias > -1.0 &&
        normalize(vertexNormal).y > 0.9) {
        authoredColor =
            vec3(0.180392161, 0.482352942, 0.431372553);
    }
    vec3 surface =
        decoration * greenHikari.rgb * authoredColor;
    if (withRim) {
        vec3 viewDirection =
            normalize(worldView.cameraPosition.xyz - worldPosition);
        float edge =
            clamp(1.0 - dot(normal, viewDirection), 0.0, 1.0);
        float rimMin =
            worldSpecializedMaterial.timingFlagsAtlas.x;
        float rimMax =
            worldSpecializedMaterial.timingFlagsAtlas.y;
        float rimStrength =
            worldSpecializedMaterial.timingFlagsAtlas.z;
        float rimSpan = max(rimMax, rimMin) - rimMin;
        float rim = rimSpan > 0.0
            ? clamp((edge - rimMin) / rimSpan, 0.0, 1.0) *
                  rimStrength
            : 0.0;
        vec3 rimColor =
            vec3(
                worldSpecializedMaterial.timingFlagsAtlas.w,
                worldSpecializedMaterial.rect0.x,
                worldSpecializedMaterial.rect0.y);
        surface += rimColor * rim;
    }
    vec3 shadowColor = pushData.emissiveAndCamera.rgb;
    vec3 result =
        mix(
            shadowColor,
            vec3(1.0),
            min(toon, evaluateLgpeRoute1ProjectedCloud())) *
        surface;
    float alpha = greenHikari.a;
    if (!withRim) {
        vec3 onGameColor =
            worldSpecializedMaterial.timingFlagsAtlas.xyz;
        float onGameValue = clamp(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            0.0,
            1.0);
        result *= mix(vec3(1.0), onGameColor, onGameValue);
        alpha *= clamp(
            worldSpecializedMaterial.rect0.x,
            0.0,
            1.0);
    }
    return vec4(result, alpha);
}

vec2 lgpeRoute1CloudTextureUv(vec3 position) {
    const vec3 projectionU =
        vec3(-0.00010391304269433, 0.0, -0.000276669561862946);
    const vec3 projectionV =
        vec3(
            -0.000223165191709995,
            -0.000349375866353512,
            0.0000838175788521767);
    float sourceU =
        dot(position, projectionU) + 0.695972776542572;
    float sourceV =
        dot(position, projectionV) + 0.692474711333548;
    return vec2(sourceU, 1.0 - sourceV);
}

float evaluateLgpeRoute1ProjectedCloud() {
    return clamp(
        texture(
            lightProjectionTexture,
            lgpeRoute1CloudTextureUv(worldPosition),
            0.0).r,
        0.0,
        1.0);
}

vec4 evaluateLgpeFieldGrassShader04Surface() {
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 = vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec4 texture01 = texture(baseColorTexture, uv0, 0.0);
    vec4 texture02 = texture(normalTexture, uv1, 0.0);
    float texture03 = clamp(
        texture(metallicRoughnessTexture, uv1, 0.0).r,
        0.0,
        1.0);
    vec4 base = mix(texture01, texture02, texture03);
    float alpha =
        base.a * vertexColor.a *
        clamp(worldSpecializedMaterial.timingFlagsAtlas.x, 0.0, 1.0) *
        clamp(worldSpecializedMaterial.timingFlagsAtlas.z, 0.0, 1.0);
    if (alpha <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
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
    float projectedCloud = evaluateLgpeRoute1ProjectedCloud();
    vec3 onGameColor =
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x,
            worldSpecializedMaterial.rect0.y);
    vec3 surface =
        base.rgb * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.y,
                0.0,
                1.0));
    vec3 lighting =
        mix(
            pushData.pbrFactors.xyz,
            vec3(1.0),
            min(toon, projectedCloud));
    return vec4(lighting * surface, alpha);
}

vec4 evaluateLgpeFieldGrassShader05Surface() {
    float scrollU = worldSpecializedMaterial.rect0.y;
    float scrollV = worldSpecializedMaterial.rect0.z;
    vec2 maskUv =
        vec2(vertexUv.x + scrollU, 1.0 - (vertexUv.y + scrollV));
    vec2 uv1Primary =
        vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 uv1Secondary =
        vec2(vertexSourceUv1.x + 1.0, 0.5 - vertexSourceUv1.y);
    float lightLine =
        clamp(texture(normalTexture, maskUv, 0.0).r, 0.0, 1.0);
    vec4 alpha01Primary =
        texture(baseColorTexture, uv1Primary, 0.0);
    vec4 alpha01Secondary =
        texture(baseColorTexture, uv1Secondary, 0.0);
    vec4 base =
        mix(alpha01Primary, alpha01Secondary, lightLine);
    float alpha =
        base.a * vertexColor.a *
        clamp(worldSpecializedMaterial.timingFlagsAtlas.y, 0.0, 1.0);
    if (alpha <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    float greenBlend =
        clamp(texture(emissiveTexture, blendUv, 0.0).r, 0.0, 1.0);
    vec3 textureMap01 =
        texture(metallicRoughnessTexture, uv0, 0.0).rgb;
    vec3 textureMap02 = texture(occlusionTexture, uv0, 0.0).rgb;
    vec3 decoration = mix(textureMap02, textureMap01, greenBlend);
    float projectedCloud = evaluateLgpeRoute1ProjectedCloud();
    vec3 onGameColor =
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.z,
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x);
    vec3 surface =
        (base.rgb + decoration) * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.x,
                0.0,
                1.0));
    vec3 lighting =
        mix(pushData.pbrFactors.xyz, vec3(1.0), projectedCloud);
    return vec4(lighting * surface, alpha);
}

vec4 evaluateLgpeRoadstoneOverlaySurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec4 texture01 =
        texture(baseColorTexture, uv0, sourceMipBias);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 onGameColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    float alpha =
        texture01.a * vertexColor.a *
        clamp(worldSpecializedMaterial.rect0.y, 0.0, 1.0) *
        clamp(worldSpecializedMaterial.rect0.x, 0.0, 1.0);
    vec3 surface =
        texture01.rgb * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.w,
                0.0,
                1.0));
    vec3 lighting = mix(
        pushData.emissiveAndCamera.rgb,
        vec3(1.0),
        min(toon, evaluateLgpeRoute1ProjectedCloud()));
    return vec4(lighting * surface * alpha, alpha);
}

vec4 evaluateLgpeRockMaskOverlaySurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 =
        vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    vec3 textureMap01 =
        texture(baseColorTexture, uv0, sourceMipBias).rgb;
    vec3 textureMap02 =
        texture(normalTexture, uv0, sourceMipBias).rgb;
    vec4 greenHikari =
        texture(
            metallicRoughnessTexture,
            uv1,
            sourceMipBias);
    float greenBlend = clamp(
        texture(
            occlusionTexture,
            blendUv,
            sourceMipBias).r,
        0.0,
        1.0);
    float highlight = clamp(
        texture(
            emissiveTexture,
            uv1,
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            environmentTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 sourceColor = pushData.pbrFactors.xyz;
    vec3 decoration =
        mix(textureMap02, textureMap01, greenBlend) +
        sourceColor * (1.0 - highlight);
    vec3 onGameColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    float alpha =
        greenHikari.a *
        clamp(worldSpecializedMaterial.rect0.x, 0.0, 1.0);
    vec3 surface =
        decoration * greenHikari.rgb * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.w,
                0.0,
                1.0));
    vec3 lighting = mix(
        pushData.emissiveAndCamera.rgb,
        vec3(1.0),
        min(toon, evaluateLgpeRoute1ProjectedCloud()));
    return vec4(lighting * surface * alpha, alpha);
}

float evaluateLgpeFieldRockLightToon(float toonCoordinate) {
    const float sourceValues[54] = float[54](
        1.0, 3.0, 5.0, 7.0, 10.0, 13.0, 16.0, 19.0,
        22.0, 26.0, 30.0, 34.0, 38.0, 43.0, 47.0, 53.0,
        58.0, 63.0, 68.0, 73.0, 80.0, 85.0, 91.0, 97.0,
        103.0, 110.0, 116.0, 122.0, 129.0, 135.0, 142.0,
        148.0, 155.0, 161.0, 168.0, 174.0, 181.0, 188.0,
        193.0, 200.0, 207.0, 211.0, 218.0, 223.0, 227.0,
        232.0, 236.0, 239.0, 243.0, 247.0, 249.0, 253.0,
        255.0, 255.0);
    float sourceTexel =
        clamp(toonCoordinate, 0.0, 1.0) * 512.0 - 0.5;
    int lower = int(floor(sourceTexel));
    int upper = lower + 1;
    float lowerValue = lower < 458
        ? 0.0
        : (lower >= 512 ? 255.0 : sourceValues[lower - 458]);
    float upperValue = upper < 458
        ? 0.0
        : (upper >= 512 ? 255.0 : sourceValues[upper - 458]);
    return mix(lowerValue, upperValue, fract(sourceTexel)) / 255.0;
}

vec4 evaluateLgpeFieldFlowerSurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec4 texture01 =
        texture(baseColorTexture, uv0, sourceMipBias);
    float alpha =
        texture01.a * vertexColor.a *
        clamp(worldSpecializedMaterial.rect0.y, 0.0, 1.0) *
        clamp(worldSpecializedMaterial.rect0.x, 0.0, 1.0);
    if (alpha <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }

    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float toon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 onGameColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    vec3 surface =
        texture01.rgb * vertexColor.rgb *
        mix(
            vec3(1.0),
            onGameColor,
            clamp(
                worldSpecializedMaterial.timingFlagsAtlas.w,
                0.0,
                1.0));
    vec3 lighting = mix(
        pushData.emissiveAndCamera.rgb,
        vec3(1.0),
        min(toon, evaluateLgpeRoute1ProjectedCloud()));
    return vec4(lighting * surface, alpha);
}

vec4 evaluateLgpeFieldRockSurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vec2(vertexUv.x, 1.0 - vertexUv.y);
    vec2 uv1 =
        vec2(vertexSourceUv1.x, 1.0 - vertexSourceUv1.y);
    vec2 uv2 =
        vec2(vertexSourceUv2.x, 1.0 - vertexSourceUv2.y);
    vec2 blendUv =
        vec2(vertexUv.x * 0.3, 1.0 - vertexUv.y * 0.3);
    vec4 rockTexture =
        texture(baseColorTexture, uv1, sourceMipBias);
    vec3 groundTexture02 =
        texture(normalTexture, uv0, sourceMipBias).rgb;
    vec3 groundTexture01 =
        texture(
            metallicRoughnessTexture,
            uv0,
            sourceMipBias).rgb;
    float blend = clamp(
        texture(
            occlusionTexture,
            blendUv,
            sourceMipBias).r,
        0.0,
        1.0);
    vec4 borderTexture =
        texture(emissiveTexture, uv2, sourceMipBias);

    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    float shadowToon = clamp(
        texture(
            environmentTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    float lightToon =
        evaluateLgpeFieldRockLightToon(toonCoordinate);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin = pushData.pbrFactors.w;
    float rimMax =
        worldSpecializedMaterial.timingFlagsAtlas.x;
    float rimStrength =
        worldSpecializedMaterial.timingFlagsAtlas.y;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              ((1.0 - dot(normal, viewDirection)) - rimMin) /
                  rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    vec3 rock =
        rockTexture.rgb +
        pushData.emissiveAndCamera.rgb * lightToon +
        pushData.pbrFactors.xyz * rim * rockTexture.a;
    vec3 ground =
        mix(groundTexture02, groundTexture01, blend);
    vec3 surface =
        mix(rock, ground, clamp(borderTexture.a, 0.0, 1.0));
    vec3 shadowColor =
        vec3(
            worldSpecializedMaterial.timingFlagsAtlas.z,
            worldSpecializedMaterial.timingFlagsAtlas.w,
            worldSpecializedMaterial.rect0.x);
    vec3 onGameColor =
        worldSpecializedMaterial.rect0.yzw;
    vec3 lighting = mix(
        shadowColor,
        vec3(1.0),
        min(shadowToon, evaluateLgpeRoute1ProjectedCloud()));
    return vec4(
        lighting * borderTexture.rgb * vertexColor.rgb * surface *
            mix(
                vec3(1.0),
                onGameColor,
                clamp(worldSpecializedMaterial.rect1.x, 0.0, 1.0)),
        clamp(worldSpecializedMaterial.rect1.y, 0.0, 1.0));
}

vec4 evaluateLgpeFieldSignSurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    // Canonical BNTX RGBA rows are top-down. The source program's 1-V
    // convention is therefore already represented by the decode.
    vec2 uv0 = vertexUv;
    vec4 texture01 =
        texture(baseColorTexture, uv0, sourceMipBias);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float normalDotLight = dot(normal, -sourceSunRay);
    float toonCoordinate = normalDotLight * 0.5 + 0.5;
    vec2 toonUv =
        vec2(toonCoordinate, 1.0 - toonCoordinate);
    float shadowToon = clamp(
        texture(
            occlusionTexture,
            toonUv,
            sourceMipBias).r,
        0.0,
        1.0);
    float lightToon =
        evaluateLgpeFieldRockLightToon(toonCoordinate);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin =
        worldSpecializedMaterial.timingFlagsAtlas.w;
    float rimMax = worldSpecializedMaterial.rect0.x;
    float rimStrength = worldSpecializedMaterial.rect0.y;
    float rimSpan = max(rimMax, rimMin) - rimMin;
    float rim = rimSpan > 0.0
        ? clamp(
              ((1.0 - dot(normal, viewDirection)) - rimMin) /
                  rimSpan,
              0.0,
              1.0) *
              rimStrength
        : 0.0;
    vec3 rimColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    // Source ShadowColor and OnGameColor are exactly white, so the recovered
    // AutoShadow and OnGame mixes are neutral for this Route 1 sign material.
    vec3 surface =
        texture01.rgb +
        pushData.emissiveAndCamera.rgb * lightToon +
        rimColor * rim;
    vec3 shadowColor = pushData.pbrFactors.xyz;
    vec3 lighting = mix(
        shadowColor,
        vec3(1.0),
        min(shadowToon, evaluateLgpeRoute1ProjectedCloud()));
    return vec4(
        lighting * surface * vertexColor.rgb,
        texture01.a * vertexColor.a);
}

vec4 evaluateLgpeFieldEncounterGrassSurface() {
    float sourceMipBias = worldSpecializedMaterial.flipbook0.w;
    vec2 uv0 = vertexUv;
    vec4 texture01 =
        texture(baseColorTexture, uv0, sourceMipBias);
    if (texture01.a <= clamp(pushData.materialParams.y, 0.0, 1.0)) {
        discard;
    }
    float rimMask = clamp(
        texture(normalTexture, uv0, sourceMipBias).r,
        0.0,
        1.0);
    vec3 normal = normalize(vertexNormal);
    const vec3 sourceSunRay =
        vec3(0.5533391237, 0.2078260481, -0.8066127300);
    float toonCoordinate =
        dot(normal, -sourceSunRay) * 0.5 + 0.5;
    float shadowToon = clamp(
        texture(
            occlusionTexture,
            vec2(toonCoordinate, 1.0 - toonCoordinate),
            sourceMipBias).r,
        0.0,
        1.0);
    vec3 viewDirection =
        normalize(worldView.cameraPosition.xyz - worldPosition);
    float rimMin =
        worldSpecializedMaterial.timingFlagsAtlas.w;
    float rimMax = worldSpecializedMaterial.rect0.x;
    float rimStrength = worldSpecializedMaterial.rect0.y;
    float rim = smoothstep(
                    rimMin,
                    max(rimMax, rimMin + 1.0e-5),
                    clamp(
                        1.0 - abs(dot(normal, viewDirection)),
                        0.0,
                        1.0)) *
                rimStrength * rimMask;
    vec3 rimColor =
        worldSpecializedMaterial.timingFlagsAtlas.xyz;
    vec3 shadowColor = pushData.pbrFactors.xyz;
    vec3 base = texture01.rgb * vertexColor.rgb;
    vec3 lighting = mix(
        shadowColor,
        vec3(1.0),
        min(
            shadowToon,
            evaluateLgpeRoute1ProjectedCloud()));
    return vec4(
        (base + rimColor * rim) * lighting,
        texture01.a * vertexColor.a);
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
        writeWorldColor(vec4(encodeLgpeFinalColor(groundLinear), 1.0));
        return;
    }
    if (materialMode > 4.5 && materialMode < 5.5) {
        vec3 cliffLinear = evaluateLgpeFieldCliffSurface();
        writeWorldColor(vec4(encodeLgpeFinalColor(cliffLinear), 1.0));
        return;
    }
    if (materialMode > 5.5 && materialMode < 6.5) {
        vec4 treeSurface = evaluateLgpeFieldTree05Surface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(treeSurface.rgb), treeSurface.a));
        return;
    }
    if (materialMode > 6.5 && materialMode < 7.5) {
        vec4 trunkSurface = evaluateLgpeFieldObjectTreeMikiSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(trunkSurface.rgb), trunkSurface.a));
        return;
    }
    if (materialMode > 7.5 && materialMode < 8.5) {
        vec4 treeSurface = evaluateLgpeFieldTree02Surface(false);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(treeSurface.rgb), treeSurface.a));
        return;
    }
    if (materialMode > 8.5 && materialMode < 9.5) {
        vec4 grassSurface = evaluateLgpeFieldGrassSurface(false);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 9.5 && materialMode < 10.5) {
        vec4 grassSurface = evaluateLgpeFieldGrassSurface(true);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 10.5 && materialMode < 11.5) {
        vec4 grassSurface = evaluateLgpeFieldGrassShader04Surface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 11.5 && materialMode < 12.5) {
        vec4 grassSurface = evaluateLgpeFieldGrassShader05Surface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 12.5 && materialMode < 13.5) {
        vec4 overlaySurface = evaluateLgpeRoadstoneOverlaySurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(overlaySurface.rgb), overlaySurface.a));
        return;
    }
    if (materialMode > 13.5 && materialMode < 14.5) {
        vec4 overlaySurface = evaluateLgpeRockMaskOverlaySurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(overlaySurface.rgb), overlaySurface.a));
        return;
    }
    if (materialMode > 14.5 && materialMode < 15.5) {
        vec4 flowerSurface = evaluateLgpeFieldFlowerSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(flowerSurface.rgb), flowerSurface.a));
        return;
    }
    if (materialMode > 15.5 && materialMode < 16.5) {
        vec4 rockSurface = evaluateLgpeFieldRockSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(rockSurface.rgb), rockSurface.a));
        return;
    }
    if (materialMode > 16.5 && materialMode < 17.5) {
        vec4 signSurface = evaluateLgpeFieldSignSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(signSurface.rgb), signSurface.a));
        return;
    }
    if (materialMode > 17.5 && materialMode < 18.5) {
        vec4 grassSurface =
            evaluateLgpeFieldEncounterGrassSurface();
        writeWorldColor(
            vec4(encodeLgpeFinalColor(grassSurface.rgb), grassSurface.a));
        return;
    }
    if (materialMode > 18.5 && materialMode < 19.5) {
        vec4 shrubSurface =
            evaluateLgpeFieldTree02Surface(true);
        writeWorldColor(
            vec4(encodeLgpeFinalColor(shrubSurface.rgb), shrubSurface.a));
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
