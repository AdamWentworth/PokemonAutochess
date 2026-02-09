// assets/shaders/vfx/leech_root.frag
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
    float spin = (vSeed * 6.2831853) + (u_Time * 0.35 + vSeed * 5.3) * 0.12;
    uv = rot(spin) * uv;

    float squash = mix(0.88, 1.12, hash1(vSeed * 3.7));
    uv.y *= squash;

    // Leaf silhouette (match grass impact, but softer)
    vec2 p = uv;
    p.y = p.y * 1.15 + 0.05;
    float t = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);

    float width = mix(0.95, 0.25, t);
    width = max(width, 0.2);
    float d = length(vec2(p.x / width, p.y));

    float leaf = smoothstep(1.0, 0.82, d);
    float stemMask = smoothstep(-1.0, -0.6, p.y);
    leaf *= stemMask;

    // Soft glow halo around the leaf
    float glow = smoothstep(1.25, 0.90, d);
    glow = max(0.0, glow - leaf * 0.65);

    float fadeIn = smoothstep(0.0, 0.16, age);
    float fadeOut = 1.0 - smoothstep(0.78, 1.0, age);
    float pulse = 0.85 + 0.15 * sin(u_Time * 2.1 + vSeed * 6.2831853);

    float alpha = (leaf + glow * 0.45) * fadeIn * fadeOut;
    if (alpha < 0.01) discard;

    vec3 dark = vec3(0.10, 0.40, 0.16);
    vec3 light = vec3(0.45, 0.92, 0.42);
    vec3 glowCol = vec3(0.62, 1.00, 0.55);

    vec3 color = mix(dark, light, t);
    color = mix(color, glowCol, glow * 0.7);

    float var = mix(0.88, 1.12, hash1(vSeed * 17.3));
    color *= var * pulse;

    FragColor = vec4(color, alpha);
}
