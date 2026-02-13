#version 330 core

in vec4 vColor;

uniform float uFade;
uniform vec3 uTevC0;
uniform float uTevK1A;
uniform vec3 uTintColor;
uniform float uPassAlphaMul;

out vec4 FragColor;

void main()
{
    // RenderDoc 1128 pixel path resolves RGB from c0 and alpha from raster alpha * k1a.
    vec3 rgb = uTevC0 * uTintColor;

    // Emulate the original 8-bit -> 6-bit alpha quantization (prev.a >> 2) / 63.
    float alpha255 = clamp(vColor.a * uTevK1A * 255.0, 0.0, 255.0);
    float alpha = floor(alpha255 * 0.25) / 63.0;
    alpha *= uFade * uPassAlphaMul;

    FragColor = vec4(rgb, alpha);
}
