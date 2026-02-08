// assets/shaders/vfx/leech_drain_dot.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

float hash1(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float r = length(p);
    float core = smoothstep(1.0, 0.0, r);

    float fadeIn = smoothstep(0.0, 0.15, age);
    float fadeOut = 1.0 - smoothstep(0.70, 1.0, age);

    float alpha = core * fadeIn * fadeOut;
    if (alpha < 0.01) discard;

    vec3 dark = vec3(0.10, 0.50, 0.18);
    vec3 light = vec3(0.45, 0.95, 0.40);

    float flick = mix(0.85, 1.15, hash1(vSeed * 37.1 + u_Time * 1.7));
    vec3 color = mix(dark, light, 0.65) * flick;

    FragColor = vec4(color, alpha);
}
