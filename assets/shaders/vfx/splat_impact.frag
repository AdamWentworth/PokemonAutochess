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

    // Quick pop-in with subtle expansion
    float grow = mix(0.55, 1.00, smoothstep(0.0, 0.30, age));
    float r2 = r / grow;

    float spikeCount = 10.0 + floor(hash1(vSeed * 31.7) * 4.0); // 10..13
    float phase = vSeed * 6.2831853;
    float spike = abs(sin(ang * spikeCount + phase));
    spike = pow(spike, 0.75);

    // Radial streaks (muzzle-flash like)
    float streak = smoothstep(0.65, 0.0, r2) * spike;
    float core = smoothstep(0.75, 0.0, r2);

    // Shock ring
    float ringRadius = 0.70;
    float ringWidth = 0.06;
    float ring = smoothstep(ringRadius, ringRadius - 0.02, r2) -
                 smoothstep(ringRadius - ringWidth, ringRadius - ringWidth - 0.02, r2);

    float fadeIn = smoothstep(0.0, 0.06, age);
    float fadeOut = 1.0 - smoothstep(0.35, 1.0, age);
    float alpha = (core * 0.9 + streak * 0.9 + ring * 0.75) * fadeIn * fadeOut;
    if (alpha < 0.01) discard;

    // Warm white with faint red tint
    vec3 hot = vec3(1.00, 0.96, 0.92);
    vec3 warm = vec3(0.98, 0.78, 0.70);
    float tint = clamp(r2 * 0.9, 0.0, 1.0);
    vec3 color = mix(hot, warm, tint);

    color *= 0.85; // less bright overall
    alpha *= 0.80;

    FragColor = vec4(color, alpha);
}
