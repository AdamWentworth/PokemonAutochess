// assets/shaders/vfx/claw_swipe.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float hash1(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

float strokeMask(vec2 q, float xOff, float halfLen, float width) {
    float x = abs(q.x - xOff);
    float y = abs(q.y);

    // Taper width aggressively near each end so strokes read as slashes, not rectangles.
    float tipT = clamp(y / max(0.0001, halfLen), 0.0, 1.0);
    float tipNarrow = mix(1.0, 0.04, smoothstep(0.58, 1.0, tipT));
    float localWidth = width * tipNarrow;
    float edgeSoft = mix(0.010, 0.004, smoothstep(0.58, 1.0, tipT));

    float core = 1.0 - smoothstep(localWidth, localWidth + edgeSoft, x);
    float lenMask = 1.0 - smoothstep(halfLen, halfLen + 0.030, y);

    return core * lenMask;
}

void main() {
    float age = clamp(vAge01, 0.0, 1.0);
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    bool metallic = (vSeed >= 0.55);

    // Fixed strong diagonal, tiny per-particle jitter.
    float angle = radians(-34.0 + (hash1(vSeed * 17.0) - 0.5) * 4.0);
    vec2 q = rot(angle) * p;

    float halfLen = metallic ? 0.86 : 0.92;
    float width = metallic ? 0.055 : 0.072;

    float s1 = strokeMask(q, -0.34, halfLen, width);
    float s2 = strokeMask(q,  0.00, halfLen, width);
    float s3 = strokeMask(q,  0.34, halfLen, width);

    float o1 = strokeMask(q, -0.34, halfLen, width * 1.95) - s1;
    float o2 = strokeMask(q,  0.00, halfLen, width * 1.95) - s2;
    float o3 = strokeMask(q,  0.34, halfLen, width * 1.95) - s3;

    float marks = max(s1, max(s2, s3));
    float outline = clamp(max(o1, max(o2, o3)), 0.0, 1.0);

    float fadeIn = smoothstep(0.0, metallic ? 0.06 : 0.04, age);
    float fadeOut = 1.0 - smoothstep(metallic ? 0.60 : 0.78, 1.0, age);
    float outlineWeight = metallic ? 0.55 : 0.70;
    float alpha = (marks * 1.0 + outline * outlineWeight) * fadeIn * fadeOut;
    if (!metallic) {
        alpha *= 1.22;
    }

    vec3 ink = vec3(0.06, 0.06, 0.06);
    vec3 white = vec3(0.98, 0.98, 0.98);
    vec3 color = mix(ink, white, marks);

    if (metallic) {
        vec3 silver = vec3(0.82, 0.87, 0.93);
        vec3 shine = vec3(0.97, 0.99, 1.00);
        float shimmer = 0.85 + 0.15 * hash1(vSeed * 31.0 + floor(u_Time * 28.0));
        color = mix(silver, shine, marks * 0.65) * shimmer;

        float glint = smoothstep(0.09, 0.0, length(q - vec2(0.20, -0.18)));
        glint += smoothstep(0.09, 0.0, length(q - vec2(-0.18, 0.16)));
        alpha += glint * (1.0 - age) * 0.55;
    }

    if (alpha < 0.01) discard;
    FragColor = vec4(color, alpha);
}
