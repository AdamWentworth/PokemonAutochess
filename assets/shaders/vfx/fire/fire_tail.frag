// --- FILE: assets/shaders/vfx/fire/fire_tail.frag ---
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

#include "assets/shaders/vfx/fire/fire_common.glsl"

// Flipbook 1
uniform int       u_UseFlipbook;
uniform sampler2D u_Flipbook;
uniform vec2      u_FlipbookGrid;
uniform float     u_FrameCount;
uniform float     u_Fps;

// Flipbook 2
uniform int       u_HasFlipbook2;
uniform sampler2D u_Flipbook2;
uniform vec2      u_FlipbookGrid2;
uniform float     u_FrameCount2;
uniform float     u_Fps2;

vec4 sampleAtlas(sampler2D tex, vec2 grid, float frames, float fps, vec2 localUV01, float seed, float t) {
    float speed = mix(0.85, 1.10, hash11(seed * 31.7 + 2.3));
    float f = floor(t * fps * speed + seed * frames);
    float frame = mod(f, max(1.0, frames));

    float cols = max(1.0, grid.x);
    float rows = max(1.0, grid.y);

    float col = mod(frame, cols);
    float rowFromTop = floor(frame / cols);
    float row = (rows - 1.0) - rowFromTop;

    vec2 cellUV = (vec2(col, row) + localUV01) / vec2(cols, rows);
    return texture(tex, cellUV);
}

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

vec3 tonemapSoftLocal(vec3 c) {
    return c / (vec3(1.0) + c);
}

