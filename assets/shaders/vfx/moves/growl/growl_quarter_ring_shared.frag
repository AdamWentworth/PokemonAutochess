#version 330 core

in vec2 vUV;

uniform sampler2D uTexture;
uniform float uFade;
uniform vec3 uTevC0;
uniform vec3 uTevC1;
uniform float uTevK1A;
uniform vec3 uTintColor;
uniform float uPassAlphaMul;

out vec4 FragColor;

void main()
{
    // TEV stage shape from source:
    // rgb = mix(c1.rgb, c0.rgb, tex.rgb)
    // a   = mix(c1.a, c0.a, tex.a)
    vec4 tex = texture(uTexture, vUV);

    vec3 rgb = mix(uTevC1, uTevC0, tex.rgb) * uTintColor;
    float alpha = tex.a * uTevK1A;

    // Match game-style 6-bit alpha write in a lightweight way.
    alpha = floor(alpha * 63.0 + 0.5) / 63.0;
    alpha *= uFade * uPassAlphaMul;

    if (alpha <= 0.0) discard;
    FragColor = vec4(rgb, alpha);
}
