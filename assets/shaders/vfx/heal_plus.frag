// assets/shaders/vfx/heal_plus.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

float hash1(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    vec2 uv = gl_PointCoord * 2.0 - 1.0;

    float spin = (vSeed * 6.2831853) + (u_Time * 0.55 + vSeed * 8.1) * 0.12;
    uv = rot(spin) * uv;

    float thickness = 0.22;
    float arm = 0.85;

    float h = smoothstep(thickness, thickness - 0.04, abs(uv.y)) * smoothstep(arm, arm - 0.08, abs(uv.x));
    float v = smoothstep(thickness, thickness - 0.04, abs(uv.x)) * smoothstep(arm, arm - 0.08, abs(uv.y));

    float plus = max(h, v);

    float fadeIn = smoothstep(0.0, 0.18, age);
    float fadeOut = 1.0 - smoothstep(0.70, 1.0, age);
    float alpha = plus * fadeIn * fadeOut;
    if (alpha < 0.01) discard;

    vec3 dark = vec3(0.10, 0.55, 0.18);
    vec3 light = vec3(0.35, 0.95, 0.45);

    float t = clamp(1.0 - age, 0.0, 1.0);
    vec3 color = mix(dark, light, t);

    float var = mix(0.85, 1.1, hash1(vSeed * 29.1));
    color *= var * 1.2;

    FragColor = vec4(color, alpha);
}
