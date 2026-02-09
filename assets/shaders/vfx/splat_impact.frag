// assets/shaders/vfx/splat_impact.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

float hash1(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r = length(uv);
    float ang = atan(uv.y, uv.x);

    // Irregular edge "splat" shape
    float wobble = 0.12 * sin(ang * 5.0 + vSeed * 6.2831853);
    wobble += 0.06 * sin(ang * 11.0 + vSeed * 2.3);
    float edge = 0.95 + wobble;

    float splat = smoothstep(edge, edge - 0.20, r);
    float core = smoothstep(0.55, 0.0, r);
    float shape = max(splat, core * 0.85);

    float fadeIn = smoothstep(0.0, 0.10, age);
    float fadeOut = 1.0 - smoothstep(0.55, 1.0, age);
    float alpha = shape * fadeIn * fadeOut;
    if (alpha < 0.01) discard;

    vec3 dark = vec3(0.40, 0.34, 0.28);
    vec3 light = vec3(0.90, 0.82, 0.72);
    float var = mix(0.85, 1.15, hash1(vSeed * 17.1));
    vec3 color = mix(dark, light, core) * var;

    FragColor = vec4(color, alpha);
}