void main() {
    float age = clamp(vAge01, 0.0, 1.0);
    float t = u_Time;

    vec2 uv = gl_PointCoord;
    uv.y = 1.0 - uv.y;

    vec2 cc = (uv - 0.5) * 2.0; // [-1..1]
    float x = cc.x;
    float y = clamp(uv.y, 0.0, 1.0);

    // Stronger base fade-in to kill "square bottom" look
    float bottomFade = smoothstep(0.00, 0.11, y);

    // --- Base/tight masks (UNCHANGED for fb2 / overall shape control) ---
    float baseT = smoothstep(0.00, 0.22, y);
    float xScaleBase = mix(2.55, 1.90, baseT);
    float yScaleBase = mix(1.05, 0.75, baseT);
    float reBase = length(vec2(cc.x * xScaleBase, cc.y * yScaleBase));
    float radialMaskBase = 1.0 - smoothstep(0.98, 1.10, reBase);
    float tightMask      = 1.0 - smoothstep(0.62, 0.88, reBase);

    // --- Hybrid mask: loose so widened fb1/hybrid isn't clipped ---
    float reLoose = length(cc * vec2(0.55, 0.85));
    float radialMaskLoose = 1.0 - smoothstep(0.98, 1.20, reLoose);

    float fade = (1.0 - age);
    fade = pow(mix(fade, 1.0, 0.25), 0.75);

    vec2 wobble = vec2(
        smoothFlicker(t * 0.9, vSeed + 0.17),
        smoothFlicker(t * 1.1, vSeed + 0.73)
    ) - 0.5;

    vec2 local1 = uv + wobble * 0.010;
    vec2 local2 = uv + wobble * 0.002;

    vec4 fb1 = vec4(1.0);
    vec4 fb2 = vec4(1.0);
    int has1 = (u_UseFlipbook == 1) ? 1 : 0;
    int has2 = (u_UseFlipbook == 1 && u_HasFlipbook2 == 1) ? 1 : 0;

    if (has1 == 1) {
        fb1 = sampleAtlas(u_Flipbook, u_FlipbookGrid, u_FrameCount, u_Fps, local1, vSeed, t);
        if (has2 == 1) fb2 = sampleAtlas(u_Flipbook2, u_FlipbookGrid2, u_FrameCount2, u_Fps2, local2, vSeed, t);
        else fb2 = fb1;
    }

    float fb1A   = clamp(fb1.a, 0.0, 1.0);
    float fb1Lum = clamp(dot(fb1.rgb, vec3(0.3333)), 0.0, 1.0);

    float speed = mix(0.95, 1.10, hash11(vSeed * 19.31));
    float flow  = t * 1.55 * speed;
    float flowY = flow * mix(0.75, 1.55, y * y);

    // Base width profile (kept from your previous file)
    float width = mix(0.30, 0.055, pow(y, 2.35));

    // AGGRESSIVE THICKENING FOR FB1/HYBRID:
    // >1 makes it thicker (because x/width becomes smaller).
    float fb1Thicken = 2.80;
    float widthHybrid = width * fb1Thicken;

    float yy = (y * 2.0 - 1.0);
    yy = yy * 1.45 + 0.38;
    yy /= 1.12;

    // Use the thicker width ONLY for the hybrid/procedural domain.
    vec2 p = vec2(x / widthHybrid, yy);
    p *= 1.22;

    float sway = fbm2D(vec2(x * 1.7, y * 3.8) + vec2(0.0, -flowY * 0.65) + vSeed * 7.0);
    p.x += (sway - 0.5) * 0.015 * (1.0 - y);

    float d0 = length(p);

    vec2 advP = advect(p * vec2(1.20, 1.0) + vSeed * 6.0, flowY, 0.25);
    float n = fbm2D(advP * vec2(2.7, 4.5) + vSeed * 11.0);
    float d = d0 + (n - 0.5) * 0.18 * (1.0 - y);

    float core  = clamp(1.0 - smoothstep(0.00, 0.88, d), 0.0, 1.0);
    float outer = clamp(1.0 - smoothstep(0.30, 1.05, d), 0.0, 1.0);

    float blobs = lickBlobs(x, y, advP, flowY, vSeed);
    float body  = clamp(smoothstep(0.92, 0.12, d), 0.0, 1.0);

    float procAlpha = body * (0.60 + 0.55 * blobs);
    procAlpha *= (0.92 + 0.15 * smoothFlicker(t * 1.2, vSeed));
    procAlpha *= bottomFade;
    procAlpha *= fade;

    procAlpha = 1.0 - exp(-procAlpha * 1.85);
    procAlpha = clamp(procAlpha, 0.0, 0.96);

    vec3 yellow = vec3(1.70, 1.20, 0.28);
    vec3 red    = vec3(1.45, 0.18, 0.06);
    vec3 orange = vec3(1.60, 0.55, 0.12);

    float wave = 0.5 + 0.5 * sin((x * 1.8 + y * 8.5 - flowY * 4.9) + vSeed * 7.0);

    float baseBoundary = 0.34;
    float segCount = 6.0;
    float kk = y * segCount - flowY * 0.55;
    float seg = floor(kk);
    float segRand  = hash11(seg + vSeed * 71.3);
    float segRand2 = hash11(seg + vSeed * 19.7 + 5.0);

    float tri1 = abs(fract((x * 0.85 + y * 1.05 - flowY * 0.18) * 2.8 + vSeed * 7.0) - 0.5) * 2.0;
    float tri2 = abs(fract((x * 1.10 - y * 0.60 - flowY * 0.14) * 3.8 + vSeed * 3.0) - 0.5) * 2.0;

    float zig = mix(tri1, tri2, 0.50 + 0.50 * (segRand - 0.5));
    zig = smoothstep(0.15, 0.85, zig);

    float warp = fbm2D(advect(vec2(x * 0.85, y * 1.2) + vSeed * 6.0, flowY, 0.22) * vec2(4.5, 7.5)) - 0.5;

    float jag = 0.0;
    jag += (segRand  - 0.5) * 0.10;
    jag += (segRand2 - 0.5) * 0.05;
    jag += (zig      - 0.5) * 0.14;
    jag += warp * 0.06;
    jag *= (1.0 - 0.55 * smoothstep(0.65, 1.0, y));

    float boundary = clamp(baseBoundary + jag, 0.14, 0.62);
    float splitWidth = 0.11;
    float redMask = smoothstep(boundary, boundary + splitWidth, y);

    vec3 procRgb = mix(yellow, red, redMask);

    float band = smoothstep(boundary - 0.02, boundary + 0.02, y) *
                 (1.0 - smoothstep(boundary + 0.02, boundary + 0.10, y));
    procRgb = mix(procRgb, orange, 0.55 * band);

    float climb = core * (1.0 - smoothstep(0.55, 0.95, y)) * (0.35 + 0.65 * wave);
    procRgb = mix(procRgb, yellow, 0.18 * climb);

    procRgb *= (1.18 + 0.35 * outer);

    // --- Hybrid path (procedural + fb1 modulation) ---
    vec3 hybridRgb = procRgb;
    float hybridAlpha = procAlpha;

    if (has1 == 1) {
        float aMod = mix(0.55, 1.65, fb1A);
        float lMod = mix(0.85, 1.25, fb1Lum);

        hybridAlpha = clamp(hybridAlpha * aMod, 0.0, 0.96);
        hybridRgb *= lMod;
        hybridRgb *= mix(vec3(1.0), fb1.rgb * 1.35, 0.30);
    }

    // --- fb2 path (keep as-is tight) ---
    vec3 fb2Rgb = fb2.rgb;
    float fb2Alpha = pow(clamp(fb2.a, 0.0, 1.0), 0.66);

    float hot = smoothstep(0.10, 0.55, 1.0 - y);
    vec3 tint = mix(red, yellow, hot);
    fb2Rgb *= tint * 1.30;

    fb2Alpha *= tightMask;
    fb2Alpha *= bottomFade;

    // Apply masks separately:
    float hybridMaskedA = hybridAlpha * radialMaskLoose * bottomFade;
    float fb2MaskedA    = fb2Alpha    * radialMaskBase;

    float mixW = 0.50;
    vec3 rgb = mix(hybridRgb, fb2Rgb, mixW);
    float alpha = mix(hybridMaskedA, fb2MaskedA, mixW);

    alpha *= fade;

    // Glow feel (premultiplied-friendly)
    alpha = clamp(alpha + 0.10 * outer * fade, 0.0, 0.985);

    float exposure = 2.60;
    rgb *= exposure;

    float emissive = (0.85 * outer + 0.45 * core) * fade;
    rgb *= (1.0 + 2.10 * emissive);

    rgb = tonemapSoftLocal(rgb);

    if (alpha < 0.003) discard;

    // premultiplied output
    rgb *= alpha;

    FragColor = vec4(rgb, alpha);
}
