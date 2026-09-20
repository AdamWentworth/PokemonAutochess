#include "character_lighting.glsl"
#include "layered_effects.glsl"
__AUTOCHESS_FIELD_DECLARATIONS__
vec3 applyVolumeRimLighting(
    vec3 color,
    vec4 authoredResponse,
    bool useAuthoredShadowColor) {
    vec3 normal = normalize(vertexNormal);
    vec3 cameraForward = safeNormalize(
        worldView.cameraForward.xyz,
        vec3(0.0, -0.6139406, -0.7893522));
    vec3 cameraRight = safeNormalize(
        cross(cameraForward, vec3(0.0, 1.0, 0.0)),
        vec3(1.0, 0.0, 0.0));
    vec3 viewDirection = safeNormalize(
        worldView.cameraPosition.xyz - worldPosition,
        -cameraForward);
    vec3 lightPosition = worldView.cameraPosition.xyz +
        cameraRight * 0.5 - cameraForward * 0.8660254;
    vec3 lightDirection = safeNormalize(
        lightPosition - worldView.cameraTarget.xyz,
        vec3(0.45, 0.86, 0.24));
    float lightFacing = clamp(dot(normal, lightDirection), -1.0, 1.0);
    float viewFacing = clamp(dot(normal, viewDirection), -1.0, 1.0);
    // Z-A IkCharacter: HalfLambertBias=.1, ShadowStrength=.7,
    // RimLightOffset=.2, RimLightContrast=2, RimLightIntensity=.8,
    // BackRimLightIntensity=.01.
    float edge = clamp(1.0 - max(viewFacing, 0.0), 0.0, 1.0);
    float rimDomain = clamp((edge - 0.2) / 0.8, 0.0, 1.0);
    float rim;
    float backRim;
    vec3 diffuseColor;
    if (useAuthoredShadowColor) {
        float wrappedLambert = clamp(lightFacing * 0.5 + 0.5, 0.0, 1.0);
        float biasedLambert = wrappedLambert * wrappedLambert;
        const float shadowBandLow = 0.3465;
        const float shadowBandHigh = 0.3535;
        float shadowAmount = clamp(
            1.0 - (biasedLambert - shadowBandLow) /
                (shadowBandHigh - shadowBandLow),
            0.0,
            1.0);
        diffuseColor = color * mix(
            vec3(1.0),
            authoredResponse.rgb,
            shadowAmount);
        float rimSmooth = rimDomain * rimDomain * (3.0 - 2.0 * rimDomain);
        float rimShape = clamp(rimSmooth * 5.0 - 2.0, 0.0, 1.0);
        const float layeredCharacterRimPresentationScale = 0.25;
        rim = rimShape * authoredResponse.a * layeredCharacterRimPresentationScale;
        float rimMask = clamp(authoredResponse.a / 0.8, 0.0, 1.0);
        backRim = clamp(-viewFacing, 0.0, 1.0) * 0.01 * rimMask *
            layeredCharacterRimPresentationScale;
    } else {
        float halfLambert = clamp(lightFacing * 0.5 + 0.6, 0.0, 1.0);
        float diffuse = mix(1.0, halfLambert, 0.7);
        diffuseColor = color * diffuse;
        rim = rimDomain * rimDomain * 0.8;
        backRim = clamp(-viewFacing, 0.0, 1.0) * 0.01;
    }
    return max(diffuseColor + color * (rim + backRim), vec3(0.0));
}
