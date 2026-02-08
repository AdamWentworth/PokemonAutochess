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

float leafShape(vec2 p) {
    // Tapered leaf silhouette
    float t = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);
    float width = mix(0.95, 0.30, t);
    float yScale = mix(0.80, 1.20, t);
    float d = length(vec2(p.x / width, p.y * yScale));
    float leaf = smoothstep(1.0, 0.82, d);
    float baseTrim = smoothstep(-1.0, -0.72, p.y);
    return leaf * baseTrim;
}

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float spin = (vSeed * 6.2831853) + (u_Time * 0.15 + vSeed * 4.9) * 0.10;
    uv = rot(spin) * uv;

    // Cluster of leaves (3-5) with variation for a more organic look
    float cluster = 0.0;
    float baseRot = vSeed * 6.2831853;

    vec2 p0 = rot(baseRot + 0.0) * (uv * vec2(1.0, 1.15));
    vec2 p1 = rot(baseRot + 2.0943951) * (uv * vec2(0.95, 1.05));
    vec2 p2 = rot(baseRot + 4.1887902) * (uv * vec2(1.05, 0.95));

    cluster = max(cluster, leafShape(p0));
    cluster = max(cluster, leafShape(p1));
    cluster = max(cluster, leafShape(p2));

    // Smaller offset leaves for variation
    vec2 off = vec2(0.18, -0.10);
    cluster = max(cluster, leafShape(p0 + off * 0.7) * 0.85);
    cluster = max(cluster, leafShape(p1 - off * 0.6) * 0.80);

    float fadeIn = smoothstep(0.0, 0.18, age);
    float fadeOut = 1.0 - smoothstep(0.70, 1.0, age);

    float alpha = cluster * fadeIn * fadeOut;
    if (alpha < 0.01) discard;

    vec3 dark = vec3(0.07, 0.28, 0.10);
    vec3 mid  = vec3(0.16, 0.46, 0.16);
    vec3 light = vec3(0.36, 0.72, 0.30);

    float t = clamp((uv.y + 1.0) * 0.5, 0.0, 1.0);
    vec3 color = mix(dark, mid, t);
    color = mix(color, light, cluster * 0.45);

    float var = mix(0.85, 1.1, hash1(vSeed * 17.3));
    color *= var;

    FragColor = vec4(color, alpha);
}
