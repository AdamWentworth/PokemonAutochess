// assets/shaders/vfx/impact_spark.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

float hash1(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r = length(uv);

    float core = smoothstep(0.8, 0.0, r);
    float halo = smoothstep(1.0, 0.2, r) * 0.45;
    float shape = core + halo;

    float fadeIn = smoothstep(0.0, 0.12, age);
    float fadeOut = 1.0 - smoothstep(0.55, 1.0, age);
    float alpha = shape * fadeIn * fadeOut;
    if (alpha < 0.01) discard;

    float t = hash1(vSeed * 17.3);
    vec3 hot = vec3(0.98, 0.92, 0.88);
    vec3 warm = vec3(0.96, 0.72, 0.66);
    vec3 color = mix(warm, hot, t);
    color *= 0.80;

    alpha *= 0.70;
    FragColor = vec4(color, alpha);
}
