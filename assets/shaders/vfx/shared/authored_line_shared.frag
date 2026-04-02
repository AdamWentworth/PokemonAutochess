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
    vec3 rgb = uTevC0 * uTintColor;

    float alpha255 = clamp(vColor.a * uTevK1A * 255.0, 0.0, 255.0);
    float alpha = floor(alpha255 * 0.25) / 63.0;
    alpha *= uFade * uPassAlphaMul;

    if (alpha <= 0.0) discard;
    FragColor = vec4(rgb, alpha);
}
