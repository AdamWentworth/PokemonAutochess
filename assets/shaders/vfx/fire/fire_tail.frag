// assets/shaders/vfx/fire/fire_tail.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

#include "assets/shaders/vfx/fire/fire_common.glsl"


// Optional flipbook detail overlay ("best of both")
uniform int   u_UseFlipbook;      // set by ParticleSystem (0/1)
uniform sampler2D u_Flipbook;     // atlas
uniform vec2  u_FlipbookGrid;     // (cols, rows)
uniform float u_FrameCount;
uniform float u_Fps;

vec4 sampleFlipbook(vec2 localUV01, float seed, float t) {
    // localUV01 expected in [0..1], Y already flipped
    float speed = mix(0.85, 1.08, hash11(seed * 19.31 + 3.1));
    float f = floor(t * u_Fps * speed + seed * u_FrameCount);
    float frame = mod(f, max(1.0, u_FrameCount));

    float cols = max(1.0, u_FlipbookGrid.x);
    float rows = max(1.0, u_FlipbookGrid.y);

    float col = mod(frame, cols);
    float rowFromTop = floor(frame / cols);
    float row = (rows - 1.0) - rowFromTop;

    vec2 cellUV = (vec2(col, row) + localUV01) / vec2(cols, rows);
    return texture(u_Flipbook, cellUV);
}


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

    // Flipbook detail (optional). We only use it to modulate alpha/color
    // while keeping the procedural silhouette and palette.
    vec4 fbTex = vec4(1.0);
    float fbA = 1.0;
    float fbLum = 1.0;
    if (u_UseFlipbook == 1) {
        vec2 local = uv;

        // small wobble only (keeps atlas from looking "stuck")
        vec2 wobble = vec2(
            smoothFlicker(t * 0.9, vSeed + 0.17),
            smoothFlicker(t * 1.1, vSeed + 0.73)
        ) - 0.5;
        local += wobble * (0.010 * (1.0 - age));

        fbTex = sampleFlipbook(local, vSeed, t);
        fbA = clamp(fbTex.a, 0.0, 1.0);
        fbLum = clamp(dot(fbTex.rgb, vec3(0.3333)), 0.0, 1.0);
    }

    float speed = mix(0.95, 1.10, hash11(vSeed * 19.31));
    float flow  = t * 1.55 * speed;

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
    p += c * (0.038 + 0.07 * edgeBand0) * pow(y, 1.25);

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

        // --- Outward flame flares: break the teardrop silhouette ---
    // Use a separate dShape so we can exaggerate silhouette without destabilizing earlier math
    float dShape = d;

    // Flares appear in moving "segments" traveling upward
    float flareSegCount = 6.0;
    float flareKey = y * flareSegCount + flowY * 0.35 + vSeed * 9.0;
    float flareSeg = floor(flareKey);

    float flareRnd = hash11(flareSeg + vSeed * 91.7);
    float flareGate = smoothstep(0.40, 1.00, flareRnd);  // lower = more frequent flares

    // Organic pattern that drifts upward
    float flareN = fbm2D(advE * vec2(6.0, 16.0) + vec2(0.0, -flowY * 0.22) + vSeed * 5.0);
    float flare = smoothstep(0.55, 0.92, flareN) * flareGate;

    // Only push outward near edges, and mostly mid/top
    float flareEdge = edgeBand * coneAllow;
    flareEdge *= smoothstep(0.12, 0.95, y);

    // Stronger toward top, but still present mid flame
    float flareStrength = mix(0.08, 0.22, smoothstep(0.20, 0.95, y));

    // This makes the flame extend outward
    dShape -= flare * flareEdge * flareStrength;

    // Use dShape for silhouette-defining terms
    float edgeSoft = 0.060;
    float outer = 1.0 - smoothstep(1.0, 1.0 + edgeSoft, dShape);
    outer = pow(clamp(outer, 0.0, 1.0), 0.70);

    float core = 1.0 - smoothstep(0.34, 0.54, dShape);
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

        // ---- Cartoon 2-tone (BOTTOM yellow -> TOP red) ----

    // Thicker (less see-through) but still translucent
    alpha = clamp(alpha, 0.0, 1.0);
    alpha = 1.0 - exp(-alpha * 1.85);
    alpha = clamp(alpha, 0.0, 0.96);

    // Colors (bright + saturated)
    vec3 yellow = vec3(1.70, 1.20, 0.28);
    vec3 red    = vec3(1.45, 0.18, 0.06);
    vec3 orange = vec3(1.60, 0.55, 0.12);

    // Needed for optional "yellow tongues climbing upward"
    float wave = 0.5 + 0.5 * sin((x * 1.8 + y * 8.5 - flowY * 4.9) + vSeed * 7.0);

    // --- EXAGGERATED jagged boundary between yellow (bottom) and red (top) ---

    float baseBoundary = 0.34;

    // Chunky segment changes (random per segment)
    float segCount = 6.0;
    float k = y * segCount - flowY * 0.55;
    float seg = floor(k);
    float segRand  = hash11(seg + vSeed * 71.3);
    float segRand2 = hash11(seg + vSeed * 19.7 + 5.0);

    // Sharp zig-zag (triangle waves)
    float tri1 = abs(fract((x * 0.85 + y * 1.05 - flowY * 0.18) * 2.8 + vSeed * 7.0) - 0.5) * 2.0;
    float tri2 = abs(fract((x * 1.10 - y * 0.60 - flowY * 0.14) * 3.8 + vSeed * 3.0) - 0.5) * 2.0;

    float zig = mix(tri1, tri2, 0.50 + 0.50 * (segRand - 0.5));
    zig = smoothstep(0.15, 0.85, zig);

    // Extra organic warp (noise)
    float warp = fbm2D(advect(vec2(x * 0.85, y * 1.2) + vSeed * 6.0, flowY, 0.22) * vec2(4.5, 7.5)) - 0.5;

    float jag = 0.0;
    jag += (segRand  - 0.5) * 0.10;
    jag += (segRand2 - 0.5) * 0.05;
    jag += (zig      - 0.5) * 0.14;
    jag += warp * 0.06;

    jag *= (1.0 - 0.55 * smoothstep(0.65, 1.0, y)); // calmer near top

    float boundary = clamp(baseBoundary + jag, 0.14, 0.62);

    float splitWidth = 0.11;
    float redMask = smoothstep(boundary, boundary + splitWidth, y);

    // Base 2-tone
    vec3 rgb = mix(yellow, red, redMask);

    // Orange band on the jagged split
    float band = smoothstep(boundary - 0.02, boundary + 0.02, y) *
                 (1.0 - smoothstep(boundary + 0.02, boundary + 0.10, y));
    rgb = mix(rgb, orange, 0.55 * band);

    // Small yellow tongues climbing into the red (cartoon fire feel)
    float climb = core * (1.0 - smoothstep(0.55, 0.95, y)) * (0.35 + 0.65 * wave);
    rgb = mix(rgb, yellow, 0.18 * climb);

    // Brightness lift
    rgb *= (1.18 + 0.35 * outer);

    // Slight darkening near top edges only
    float topRim = smoothstep(0.55, 1.0, y) * smoothstep(0.40, 0.95, edgeBand);
    rgb *= (1.0 - 0.08 * topRim);

    // Bottom a bit denser
    float bottomBoost = (1.0 - smoothstep(0.00, 0.22, y));
    alpha = clamp(alpha + bottomBoost * 0.10, 0.0, 0.96);

    // Emissive (enough to glow, not enough to wash to white)
    float emissive = (0.60 * outer + 0.22 * core);
    emissive *= (0.95 + 0.25 * (1.0 - y));
    emissive *= fade;
    rgb *= (1.0 + 1.05 * emissive);

    // Prevent additive from turning it white
    rgb = tonemapSoft(rgb);

    FragColor = vec4(rgb, alpha);
}
