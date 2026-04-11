#version 330 core

in vec2 vUV;

uniform sampler2D uTexture;
uniform float uFade;
uniform vec3 uTevC0;
uniform vec3 uTevC1;
uniform float uTevC0A;
uniform float uTevC1A;
uniform float uTevK1A;
uniform vec3 uTintColor;
uniform float uPassAlphaMul;
uniform float uDualSourceBlendEnabled;

out vec4 FragColor;
out vec4 FragBlendAlpha;

float tevMixU8(float a, float b, float t)
{
    float a8 = floor(clamp(a, 0.0, 1.0) * 255.0 + 0.5);
    float b8 = floor(clamp(b, 0.0, 1.0) * 255.0 + 0.5);
    float t8 = floor(clamp(t, 0.0, 1.0) * 255.0 + 0.5);
    float tc = t8 + floor(t8 / 128.0);
    float out8 = floor((a8 * 256.0 + (b8 - a8) * tc + 128.0) / 256.0);
    return clamp(out8 / 255.0, 0.0, 1.0);
}

vec3 tevMixU8(vec3 a, vec3 b, vec3 t)
{
    return vec3(
        tevMixU8(a.r, b.r, t.r),
        tevMixU8(a.g, b.g, t.g),
        tevMixU8(a.b, b.b, t.b)
    );
}

void main()
{
    vec4 tex = texture(uTexture, vUV);
    vec3 colorMask = (uUseAlphaMaskForColor > 0.5) ? vec3(tex.a) : tex.rgb;
    vec3 rgb = tevMixU8(uTevC1, uTevC0, colorMask) * uTintColor;
    float alpha = tevMixU8(uTevC1A, uTevC0A, tex.a);
    alpha *= uFade * uPassAlphaMul;

    if (alpha <= 0.0) discard;
    float mainAlpha = alpha;
    if (uDualSourceBlendEnabled > 0.5) {
        mainAlpha = floor(clamp(alpha, 0.0, 1.0) * 63.0 + 0.5) / 63.0;
    }
    FragColor = vec4(rgb, mainAlpha);
    FragBlendAlpha = (uDualSourceBlendEnabled > 0.5)
        ? vec4(0.0, 0.0, 0.0, clamp(alpha, 0.0, 1.0))
        : vec4(0.0);
}
