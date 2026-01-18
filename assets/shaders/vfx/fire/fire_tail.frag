// assets/shaders/vfx/fire/fire_tail.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

#include "assets/shaders/vfx/fire/fire_common.glsl"

// internal "lick" blobs (lean into randomness, but not thin line filaments)
float lickBlobs(float x, float y, vec2 advP, float flowY, float seed) {
    float k = y * 6.6 + flowY * 0.55;
    float seg = floor(k);
    float f = fract(k);

    float cx1 = (hash11(seg + seed * 31.0) - 0.5) * 0.95 * (1.0 - y);
    float cx2 = (hash11(seg + seed * 73.0) - 0.5) * 0.95 * (1.0 - y);

    float w = mix(0.34, 0.085, y);

    vec2 q1 = vec2((x - cx1) / w,        (f - 0.30) / 0.70);
    vec2 q2 = vec2((x - cx2) / (w*0.85), (f - 0.45) / 0.65);

    float m1 = 1.0 - smoothstep(0.60, 1.00, length(q1 * vec2(1.0, 1.45)));
    float m2 = 1.0 - smoothstep(0.60, 1.00, length(q2 * vec2(1.0, 1.60)));

    float br = fbm2D(advP * vec2(7.0, 12.0) + seed * 17.0);
    float broken = smoothstep(0.25, 0.88, br);

    float gate = smoothstep(0.05, 0.22, y) * (1.0 - smoothstep(0.86, 1.0, y));

    float m = (m1 + 0.85 * m2) * broken * gate;
    return clamp(m, 0.0, 1.0);
}

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    vec2 uv = gl_PointCoord;
    uv.y = 1.0 - uv.y;

    float x = (uv.x - 0.5) * 2.0;
    float y = clamp(uv.y, 0.0, 1.0);

    float t = u_Time;
    float speed = mix(0.95, 1.10, hash11(vSeed * 19.31));
    float flow  = t * 2.35 * speed;

    float width = mix(0.58, 0.13, pow(y, 1.85));

    float yy = (y * 2.0 - 1.0);
    yy = yy * 1.26 + 0.29;
    yy /= 1.18;

    vec2 p = vec2(x / width, yy);
    p *= 1.22;

    float flowY = flow * mix(0.75, 1.55, y * y);

    float sway = fbm2D(vec2(x * 1.7, y * 3.8) + vec2(0.0, -flowY * 0.65) + vSeed * 7.0);
    p.x += (sway - 0.5) * 0.050 * (1.0 - y);

    float d0 = length(p);
    float edgeBand0 = smoothstep(0.60, 1.05, d0);

    vec2 c = curl2D(vec2(x * 1.8, y * 2.6 - flowY * 0.22) + vSeed * 11.0);
    p += c * (0.055 + 0.10 * edgeBand0) * pow(y, 1.35);

    float d = length(p);

    float edgeBand = smoothstep(0.60, 1.05, d);
    float coneAllow = smoothstep(0.08, 0.40, y);

    vec2 baseP = vec2(x, y);
    vec2 advP  = advect(baseP + vSeed * 7.0,  flowY, 0.18 * pow(y, 1.15));
    vec2 advE  = advect(baseP + vSeed * 19.0, flowY, 0.26 * pow(y, 1.30));

    float e1 = fbm2D(advE * vec2(10.5, 18.5) + vec2(0.0, -flowY * 0.30));
    float e2 = valueNoise2D(advE * vec2(32.0, 46.0) + vec2(4.7, -flowY * 0.70));

    float sp = (e2 - 0.5);
    sp = sp * sp * sp * 4.0;

    float edgeOffsetA = (e1 - 0.5) * 0.145 + sp * 0.060;
    d += edgeOffsetA * (1.0 - y) * edgeBand;

    float hf1 = valueNoise2D(advE * vec2(55.0, 90.0)  + vec2(1.3, -flowY * 0.95));
    float hf2 = valueNoise2D(advE * vec2(85.0, 120.0) + vec2(7.1, -flowY * 1.35));

    float microSpike = pow(clamp(hf1, 0.0, 1.0), 6.0);
    float microBite  = pow(clamp(hf2, 0.0, 1.0), 6.5);

    float microMask = edgeBand * coneAllow;
    d -= microSpike * 0.11 * microMask;
    d += microBite  * 0.07 * microMask;

    float burst = smoothstep(0.82, 0.98, smoothFlicker(t * 0.95, vSeed * 3.7));

    float sparseSeg = valueNoise2D(vec2(x * 4.5 + vSeed * 33.0, y * 9.0 + floor(t * 3.5)));
    float segMask = smoothstep(0.78, 0.98, sparseSeg);

    float lickN = fbm2D(advE * vec2(17.0, 28.0) + vec2(2.1, -flowY * 0.55));
    float lickSharp = smoothstep(0.62, 0.99, lickN);

    float lickMask = burst * segMask * lickSharp * edgeBand * coneAllow;
    d -= lickMask * 0.17;

    float biteN = valueNoise2D(advE * vec2(14.0, 20.0) + vec2(0.0, -flowY * 0.40));
    float biteMask = burst * smoothstep(0.86, 0.99, biteN) * edgeBand * coneAllow;
    d += biteMask * 0.08;

    float edgeSoft = 0.060;
    float outer = 1.0 - smoothstep(1.0, 1.0 + edgeSoft, d);
    outer = pow(clamp(outer, 0.0, 1.0), 0.70);

    float core = 1.0 - smoothstep(0.34, 0.54, d);
    core = pow(clamp(core, 0.0, 1.0), 0.85);

    float tongueN = fbm2D(advP * vec2(3.4, 7.5) + vec2(1.7, 0.0));
    float tongues = smoothstep(0.30, 0.92, tongueN);

    float d1 = fbm2D(advP * vec2(8.0, 15.0) + vec2(4.1, 0.0));
    float d2 = valueNoise2D(advP * vec2(22.0, 32.0) + vec2(1.3, 0.0));

    float inner = smoothstep(0.22, 0.95, d1);
    float micro = mix(0.92, 1.08, d2);

    float streak = 0.5 + 0.5 * sin((y * 10.5 - flowY * 4.1) + inner * 2.4);
    streak = smoothstep(0.12, 0.96, streak);

    float filN = fbm2D(advP * vec2(9.0, 28.0) + vec2(0.0, -flowY * 0.12));
    float fil = smoothstep(0.42, 0.92, filN);
    fil = pow(clamp(fil, 0.0, 1.0), 1.25);

    vec2 strandP = advect(baseP + vSeed * 10.0, flowY, 0.22 * y);
    float strand = ridgedFbm2D(strandP * vec2(7.0, 20.0));
    strand = smoothstep(0.28, 0.92, strand) * coneAllow;

    float lickF = mix(0.70, 1.00, smoothFlicker(t * 1.6, vSeed * 7.3));
    float licks = lickBlobs(x, y, advP, flowY, vSeed) * lickF;

    float fade = (1.0 - age);
    fade = mix(fade, 1.0, 0.18);
    fade = pow(fade, 0.70);

    float fl = smoothFlicker(t * 1.2, vSeed);

    float alpha = outer;

    float base = smoothstep(0.20, 0.00, y);
    float baseF = valueNoise2D(vec2(x * 18.0 + vSeed * 9.0, t * 14.0));
    alpha *= 1.0 + base * (baseF - 0.5) * 0.20;

    alpha *= mix(1.03, 1.22, tongues);
    alpha *= mix(0.92, 1.16, inner);
    alpha *= mix(0.96, 1.05, micro);
    alpha *= mix(0.95, 1.10, streak);

    alpha *= mix(0.96, 1.10, fil * coneAllow);
    alpha *= mix(0.92, 1.16, strand);
    alpha *= mix(0.90, 1.28, licks);

    alpha += core * (0.30 + 0.16 * inner);

    float tip = smoothstep(0.65, 1.0, y);
    float tipNoise = fbm2D(advect(vec2(x * 0.9, y * 1.2) + vSeed * 3.0, flowY, 0.25) + vec2(0.0, t * 0.40));
    float tipHoles = smoothstep(0.55, 0.95, tipNoise);
    alpha *= 1.0 - tip * tipHoles * 0.45;

    alpha *= fade;
    alpha *= mix(0.95, 1.08, fl);

    if (alpha < 0.006) discard;

    alpha = clamp(alpha * 0.80, 0.0, 1.0);

    float heat01 = clamp(
        0.76 * y +
        0.38 * (1.0 - core) +
        0.14 * edgeBand +
        age * 0.25,
        0.0, 1.0
    );

    heat01 = clamp(heat01 + 0.10 * core, 0.0, 1.0);
    heat01 = clamp(heat01 - 0.025 * licks * (1.0 - y), 0.0, 1.0);

    vec3 rgb = fireColor(heat01);

    float baseBlue = smoothstep(0.18, 0.00, y) * core;
    rgb = mix(rgb, vec3(0.08, 0.22, 0.95), baseBlue * 0.18);

    float soot = edgeBand * smoothstep(0.12, 0.80, y);
    rgb *= (1.0 - 0.24 * soot);

    rgb *= mix(1.0, 0.78, smoothstep(0.55, 1.0, y));

    float emissive = (0.55 * outer + 0.70 * core);
    emissive *= (0.92 + 0.38 * inner);
    emissive *= (0.94 + 0.18 * tongues);
    emissive *= (0.92 + 0.22 * strand);
    emissive *= (0.92 + 0.30 * licks);
    emissive *= fade;
    emissive *= mix(0.94, 1.08, smoothFlicker(t * 2.1, vSeed * 13.0));

    emissive *= 1.0 + 0.90 * pow(core, 3.2);

    float emberSeed = valueNoise2D(vec2(vSeed * 91.7, floor(t * 6.0)));
    float emberOn = smoothstep(0.985, 1.0, emberSeed);
    vec2 emberP = advect(vec2(x * 3.0, y * 3.0) + vSeed * 13.0, flowY, 0.28 * y) + vec2(0.0, -t * 1.4);
    float emberN = valueNoise2D(emberP * vec2(6.0, 10.0));
    float ember = smoothstep(0.90, 1.0, emberN) * emberOn * (0.35 + 0.65 * tip);
    rgb += ember * vec3(1.8, 0.55, 0.12) * 0.22;

    rgb *= (1.02 + 0.12 * licks);

    rgb *= (1.05 + 1.10 * emissive);
    rgb = tonemapSoft(rgb);

    FragColor = vec4(rgb, alpha);
}
