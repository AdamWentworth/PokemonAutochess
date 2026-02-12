#version 330 core

in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTexture;
uniform float uFade;
uniform vec3 uTevC0;
uniform vec3 uTevC1;
uniform vec3 uTevK0;
uniform float uTevK1A;
uniform vec3 uTintColor;
uniform int uUseAlphaMaskForColor;
uniform float uPassAlphaMul;

out vec4 FragColor;

void main()
{
    vec4 tex = texture(uTexture, vUV);

    vec3 tevInput = tex.rgb;
    if (uUseAlphaMaskForColor != 0) {
        // Mask texture path: color comes from alpha/intensity, not source RGB hue.
        tevInput = vec3(tex.a);
    }

    // TEV-inspired combine matching the original 3-stage pixel path:
    // stage1_rgb = mix(C1, K0, tex.rgb)
    // stage2_rgb = C0 * stage1_rgb
    vec3 stage1 = mix(uTevC1, uTevK0, tevInput);
    vec3 rgb = uTintColor * (uTevC0 * stage1);

    // stage0/2 alpha path: alpha = vColor.a * K1a * tex.a
    float alpha = tex.a * vColor.a * uTevK1A * uFade * uPassAlphaMul;

    FragColor = vec4(rgb, alpha);
}
