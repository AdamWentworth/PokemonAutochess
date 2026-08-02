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
    float spin = (vSeed * 6.2831853) +
        (u_Time * 0.9 + vSeed * 7.1) * 0.18;
    uv = rot(spin) * uv;

    vec2 p = uv;
    p.y = p.y * 1.15 + 0.05;
    float t = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);
    float width = max(mix(0.95, 0.25, t), 0.2);
    float leaf = smoothstep(
        1.0,
        0.82,
        length(vec2(p.x / width, p.y)));
    leaf *= smoothstep(-1.0, -0.6, p.y);

    float alpha = leaf * pow(1.0 - age, 0.58);
    if (alpha < 0.02) discard;

    vec3 color = mix(
        vec3(0.28, 0.64, 0.12),
        vec3(0.78, 0.96, 0.32),
        t);
    color *= mix(0.90, 1.08, hash1(vSeed * 11.37));
    float vein = smoothstep(0.08, 0.0, abs(p.x)) *
        smoothstep(-0.8, 0.5, p.y);
    color = mix(color, vec3(0.86, 1.0, 0.42), vein * 0.35);

    FragColor = vec4(color, alpha);
}
