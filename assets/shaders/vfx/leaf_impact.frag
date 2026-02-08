// assets/shaders/vfx/leaf_impact.frag
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
    float spin = (vSeed * 6.2831853) + (u_Time * 0.6 + vSeed * 7.1) * 0.15;
    uv = rot(spin) * uv;

    // Leaf silhouette (teardrop-ish, tapered at the tip)
    vec2 p = uv;
    p.y = p.y * 1.15 + 0.05;
    float t = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);

    float width = mix(0.95, 0.25, t);
    width = max(width, 0.2);
    float d = length(vec2(p.x / width, p.y));

    float leaf = smoothstep(1.0, 0.82, d);

    // Trim the stem base a bit
    float stemMask = smoothstep(-1.0, -0.6, p.y);
    leaf *= stemMask;

    // Soft edge + lifetime fade
    float alpha = leaf * pow(1.0 - age, 0.65);
    if (alpha < 0.02) discard;

    vec3 dark = vec3(0.12, 0.45, 0.15);
    vec3 light = vec3(0.50, 0.88, 0.36);
    vec3 color = mix(dark, light, t);

    float var = mix(0.85, 1.1, hash1(vSeed * 11.37));
    color *= var;

    // Central vein highlight
    float vein = smoothstep(0.08, 0.0, abs(p.x)) * smoothstep(-0.8, 0.5, p.y);
    color = mix(color, vec3(0.62, 0.96, 0.42), vein * 0.35);

    FragColor = vec4(color, alpha);
}
