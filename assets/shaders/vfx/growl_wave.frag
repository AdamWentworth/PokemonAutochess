// assets/shaders/vfx/growl_wave.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float lineMask(float y, float center, float thick, float soft) {
    float d = abs(y - center);
    return 1.0 - smoothstep(thick, thick + soft, d);
}

void main() {
    float age = clamp(vAge01, 0.0, 1.0);
    vec2 p = gl_PointCoord * 2.0 - 1.0;

    // Single cone: tight at source (left), wider toward target (+X).
    float x = p.x + 0.95;
    float maxLen = mix(1.15, 1.92, age);
    float halfW = 0.03 + x * 0.46;

    float front = smoothstep(0.0, 0.06, x) * (1.0 - smoothstep(maxLen, maxLen + 0.18, x));
    float widthMask = 1.0 - smoothstep(halfW, halfW + 0.09, abs(p.y));
    float cone = front * widthMask;
    if (cone <= 0.0) discard;

    // 3 fuzzy forward streak lines inside ONE cone.
    float t = 0.010 + 0.010 * age;
    float soft = t * 1.8;
    float c0 = 0.0;
    float c1 = halfW * 0.34;
    float c2 = -halfW * 0.34;

    float l0 = lineMask(p.y, c0, t * 1.10, soft);
    float l1 = lineMask(p.y, c1, t, soft);
    float l2 = lineMask(p.y, c2, t, soft);
    float lines = l0 * 1.2 + l1 + l2;

    // Add fuzzy breakup without creating extra cone bands.
    float grain = hash2(vec2(floor((p.x + 1.0) * 80.0) + vSeed * 13.0,
                             floor((p.y + 1.0) * 60.0) + floor(u_Time * 20.0)));
    float fuzz = mix(0.82, 1.18, grain);

    // Faint fill keeps it readable as one cone.
    float fill = 0.22 * cone;

    float fadeIn = smoothstep(0.0, 0.06, age);
    float fadeOut = 1.0 - smoothstep(0.86, 1.0, age);
    float alpha = (lines * fuzz * 0.78 + fill) * cone * fadeIn * fadeOut;
    if (alpha < 0.01) discard;

    vec3 nearCol = vec3(0.93, 0.28, 0.14);
    vec3 farCol  = vec3(0.70, 0.09, 0.08);
    float grad = clamp(x / max(0.001, maxLen), 0.0, 1.0);
    vec3 color = mix(nearCol, farCol, grad);

    FragColor = vec4(color, clamp(alpha, 0.0, 1.0));
}
