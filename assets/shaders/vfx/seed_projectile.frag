// assets/shaders/vfx/seed_projectile.frag
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

    // Slow spin for subtle life
    float spin = (vSeed * 6.2831853) + (u_Time * 0.75 + vSeed * 11.7) * 0.2;
    uv = rot(spin) * uv;

    // Seed silhouette (rounded base, tapered tip)
    vec2 p = uv;
    float t = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);
    float width = mix(0.85, 0.25, t);
    float yScale = mix(0.85, 1.15, t);
    float d = length(vec2(p.x / width, p.y * yScale));

    float seed = smoothstep(1.0, 0.84, d);
    float baseTrim = smoothstep(-1.0, -0.70, p.y);
    seed *= baseTrim;

    float alpha = seed * pow(1.0 - age, 0.65);
    if (alpha < 0.005) discard;

    // Shift toward golden yellow-brown with a hint of green
    vec3 dark = vec3(0.50, 0.40, 0.14);
    vec3 light = vec3(0.96, 0.86, 0.34);

    vec3 color = mix(dark, light, t);

    float var = mix(0.85, 1.1, hash1(vSeed * 23.17));
    color *= var * 1.18;

    // Small highlight line
    float vein = smoothstep(0.07, 0.0, abs(p.x)) * smoothstep(-0.9, 0.6, p.y);
    color = mix(color, vec3(0.98, 0.92, 0.46), vein * 0.28);

    FragColor = vec4(color, alpha);
}
