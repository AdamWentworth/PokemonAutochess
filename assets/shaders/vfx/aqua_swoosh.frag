// assets/shaders/vfx/aqua_swoosh.frag
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

    float alpha = 0.0;
    vec3 color = vec3(0.48, 0.86, 1.00);

    // Tail-whip style airy swoosh
    if (vSeed < 0.40) {
        float band = abs(p.y - (0.60 * p.x));
        float arc = smoothstep(0.35, 0.0, band) * smoothstep(1.0, 0.45, r);
        alpha = arc * (1.0 - age);
        color = vec3(0.58, 0.92, 1.00);
    }
    // Bubble style
    else if (vSeed < 0.75) {
        float shell = smoothstep(0.75, 0.68, r) - smoothstep(0.68, 0.60, r);
        float fill = smoothstep(0.62, 0.0, r) * 0.25;
        float wobble = 0.85 + 0.15 * sin(u_Time * 8.0 + vSeed * 20.0);
        alpha = (shell * 1.1 + fill) * wobble * (1.0 - age);
        color = vec3(0.70, 0.95, 1.00);
    }
    // Water-gun style denser streak
    else {
        float streak = smoothstep(0.24, 0.0, abs(p.y)) * smoothstep(1.0, 0.30, abs(p.x));
        float core = smoothstep(0.58, 0.0, r);
        alpha = (streak * 1.1 + core * 0.55) * (1.0 - age);
        color = vec3(0.52, 0.82, 1.00);
    }

    float fadeIn = smoothstep(0.0, 0.12, age);
    float fadeOut = 1.0 - smoothstep(0.70, 1.0, age);
    alpha *= fadeIn * fadeOut;

    float flick = 0.92 + 0.08 * hash1(vSeed * 31.0 + floor(u_Time * 16.0));
    color *= flick;

    if (alpha < 0.01) discard;
    FragColor = vec4(color, alpha);
}

