// assets/shaders/vfx/particle.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

// Flipbook
uniform sampler2D u_Flipbook;
uniform vec2  u_FlipbookGrid;   // (cols, rows)
uniform float u_FrameCount;     // e.g. 40
uniform float u_Fps;            // e.g. 30

float hash1(float x) {
    return fract(sin(x * 12.9898) * 43758.5453);
}

// Simple 2D value-noise-ish flicker (cheap)
float flicker(float t, float seed) {
    float a = hash1(floor(t * 17.0 + seed * 101.0));
    float b = hash1(floor(t * 17.0 + seed * 101.0) + 1.0);
    float f = fract(t * 17.0 + seed * 101.0);
    return mix(a, b, f);
}

// Color ramp for fire (HDR-ish; additive blend will handle glow)
vec3 fireRamp(float x) {
    // x: 0 (young) -> 1 (old)
    vec3 hot   = vec3(3.2, 2.0, 0.6);  // yellow-white
    vec3 mid   = vec3(2.2, 0.8, 0.2);  // orange
    vec3 cool  = vec3(1.2, 0.2, 0.05); // red
    vec3 c = mix(hot, mid, smoothstep(0.0, 0.6, x));
    c = mix(c, cool, smoothstep(0.5, 1.0, x));
    return c;
}

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    // Local point sprite UV
    vec2 local = gl_PointCoord;

    // Keep upright if your atlas was authored top-to-bottom
    local.y = 1.0 - local.y;

    // Soft circular-ish mask so points don't look like squares
    vec2 p = local - vec2(0.5);
    float r = length(p) * 2.0;
    float soft = smoothstep(1.05, 0.55, r); // softer edges

    // Heat distortion (tiny UV wobble)
    float t = u_Time;
    float fl = flicker(t, vSeed);
    vec2 wobble = (vec2(flicker(t * 0.9, vSeed + 0.17), flicker(t * 1.1, vSeed + 0.73)) - 0.5) * 0.08;
    local += wobble * (1.0 - age);

    // Flipbook frame selection
    float f = floor(u_Time * u_Fps + vSeed * u_FrameCount);
    float frame = mod(f, u_FrameCount);

    float cols = u_FlipbookGrid.x;
    float rows = u_FlipbookGrid.y;

    float col = mod(frame, cols);
    float rowFromTop = floor(frame / cols);
    float row = (rows - 1.0) - rowFromTop;

    vec2 cellUV = (vec2(col, row) + local) / vec2(cols, rows);
    vec4 tex = texture(u_Flipbook, cellUV);

    // If your flipbook has premultiplied alpha, remove this; assumed NOT premultiplied here.
    float alpha = tex.a;

    // Age shaping:
    // - Fade out with age
    // - Slightly shrink contribution near end
    float fade = (1.0 - age);
    fade *= fade; // smoother fade

    // Boost core brightness + add color ramp
    vec3 ramp = fireRamp(age);
    vec3 rgb = tex.rgb * ramp;

    // Apply mask + fade + flicker
    float intensity = soft * fade;
    intensity *= mix(0.85, 1.25, fl);

    // Alpha drives additive blending strength
    alpha *= intensity;

    if (alpha < 0.01) discard;

    // Output for additive blending
    FragColor = vec4(rgb * alpha, alpha);
}
